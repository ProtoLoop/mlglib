#include "TextureHelper.h"

#include "Defer.h"
#include "FileFetcher.h"
#include "GpuHelper.h"
#include "System.h"
#include "ThreadPool.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <stb_image.h>
#include <webgpu/webgpu_cpp.h>

namespace
{
using namespace TextureHelper;

static_assert(TextureHeader::kMaxImageWidth > 0, "Max image width must be greater than zero");
static_assert(TextureHeader::kMaxImageHeight > 0, "Max image height must be greater than zero");
static_assert(TextureHeader::kMaxImageWidth <= std::numeric_limits<uint32_t>::max(),
    "Max image width exceeds the maximum value for the type");
static_assert(TextureHeader::kMaxImageHeight <= std::numeric_limits<uint32_t>::max(),
    "Max image height exceeds the maximum value for the type");
static_assert(std::numeric_limits<uint32_t>::max() / TextureHeader::kMaxImageWidth
        >= GpuHelper::kNumTextureChannels,
    "Max image width multiplied by the number of texture channels exceeds the maximum value for the type");

constexpr uint32_t kMaxByteWidth = TextureHeader::kMaxImageWidth * GpuHelper::kNumTextureChannels;

static_assert(std::numeric_limits<uint32_t>::max() / kMaxByteWidth
        >= TextureHeader::kMaxImageHeight,
    "Max image byte width multiplied by the max image height exceeds the maximum value for the type");
} // namespace

