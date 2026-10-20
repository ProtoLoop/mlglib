#define MLG_LOGGER_NAME "TLOD"

#include "TextureLoader.h"

#include "Defer.h"
#include "FileFetcher.h"
#include "GpuHelper.h"
#include "Ktx2.h"
#include "ResourceBundle.h"
#include "System.h"
#include "ThreadPool.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <stb_image.h>
#include <string_view>
#include <webgpu/webgpu_cpp.h>

/// TextureLoader

Result<wgpu::Texture>
TextureLoader::LoadFromMemory(const GpuHelper& gpuHelper,
    const std::string_view name,
    const uint32_t width,
    const uint32_t height,
    const TextureFormat& format,
    const std::span<const std::byte> pixelBytes)
{
    constexpr uint32_t kMipCount = 1;

    auto texBufResult =
        gpuHelper.CreateTextureStagingBuffer(width, height, kMipCount, format, name);
    MLG_CHECK(texBufResult, "Failed to create texture staging buffer");

    MLG_CHECK(texBufResult->StageBytes(pixelBytes), "Failed to stage bytes");

    return texBufResult->Commit();
}

Result<std::unique_ptr<TextureLoader>>
TextureLoader::LoadFromFile(const GpuHelper& gpuHelper,
    FileFetcher& fileFetcher,
    ThreadPool& threadPool,
    const FilePath& path,
    wgpu::CommandEncoder& commandEncoder)
{
    std::unique_ptr<TextureLoader> loader(
        new TextureLoader(gpuHelper, fileFetcher, threadPool, path, commandEncoder));

    MLG_CHECK(loader->Start(), "Failed to start texture loader");

    return loader;
}

Result<std::unique_ptr<TextureLoader>>
TextureLoader::LoadFromResource(const GpuHelper& gpuHelper,
    FileFetcher& fileFetcher,
    ThreadPool& threadPool,
    const DirectoryPath& workingDirectory,
    const ResourceBundle& resourceBundle,
    const TextureResource& texRsrc,
    wgpu::CommandEncoder& commandEncoder)
{
    const std::string_view texturePath = resourceBundle.GetStringView(texRsrc.TexturePath);
    MLG_CHECK(!texturePath.empty(), "Texture path is empty");

    const auto relPathResult = RelativeFilePath::Create(texturePath);
    MLG_CHECK(relPathResult, "Failed to create relative file path");

    RelativeFilePath relPath = *relPathResult;

    if((texRsrc.HasBC7Encoding())
        && gpuHelper.HasTextureFormat(TextureFormat::FormatIdentifier::BC7))
    {
        const std::string_view bc7Extension = TextureFormat::GetBC7().GetExtension().AsStringView();
        const InplaceString<> ktx2Extension = InplaceString<>::Format("{}.{}", bc7Extension, "ktx2");
        const auto pathResult = relPath.ReplaceExtension(ktx2Extension);
        MLG_CHECK(pathResult, "Failed to replace extension for {} with {}", relPath, ktx2Extension);
        relPath = *pathResult;
    }

    const auto fullPathResult = workingDirectory.Join(relPath);
    MLG_CHECK(fullPathResult, "Failed to create full file path");

    return LoadFromFile(gpuHelper, fileFetcher, threadPool, *fullPathResult, commandEncoder);
}

TextureLoader::TextureLoader(const GpuHelper& gpuHelper,
    FileFetcher& fileFetcher,
    ThreadPool& threadPool,
    const FilePath& path,
    wgpu::CommandEncoder commandEncoder)
    : m_GpuHelper(&gpuHelper),
      m_FileFetcher(&fileFetcher),
      m_ThreadPool(&threadPool),
      m_Path(path),
      m_CommandEncoder(std::move(commandEncoder)),
      m_Stage(Stage::LoadFromFile)
{
}

Result<wgpu::Texture>
TextureLoader::Take()
{
    MLG_CHECKV(!IsRunning(), "Task is not complete");
    MLG_CHECK(Stage::Succeeded == m_Stage, "Task failed");
    MLG_CHECKV(m_Texture, "Texture is not valid");

    wgpu::Texture texture = m_Texture;
    m_Texture = nullptr; // Invalidate the texture so it can only be taken once

    return texture;
}

Result<>
TextureLoader::OnStart()
{
    return Result<>::Ok;
}

