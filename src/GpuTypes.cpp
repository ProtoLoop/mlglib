#include "GpuTypes.h"

#include "Defer.h"
#include "GpuHelper.h"
#include "TextureFormat.h"

#include <webgpu/webgpu_cpp.h>

GpuTextureStagingBuffer::GpuTextureStagingBuffer(const GpuHelper& gpuHelper,
    wgpu::Buffer stagingBuffer,
    const uint32_t width,
    const uint32_t height,
    const uint32_t mipCount,
    const TextureFormat& format,
    const std::string_view name)
    : m_GpuHelper(&gpuHelper),
      m_StagingBuffer(std::move(stagingBuffer)),
      m_Width(width),
      m_Height(height),
      m_MipCount(mipCount),
      m_Format(format),
      m_Name(name)
{
    MLG_ASSERT(m_StagingBuffer, "Invalid staging buffer");
    MLG_ASSERT(format.CheckDimensions(m_Width, m_Height, m_MipCount),
        "Invalid texture dimensions or mip count: {}x{} with mip count {}",
        m_Width,
        m_Height,
        m_MipCount);
    MLG_ASSERT(!m_Format.IsUnknown(), "Unknown texture format: {}", m_Format.GetName());

    void* p = m_StagingBuffer.GetMappedRange();
    m_MappedMemory = std::span<std::byte>(static_cast<std::byte*>(p), m_StagingBuffer.GetSize());
}

Result<std::span<std::byte>>
GpuTextureStagingBuffer::GetMappedRange()
{
    MLG_CHECKV(m_StagingBuffer, "Invalid staging buffer");

    return m_MappedMemory;
}

Result<>
GpuTextureStagingBuffer::StageBytes(const std::span<const std::byte> srcBytes)
{
    MLG_CHECKV(m_StagingBuffer, "Invalid staging buffer");
    MLG_CHECKV(!srcBytes.empty(), "Source byte span is empty");

    const size_t expectedSize = m_Format.GetTotalSizeofMipData(m_Width, m_Height, m_MipCount);

    MLG_CHECKV(expectedSize == srcBytes.size(),
        "Source byte size does not match expected size: {} != {}",
        srcBytes.size(),
        expectedSize);

    size_t srcOffset = 0;
    for(uint32_t mip = 0; mip < m_MipCount; ++mip)
    {
        const uint32_t width = std::max(m_Width >> mip, 1u);
        const uint32_t height = std::max(m_Height >> mip, 1u);
        const size_t mipSize = m_Format.GetBlockAlignedSizeofData(width, height);
        MLG_CHECK(StageMipBytes(mip, srcBytes.subspan(srcOffset, mipSize)));
        srcOffset += mipSize;
    }
    return Result<>::Ok;
}

Result<>
GpuTextureStagingBuffer::StageMipBytes(const uint32_t mipLevel,
    const std::span<const std::byte> srcBytes)
{
    MLG_CHECKV(m_StagingBuffer, "Invalid staging buffer");
    MLG_CHECKV(mipLevel < m_MipCount, "Mip index out of range");

    const uint32_t width = std::max(m_Width >> mipLevel, 1u);
    const uint32_t height = std::max(m_Height >> mipLevel, 1u);
    MLG_CHECKV(srcBytes.size() == m_Format.GetBlockAlignedSizeofData(width, height),
        "Source mip byte size does not match expected size");

    const size_t dstOffset = m_Format.GetTotalSizeofMipDataWithPadding(m_Width,
        m_Height,
        mipLevel,
        GpuHelper::kTextureStrideAlignment);
    const size_t dstSize =
        m_Format.GetBlockAlignedSizeofDataWithPadding(width, height, GpuHelper::kTextureStrideAlignment);
    auto mappedRangeResult = GetMappedRange();
    MLG_CHECK(mappedRangeResult, "Failed to get mapped range");
    MLG_CHECKV(dstOffset <= mappedRangeResult->size()
            && dstSize <= mappedRangeResult->size() - dstOffset,
        "Mip exceeds mapped memory size");

    const std::span dstBytes = mappedRangeResult->subspan(dstOffset, dstSize);
    const size_t srcRowStride = m_Format.GetBlockAlignedBytesPerRow(width);
    const size_t dstRowStride =
        m_Format.GetBlockAlignedBytesPerRowWithPadding(width, GpuHelper::kTextureStrideAlignment);
    if(dstRowStride == srcRowStride)
    {
        ::memcpy(dstBytes.data(), srcBytes.data(), srcBytes.size());
    }
    else
    {
        const size_t numRows = m_Format.GetBlockRowCount(height);
        for(size_t row = 0; row < numRows; ++row)
        {
            ::memcpy(dstBytes.subspan(row * dstRowStride).data(),
                srcBytes.subspan(row * srcRowStride).data(),
                srcRowStride);
        }
    }
    return Result<>::Ok;
}