namespace TextureHelper
{

Result<>
ValidateDimensions(const uint32_t width, const uint32_t height)
{
    MLG_CHECK(width > 0 && height > 0, "Image width and height must be greater than zero");

    MLG_CHECK(width <= TextureHeader::kMaxImageWidth,
        "Image width exceeds maximum width: {} > {}",
        width,
        TextureHeader::kMaxImageWidth);

    MLG_CHECK(height <= TextureHeader::kMaxImageHeight,
        "Image height exceeds maximum height: {} > {}",
        height,
        TextureHeader::kMaxImageHeight);

    return Result<>::Ok;
}

bool
CheckHeaderMagic(const std::span<const std::byte> buffer)
{
    if(buffer.size() < sizeof(TextureHeader))
    {
        return false;
    }

    const void* p = buffer.data();
    const TextureHeader* header = static_cast<const TextureHeader*>(p);
    return header->Magic0 == TextureHeader::kMagic0 && header->Magic1 == TextureHeader::kMagic1;
}

bool
CheckHeaderVersion(const std::span<const std::byte> buffer)
{
    if(buffer.size() < sizeof(TextureHeader))
    {
        return false;
    }

    const void* p = buffer.data();
    const TextureHeader* header = static_cast<const TextureHeader*>(p);
    return header->Version == TextureHeader::kVersion;
}

const TextureHeader*
GetTextureHeader(const std::span<const std::byte> buffer)
{
    if(!CheckHeaderMagic(buffer))
    {
        MLG_ERROR("Invalid texture header: Magic numbers do not match");
        return nullptr;
    }

    if(!CheckHeaderVersion(buffer))
    {
        MLG_ERROR("Invalid texture header: Version does not match");
        return nullptr;
    }

    const void* p = buffer.data();
    const TextureHeader* header = static_cast<const TextureHeader*>(p);

    if(!ValidateDimensions(header->Width, header->Height))
    {
        return nullptr;
    }

    return header;
}

/// TextureHeader

Result<>
TextureHeader::Validate(const std::span<const std::byte> buffer)
{
    const TextureHeader* header = GetTextureHeader(buffer);
    MLG_CHECK(header, "Invalid texture header");

    const size_t rowStride = static_cast<size_t>(header->Width) * GpuHelper::kNumTextureChannels;
    const size_t expectedSize =
        (rowStride * static_cast<size_t>(header->Height)) + sizeof(TextureHeader);

    MLG_CHECK(buffer.size() == expectedSize,
        "Buffer size does not match expected size: {} != {}",
        buffer.size(),
        expectedSize);

    return Result<>::Ok;
}

/// TextureBuilder

TextureBuilder::TextureBuilder(const GpuHelper& gpuHelper,
    const FilePath& path,
    wgpu::Buffer stagingBuffer,
    std::span<std::byte> mappedMemory,
    const uint32_t width,
    const uint32_t height)
    : m_GpuHelper(&gpuHelper),
      m_Path(path),
      m_StagingBuffer(std::move(stagingBuffer)),
      m_MappedMemory(mappedMemory),
      m_Width(width),
      m_Height(height)
{
}

Result<TextureBuilder>
TextureBuilder::Create(
    const GpuHelper& gpuHelper, const FilePath& path, const uint32_t width, const uint32_t height)
{
    MLG_CHECK(ValidateDimensions(width, height));

    auto stagingBuffer = gpuHelper.CreateStagingBuffer(width, height, path.GetStem());
    MLG_CHECK(stagingBuffer);
    MLG_CHECK(stagingBuffer->GetSize() > 0, "Staging buffer has zero size");
    MLG_CHECK(stagingBuffer->GetSize() <= std::numeric_limits<size_t>::max(),
        "Staging buffer size exceeds maximum allowed size");

    void* mapped = stagingBuffer->GetMappedRange();
    MLG_CHECK(mapped);

    const std::span<std::byte> mappedSpan(static_cast<std::byte*>(mapped),
        static_cast<size_t>(stagingBuffer->GetSize()));

    TextureBuilder texBuilder(gpuHelper,
        path,
        std::move(*stagingBuffer),
        mappedSpan,
        width,
        height);

    return std::move(texBuilder);
}

Result<>
TextureBuilder::StagePixels(const std::span<const std::byte> srcBytes)
{
    MLG_CHECKV(!srcBytes.empty(), "Source byte span is empty");
    MLG_CHECKV(m_StagingBuffer, "Staging buffer is not valid - was Commit() already called?");

    const size_t expectedSrcSize = static_cast<size_t>(m_Width)
        * static_cast<size_t>(m_Height)
        * GpuHelper::kNumTextureChannels;

    MLG_CHECKV(srcBytes.size() == expectedSrcSize,
        "Source byte size does not match expected size: {} != {}",
        srcBytes.size(),
        expectedSrcSize);

    MLG_CHECKV(srcBytes.size() <= m_MappedMemory.size(),
        "Source byte size exceeds mapped memory size: {} > {}",
        srcBytes.size(),
        m_MappedMemory.size());

    const size_t srcRowStride = static_cast<size_t>(m_Width) * GpuHelper::kNumTextureChannels;
    const size_t dstRowStride = GpuHelper::GetTextureAlignedRowStride(static_cast<size_t>(m_Width));

    if(dstRowStride == srcRowStride)
    {
        ::memcpy(m_MappedMemory.data(), srcBytes.data(), srcBytes.size());
    }
    else
    {
        size_t dstOffset = 0, srcOffset = 0;

        for(uint32_t y = 0; y < m_Height; ++y, dstOffset += dstRowStride, srcOffset += srcRowStride)
        {
            ::memcpy(&m_MappedMemory[dstOffset], &srcBytes[srcOffset], srcRowStride);
        }
    }

    return Result<>::Ok;
}
Result<wgpu::Texture>
TextureBuilder::Commit(const wgpu::CommandEncoder& commandEncoder)
{
    MLG_CHECKV(m_StagingBuffer, "Staging buffer already committed");

    auto textureResult = m_GpuHelper->CreateTexture(m_Width, m_Height, m_Path.GetStem());
    MLG_CHECK(textureResult);

    MLG_CHECK(textureResult->GetFormat() == GpuHelper::kTextureFormat,
        "GPU texture format does not match expected format");

    MLG_DEFER
    {
        m_StagingBuffer = nullptr;
        m_MappedMemory = {};
        m_Width = 0;
        m_Height = 0;
    };

    MLG_CHECK(m_GpuHelper->CommitStagingBuffer(*textureResult, m_StagingBuffer, commandEncoder),
        "Failed to commit staging buffer");

    return *textureResult;
}

} // namespace TextureHelper