void
TextureLoader::OnUpdate()
{
    switch(m_Stage)
    {
        case Stage::None:
            MLG_ERROR("Task has not started");
            m_Stage = Stage::Failed;
            break;

        case Stage::LoadFromFile:
            MLG_DEBUG("Loading texture: {}...", m_Path->GetFilename());
            if(auto fetchResult = m_FileFetcher->Fetch(*m_Path); !fetchResult)
            {
                m_Stage = Stage::Failed;
            }
            else
            {
                m_FetchRequestId = *fetchResult;
                m_Stage = Stage::FetchingFile;
            }
            break;

        case Stage::FetchingFile:
            if(!m_FileFetcher->IsPending(m_FetchRequestId))
            {
                auto fetchedData = m_FileFetcher->Take(m_FetchRequestId);

                if(!fetchedData)
                {
                    MLG_ERROR("Failed to take fetched data");
                    m_Stage = Stage::Failed;
                }
                else
                {
                    m_FileBytes = std::move(*fetchedData);
                    if(!BeginDecode())
                    {
                        MLG_ERROR("Failed to decode texture");
                        m_Stage = Stage::Failed;
                    }
                    else
                    {
                        m_Stage = Stage::Decoding;
                    }
                }
            }
            break;

        case Stage::Decoding:
            if(m_DecodeCompletionFlag)
            {
                if(!m_DecodeResult)
                {
                    MLG_ERROR("Texture decoding failed");
                    m_Stage = Stage::Failed;
                }
                else if(!Commit())
                {
                    MLG_ERROR("Failed to commit texture");
                    m_Stage = Stage::Failed;
                }
                else
                {
                    m_Stage = Stage::Succeeded;
                }
            }
            break;

        case Stage::Succeeded:
            [[fallthrough]];
        case Stage::Failed:
            SetComplete();
            break;
    }
}

Result<>
TextureLoader::Commit()
{
    MLG_DEFER
    {
        m_StagingBuffer.reset();
        m_Width = 0;
        m_Height = 0;
        m_MipCount = 1;
    };

    MLG_CHECKV(m_StagingBuffer, "Invalid staging buffer");

    wgpu::CommandEncoder commandEncoder = m_CommandEncoder;
    if(!commandEncoder)
    {
        commandEncoder = m_GpuHelper->GetDevice().CreateCommandEncoder();
        MLG_CHECK(commandEncoder, "Failed to create command encoder");
    }

    auto commitResult = m_StagingBuffer->Commit(commandEncoder);
    MLG_CHECK(commitResult, "Failed to commit staging buffer");

    if(!m_CommandEncoder)
    {
        const wgpu::CommandBuffer commandBuffer = commandEncoder.Finish();
        m_GpuHelper->GetDevice().GetQueue().Submit(1, &commandBuffer);
    }

    m_Texture = *commitResult;

    return Result<>::Ok;
}

Result<>
TextureLoader::BeginDecode()
{
    MLG_LOG_SCOPE(m_Path->GetFilename());

    MLG_DEBUG("Decoding texture...");

    void (*decodeFunction)(void*) = nullptr;

    if(TextureHeader::IsMlgTexture(m_FileBytes))
    {
        const TextureHeader* header = TextureHeader::GetHeader(m_FileBytes);
        MLG_CHECK(header, "Failed to get texture header");

        MLG_CHECK(TextureHeader::ValidateFile(m_FileBytes));

        MLG_CHECK(header, "Failed to get texture header");

        MLG_DEBUG("Image info - {} x {} - {}",
            header->Width,
            header->Height,
            header->FourCC.AsStringView());

        m_Width = header->Width;
        m_Height = header->Height;
        m_MipCount = header->MipCount;
        m_Format = TextureFormat::GetFormat(header->FourCC);

        decodeFunction = DecodeMlg;
    }
    else if(Ktx2Header::IsKtx2(m_FileBytes))
    {
        const auto header = Ktx2Header::GetHeader(m_FileBytes);
        MLG_CHECK(header, "Invalid or unsupported KTX2 texture");
        m_Width = header->Width;
        m_Height = header->Height;
        m_MipCount = header->MipCount;
        m_Format = header->Format;
        decodeFunction = DecodeKtx2;
    }
    else
    {
        // stb takes image size as an int.  Make sure we don't overflow.
        MLG_CHECKV(m_FileBytes.size() <= std::numeric_limits<int>::max(),
            "Texture size exceeds maximum size for stb_image: {} > {}",
            m_FileBytes.size(),
            std::numeric_limits<int>::max());

        int width = 0, height = 0, numChannels = 0;

        const void* p = m_FileBytes.data();

        if(!stbi_info_from_memory(static_cast<const stbi_uc*>(p),
               static_cast<int>(m_FileBytes.size()),
               &width,
               &height,
               &numChannels))
        {
            MLG_ERROR("Error getting image info - {}", stbi_failure_reason());
            return Result<>::Fail;
        }

        MLG_DEBUG("Image info - {} x {} x {}", width, height, numChannels);

        m_Width = static_cast<uint32_t>(width);
        m_Height = static_cast<uint32_t>(height);
        m_MipCount = 1;
        m_Format = TextureFormat::GetRGBA();

        decodeFunction = DecodeStb;
    }

    auto texBufResult = m_GpuHelper->CreateTextureStagingBuffer(m_Width,
        m_Height,
        m_MipCount,
        m_Format,
        m_Path->GetFilename());

    MLG_CHECK(texBufResult, "Failed to create texture staging buffer");

    m_StagingBuffer.emplace(std::move(*texBufResult));

    MLG_CHECK(m_ThreadPool->Enqueue(decodeFunction, this), "Failed to enqueue texture decode task");

    return Result<>::Ok;
}