Result<wgpu::Texture>
GpuTextureStagingBuffer::Commit()
{
    const wgpu::CommandEncoder cmdEncoder = m_GpuHelper->GetDevice().CreateCommandEncoder();
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    auto commitResult = Commit(cmdEncoder);
    MLG_CHECK(commitResult, "Failed to commit texture staging buffer");

    const wgpu::CommandBuffer cmdBuffer = cmdEncoder.Finish();
    MLG_CHECK(cmdBuffer, "Failed to finish command buffer");

    m_GpuHelper->GetDevice().GetQueue().Submit(1, &cmdBuffer);

    return *commitResult;
}

Result<wgpu::Texture>
GpuTextureStagingBuffer::Commit(wgpu::CommandEncoder cmdEncoder)
{
    MLG_DEFER
    {
        m_StagingBuffer = nullptr;
        m_Width = 0;
        m_Height = 0;
        m_Format = TextureFormat::GetUnknown();
        m_MipCount = 1;
        m_MappedMemory = {};
    };

    MLG_CHECKV(m_StagingBuffer, "Invalid staging buffer");

    m_StagingBuffer.Unmap();

    MLG_CHECKV(!m_Format.IsUnknown(), "Unknown texture format: {}", m_Format.GetName());

    const wgpu::TextureDescriptor desc //
        {
            .label = m_Name.c_str(),
            .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
            .dimension = wgpu::TextureDimension::e2D,
            .size = //
            {
                .width = m_Width,
                .height = m_Height,
                .depthOrArrayLayers = 1,
            },
            .format = m_Format.GetWgpuFormat(),
            .mipLevelCount = m_MipCount,
            .sampleCount = 1,
        };

    const wgpu::Texture texture = m_GpuHelper->GetDevice().CreateTexture(&desc);
    MLG_CHECK(texture, "Failed to create texture");

    const uint32_t blockDimension = m_Format.GetBlockDimension();

    size_t offset = 0;
    uint32_t mipWidth = m_Width;
    uint32_t mipHeight = m_Height;

    for(uint32_t mip = 0; mip < m_MipCount; ++mip)
    {
        const size_t rowStride =
            m_Format.GetBlockAlignedBytesPerRowWithPadding(mipWidth, GpuHelper::kTextureStrideAlignment);

        const wgpu::TexelCopyBufferInfo copySrc = //
            {
                .layout = //
                {
                    .offset = offset,
                    .bytesPerRow = static_cast<uint32_t>(rowStride),
                    .rowsPerImage = mipHeight,
                },
                .buffer = m_StagingBuffer,
            };

        const wgpu::TexelCopyTextureInfo copyDst = //
            {
                .texture = texture,
                .mipLevel = mip,
                .origin{},
            };

        const uint32_t copyWidth =
            ((mipWidth + blockDimension - 1) / blockDimension) * blockDimension;
        const uint32_t copyHeight =
            ((mipHeight + blockDimension - 1) / blockDimension) * blockDimension;

        const wgpu::Extent3D copySize = //
            {
                .width = copyWidth,
                .height = copyHeight,
                .depthOrArrayLayers = 1,
            };

        cmdEncoder.CopyBufferToTexture(&copySrc, &copyDst, &copySize);

        offset +=
            m_Format.GetBlockAlignedSizeofDataWithPadding(mipWidth, mipHeight, GpuHelper::kTextureStrideAlignment);

        mipWidth = std::max(1u, mipWidth >> 1);
        mipHeight = std::max(1u, mipHeight >> 1);
    }

    return texture;
}