/// TextureDecodeTask

TextureDecodeTask::TextureDecodeTask(const GpuHelper& gpuHelper,
    std::vector<std::byte>&& rawData,
    const FilePath& path,
    ThreadPool& threadPool,
    wgpu::CommandEncoder commandEncoder)
    : m_GpuHelper(&gpuHelper),
      m_ThreadPool(&threadPool),
      m_Path(path),
      m_TexBytes(std::move(rawData)),
      m_CommandEncoder(std::move(commandEncoder))
{
}

Result<wgpu::Texture>
TextureDecodeTask::Take()
{
    MLG_CHECKV(!IsRunning(), "Task is not complete");
    MLG_CHECK(Stage::Succeeded == m_Stage, "Task failed");
    MLG_CHECKV(m_Texture, "Texture is not valid");

    wgpu::Texture texture = m_Texture;
    m_Texture = nullptr; // Invalidate the texture so it can only be taken once

    return texture;
}

// private:

Result<>
TextureDecodeTask::OnStart()
{
    MLG_CHECKV(Stage::None == m_Stage, "Task already started");

    m_Stage = Stage::Failed; // Set to failed in case of early exit

    // Default texture in case decoding fails
    m_Texture = m_GpuHelper->GetDefaultTexture();
    MLG_CHECK(m_Texture, "Failed to get default texture");

    auto decodeResult = BeginDecode();
    MLG_CHECK(decodeResult, "Failed to begin decode");

    m_Stage = Stage::Decoding;

    return Result<>::Ok;
}

void
TextureDecodeTask::OnUpdate()
{
    MLG_LOG_SCOPE(m_Path.GetStem());

    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::Decoding:
            if(m_CompletionFlag.load(std::memory_order_acquire))
            {
                if(!m_DecodeResult)
                {
                    MLG_ERROR("Failed to decode texture");
                    m_Stage = Stage::Failed;
                }
                else if(const auto textureResult = m_TextureBuilder->Commit(m_CommandEncoder);
                    !textureResult)
                {
                    MLG_ERROR("Failed to commit staging buffer");
                    m_Stage = Stage::Failed;
                }
                else
                {
                    MLG_DEBUG("Loaded");
                    m_Texture = std::move(*textureResult);
                    m_Stage = Stage::Succeeded;
                }
            }
            break;

        case Stage::Failed:
            MLG_ERROR("Task failed");
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            m_CompletionFlag.store(true, std::memory_order_release);
            break;
    }
}

Result<>
TextureDecodeTask::BeginDecode()
{
    MLG_DEBUG("Beginning texture decode...");

    if(CheckHeaderMagic(m_TexBytes) && CheckHeaderVersion(m_TexBytes))
    {
        MLG_CHECK(BeginDecodeMlg(), "Failed to begin mlg decode");
    }
    else
    {
        MLG_CHECK(BeginDecodeStb(), "Failed to begin stb decode");
    }

    return Result<>::Ok;
}

Result<>
TextureDecodeTask::BeginDecodeMlg()
{
    MLG_DEBUG("Begin decoding mlg texture...");

    MLG_CHECK(TextureHeader::Validate(m_TexBytes));

    const TextureHeader* header = TextureHelper::GetTextureHeader(m_TexBytes);
    MLG_CHECK(header, "Failed to get texture header");

    MLG_DEBUG("Image info - {} x {} x {}",
        header->Width,
        header->Height,
        GpuHelper::kNumTextureChannels);

    auto builderResult =
        TextureHelper::TextureBuilder::Create(*m_GpuHelper, m_Path, header->Width, header->Height);
    MLG_CHECK(builderResult);
    m_TextureBuilder = std::move(*builderResult);

    MLG_CHECK(m_ThreadPool->Enqueue(DecodeMlg, this), "Failed to enqueue texture decode task");

    return Result<>::Ok;
}