void
TextureLoader::DecodeKtx2(void* userData)
{
    TextureLoader* loader = static_cast<TextureLoader*>(userData);
    loader->m_DecodeResult = loader->DecodeKtx2();
    loader->m_DecodeCompletionFlag.store(true);
}

Result<>
TextureLoader::DecodeKtx2()
{
    MLG_LOG_SCOPE(m_Path->GetFilename());

    MLG_DEFER
    {
        std::vector<std::byte>().swap(m_FileBytes);
    };

    for(uint32_t mip = 0; mip < m_MipCount; ++mip)
    {
        const auto bytes = Ktx2Header::GetMipBytes(m_FileBytes, mip);
        MLG_CHECK(bytes, "Failed to get KTX2 mip bytes");
        MLG_CHECK(m_StagingBuffer->StageMipBytes(mip, *bytes), "Failed to stage KTX2 mip");
    }
    return Result<>::Ok;
}

void
TextureLoader::DecodeMlg(void* userData)
{
    TextureLoader* loader = static_cast<TextureLoader*>(userData);
    loader->m_DecodeResult = loader->DecodeMlg();
    loader->m_DecodeCompletionFlag.store(true);
}

void
TextureLoader::DecodeStb(void* userData)
{
    TextureLoader* loader = static_cast<TextureLoader*>(userData);
    loader->m_DecodeResult = loader->DecodeStb();
    loader->m_DecodeCompletionFlag.store(true);
}

Result<>
TextureLoader::DecodeMlg()
{
    MLG_LOG_SCOPE(m_Path->GetFilename());

    MLG_DEBUG("Decoding MLG texture...");

    MLG_DEFER
    {
        // Free the buffer to reclaim memory.
        std::vector<std::byte>().swap(m_FileBytes);
    };

    const TextureHeader* header = TextureHeader::GetHeader(m_FileBytes);
    MLG_CHECK(header, "Failed to get texture header");

    MLG_DEBUG("Image info - {} x {} - {}",
        header->Width,
        header->Height,
        header->FourCC.AsStringView());

    const Result<std::span<const std::byte>> pixelBytesResult =
        TextureHeader::GetPixelBytes(m_FileBytes);
    MLG_CHECK(pixelBytesResult, "Failed to get pixel bytes");

    MLG_CHECK(m_StagingBuffer->StageBytes(*pixelBytesResult), "Failed to stage bytes");

    return Result<>::Ok;
}

Result<>
TextureLoader::DecodeStb()
{
    MLG_LOG_SCOPE(m_Path->GetFilename());

    MLG_DEBUG("Decoding stb texture...");

    MLG_DEFER
    {
        // Free the buffer to reclaim memory.
        std::vector<std::byte>().swap(m_FileBytes);
    };

    int imgWidth = 0, imgHeight = 0, imgNumChannels = 0;

    const void* p = m_FileBytes.data();
    const stbi_uc* texBytes = static_cast<const stbi_uc*>(p);

    void* imgData = stbi_load_from_memory(texBytes,
        static_cast<int>(m_FileBytes.size()),
        &imgWidth,
        &imgHeight,
        &imgNumChannels,
        TextureFormat::kDefaultChannelCount);

    MLG_CHECK(imgData, "Failed to decode image - {}", stbi_failure_reason());

    MLG_DEFER
    {
        // Free the stb image buffer.
        stbi_image_free(imgData);
    };

    const size_t sizeofImgData = static_cast<size_t>(m_Width)
        * static_cast<size_t>(m_Height)
        * TextureFormat::kDefaultChannelCount;

    const std::span<const std::byte> pixelBytes =
        std::span<const std::byte>(static_cast<const std::byte*>(imgData), sizeofImgData);

    MLG_CHECK(m_StagingBuffer->StageBytes(pixelBytes), "Failed to stage bytes");

    return Result<>::Ok;
}