Result<>
TextureDecodeTask::BeginDecodeStb()
{
    MLG_DEBUG("Begin decoding stb texture...");

    // stb takes image size as an int.  Make sure we don't overflow.
    MLG_CHECKV(m_TexBytes.size() <= std::numeric_limits<int>::max(),
        "Texture size exceeds maximum size for stb_image: {} > {}",
        m_TexBytes.size(),
        std::numeric_limits<int>::max());

    int width = 0, height = 0, numChannels = 0;

    const void* p = m_TexBytes.data();
    const stbi_uc* texBytes = static_cast<const stbi_uc*>(p);

    if(!stbi_info_from_memory(texBytes,
           static_cast<int>(m_TexBytes.size()),
           &width,
           &height,
           &numChannels))
    {
        MLG_ERROR("Error getting image info - {}", stbi_failure_reason());
        return Result<>::Fail;
    }

    MLG_DEBUG("Image info - {} x {} x {}", width, height, numChannels);

    const uint32_t uWidth = static_cast<uint32_t>(width);
    const uint32_t uHeight = static_cast<uint32_t>(height);

    auto builderResult =
        TextureHelper::TextureBuilder::Create(*m_GpuHelper, m_Path, uWidth, uHeight);
    MLG_CHECK(builderResult);
    m_TextureBuilder = std::move(*builderResult);

    MLG_CHECK(m_ThreadPool->Enqueue(DecodeStb, this), "Failed to enqueue texture decode task");

    return Result<>::Ok;
}

Result<>
TextureDecodeTask::DecodeMlg()
{
    MLG_LOG_SCOPE(m_Path.GetStem());

    MLG_DEBUG("Decoding MLG texture...");

    MLG_DEFER
    {
        // Free the buffer to reclaim memory.
        std::vector<std::byte>().swap(m_TexBytes);
    };

    const std::span srcSpan = std::span(m_TexBytes).subspan(sizeof(TextureHeader));

    MLG_CHECK(m_TextureBuilder->StagePixels(srcSpan));

    return Result<>::Ok;
}

Result<>
TextureDecodeTask::DecodeStb()
{
    MLG_LOG_SCOPE(m_Path.GetStem());

    MLG_DEBUG("Decoding stb texture...");

    MLG_DEFER
    {
        // Free the buffer to reclaim memory.
        std::vector<std::byte>().swap(m_TexBytes);
    };

    int imgWidth = 0, imgHeight = 0, imgNumChannels = 0;

    const void* p = m_TexBytes.data();
    const stbi_uc* texBytes = static_cast<const stbi_uc*>(p);

    stbi_uc* data = stbi_load_from_memory(texBytes,
        static_cast<int>(m_TexBytes.size()),
        &imgWidth,
        &imgHeight,
        &imgNumChannels,
        GpuHelper::kNumTextureChannels);

    MLG_CHECK(data, "Failed to decode image - {}", stbi_failure_reason());

    // Eager reclaiming of memory.  The deferred recalaim from above will
    // still run but it will be a no-op.
    std::vector<std::byte>().swap(m_TexBytes);

    MLG_DEFER
    {
        // Free the stb image buffer.
        stbi_image_free(data);
    };

    const uint32_t uWidth = static_cast<uint32_t>(imgWidth);
    const uint32_t uHeight = static_cast<uint32_t>(imgHeight);

    MLG_CHECKV(uWidth == m_TextureBuilder->GetWidth(),
        "Decoded image width does not match staging texture width: {} != {}",
        uWidth,
        m_TextureBuilder->GetWidth());
    MLG_CHECKV(uHeight == m_TextureBuilder->GetHeight(),
        "Decoded image height does not match staging texture height: {} != {}",
        uHeight,
        m_TextureBuilder->GetHeight());

    const size_t sizeofSrcData = static_cast<size_t>(imgWidth)
        * static_cast<size_t>(imgHeight)
        * GpuHelper::kNumTextureChannels;

    p = static_cast<const void*>(data);
    const std::span<const std::byte> srcSpan(static_cast<const std::byte*>(p), sizeofSrcData);

    MLG_CHECK(m_TextureBuilder->StagePixels(srcSpan));

    return Result<>::Ok;
}

void
TextureDecodeTask::DecodeMlg(void* userData)
{
    TextureDecodeTask* task = static_cast<TextureDecodeTask*>(userData);
    task->m_DecodeResult = task->DecodeMlg();
    task->m_CompletionFlag.store(true, std::memory_order_release);
}
void
TextureDecodeTask::DecodeStb(void* userData)
{
    TextureDecodeTask* task = static_cast<TextureDecodeTask*>(userData);
    task->m_DecodeResult = task->DecodeStb();
    task->m_CompletionFlag.store(true, std::memory_order_release);
}

/// TextureFetchTask

TextureFetchTask::TextureFetchTask(const GpuHelper& gpuHelper,
    FileFetcher& fileFetcher,
    ThreadPool& threadPool,
    const FilePath& path,
    wgpu::CommandEncoder commandEncoder)
    : m_GpuHelper(&gpuHelper),
      m_FileFetcher(&fileFetcher),
      m_ThreadPool(&threadPool),
      m_Path(path),
      m_CommandEncoder(std::move(commandEncoder))
{
}

Result<wgpu::Texture>
TextureFetchTask::Take()
{
    MLG_CHECKV(!IsRunning(), "Task is not complete");
    MLG_CHECK(Stage::Succeeded == m_Stage, "Task failed");
    MLG_CHECKV(m_Texture, "Texture is not valid");

    wgpu::Texture texture = m_Texture;
    m_Texture = nullptr; // Invalidate the texture so it can only be taken once

    return texture;
}

// private:

Result<>
TextureFetchTask::OnStart()
{
    MLG_CHECKV(Stage::None == m_Stage, "Task already started");

    m_Stage = Stage::Failed; // Set to failed in case of early exit

    // If loading fails then we use the default texture.
    m_Texture = m_GpuHelper->GetDefaultTexture();
    MLG_CHECK(m_Texture, "Failed to get default texture");

    auto fetchRequestId = m_FileFetcher->Fetch(m_Path);
    MLG_CHECK(fetchRequestId);

    m_FetchRequestId = *fetchRequestId;

    m_Stage = Stage::Fetching;

    return Result<>::Ok;
}

void
TextureFetchTask::OnUpdate()
{
    MLG_LOG_SCOPE(m_Path.GetStem());

    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::Fetching:
            if(!m_FileFetcher->IsPending(m_FetchRequestId))
            {
                auto fetchedData = m_FileFetcher->Take(m_FetchRequestId);

                if(!fetchedData)
                {
                    MLG_ERROR("Failed to take fetched data");
                    m_Stage = Stage::Failed;
                }
                else if(m_TextureDecodeTask.emplace(*m_GpuHelper,
                            std::move(*fetchedData),
                            m_Path,
                            *m_ThreadPool,
                            m_CommandEncoder);
                    !m_TextureDecodeTask->Start())
                {
                    MLG_ERROR("Failed to start texture decode task");
                    m_Stage = Stage::Failed;
                }
                else
                {
                    m_Stage = Stage::Decoding;
                }
            }
            break;
        case Stage::Decoding:
            if(m_TextureDecodeTask->IsRunning())
            {
                m_TextureDecodeTask->Update();
            }
            else
            {
                if(auto textureResult = m_TextureDecodeTask->Take(); !textureResult)
                {
                    MLG_ERROR("Failed to take decoded texture");
                    m_Stage = Stage::Failed;
                }
                else
                {
                    MLG_DEBUG("Loaded");
                    m_Texture = *textureResult;
                    m_Stage = Stage::Succeeded;
                }

                // Reclaim resources.
                m_TextureDecodeTask.reset();
            }
            break;

        case Stage::Failed:
            MLG_ERROR("Task failed");
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            break;
    }
}