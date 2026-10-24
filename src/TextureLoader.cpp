#include <variant>
#define MLG_LOGGER_NAME "TLOD"

#include "Defer.h"
#include "FileFetcher.h"
#include "GpuHelper.h"
#include "ResourceBundle.h"
#include "System.h"
#include "TextureLoader.h"
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

    if((texRsrc.HasASTCEncoding())
        && gpuHelper.HasTextureFormat(TextureFormat::FormatIdentifier::ASTC))
    {
        const std::string_view astcExtension =
            TextureFormat::GetASTC().GetExtension().AsStringView();
        const InplaceString<> ktx2Extension =
            InplaceString<>::Format("{}.{}", astcExtension, "ktx2");
        const auto pathResult = relPath.ReplaceExtension(ktx2Extension);
        MLG_CHECK(pathResult, "Failed to replace extension for {} with {}", relPath, ktx2Extension);
        relPath = *pathResult;
    }
    else if((texRsrc.HasBC7Encoding())
        && gpuHelper.HasTextureFormat(TextureFormat::FormatIdentifier::BC7))
    {
        const std::string_view bc7Extension = TextureFormat::GetBC7().GetExtension().AsStringView();
        const InplaceString<> ktx2Extension =
            InplaceString<>::Format("{}.{}", bc7Extension, "ktx2");
        const auto pathResult = relPath.ReplaceExtension(ktx2Extension);
        MLG_CHECK(pathResult, "Failed to replace extension for {} with {}", relPath, ktx2Extension);
        relPath = *pathResult;
    }
    else if(texRsrc.HasRGBAEncoding())
    {
        const std::string_view rgbaExtension = TextureFormat::GetRGBA().GetExtension().AsStringView();
        const InplaceString<> ktx2Extension =
            InplaceString<>::Format("{}.{}", rgbaExtension, "ktx2");
        const auto pathResult = relPath.ReplaceExtension(ktx2Extension);
        MLG_CHECK(pathResult, "Failed to replace extension for {} with {}", relPath, ktx2Extension);
        relPath = *pathResult;
    }
    else
    {
        MLG_CHECK(texRsrc.Encodings == TextureResourceEncoding::None,
            "No supported cooked texture encoding for {}",
            texturePath);
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
            MLG_DEBUG("Loading texture: {}...", m_Path.GetFilename());
            if(auto fetchResult = m_FileFetcher->Fetch(m_Path); !fetchResult)
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
                else if(auto ktx2 = Ktx2Container::Create(std::move(*fetchedData)); ktx2)
                {
                    m_Container = std::move(*ktx2);
                }
                else if(auto stb = StbContainer::Create(std::move(*fetchedData)); stb)
                {
                    m_Container = std::move(*stb);
                }

                if(std::holds_alternative<std::monostate>(m_Container))
                {
                    MLG_ERROR("Unsupported texture format");
                    m_Stage = Stage::Failed;
                }
                else if(!BeginDecode())
                {
                    MLG_ERROR("Failed to decode texture");
                    m_Stage = Stage::Failed;
                }
                else
                {
                    m_Stage = Stage::Decoding;
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
        m_Container = std::monostate{};
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
    MLG_LOG_SCOPE(m_Path.GetFilename());

    MLG_DEBUG("Decoding texture...");

    uint32_t width = 0, height = 0, mipCount = 1;
    std::optional<TextureFormat> format;

    auto result = std::visit(
        [&](auto&& container) -> Result<>
        {
            using T = std::decay_t<decltype(container)>;
            
            if constexpr(std::is_same_v<T, std::monostate>)
            {
                MLG_ERROR("Texture container is empty");
                return Result<>::Fail;
            }
            else
            {
                width = container.GetWidth();
                height = container.GetHeight();
                mipCount = container.GetMipCount();
                format = container.GetFormat();
                return Result<>::Ok;
            }
        },
        m_Container);

    MLG_CHECK(result);

    auto texBufResult = m_GpuHelper->CreateTextureStagingBuffer(width,
        height,
        mipCount,
        *format,
        m_Path.GetFilename());

    MLG_CHECK(texBufResult, "Failed to create texture staging buffer");

    m_StagingBuffer.emplace(std::move(*texBufResult));

    MLG_CHECK(m_ThreadPool->Enqueue(Decode, this), "Failed to enqueue texture decode task");

    return Result<>::Ok;
}

void
TextureLoader::Decode(void* userData)
{
    TextureLoader* loader = static_cast<TextureLoader*>(userData);

    std::visit(
        [&](auto&& container)
        {
            using T = std::decay_t<decltype(container)>;

            if constexpr(std::is_same_v<T, std::monostate>)
            {
                MLG_ERROR("Texture container is empty");
                loader->m_DecodeResult = Result<>::Fail;
            }
            else
            {
                loader->m_DecodeResult = loader->Decode(container);
            }
        },
        loader->m_Container);

    // Free resources.
    loader->m_Container = std::monostate{};

    loader->m_DecodeCompletionFlag.store(true);
}

Result<>
TextureLoader::Decode(const Ktx2Container& ktx2)
{
    MLG_LOG_SCOPE(m_Path.GetFilename());

    for(uint32_t mip = 0; mip < ktx2.GetMipCount(); ++mip)
    {
        const auto bytes = ktx2.GetMipBytes(mip);
        MLG_CHECK(bytes, "Failed to get KTX2 mip bytes");
        MLG_CHECK(m_StagingBuffer->StageMipBytes(mip, *bytes), "Failed to stage KTX2 mip");
    }
    return Result<>::Ok;
}

Result<>
TextureLoader::Decode(const StbContainer& stb)
{
    MLG_LOG_SCOPE(m_Path.GetFilename());

    MLG_DEBUG("Decoding stb texture...");

    int imgWidth = 0, imgHeight = 0, imgNumChannels = 0;

    const void* p = stb.GetBytes().data();
    const stbi_uc* texBytes = static_cast<const stbi_uc*>(p);

    stbi_uc* imgData = stbi_load_from_memory(texBytes,
        static_cast<int>(stb.GetBytes().size()),
        &imgWidth,
        &imgHeight,
        &imgNumChannels,
        TextureFormat::kDefaultChannelCount);

    MLG_DEFER
    {
        // Free the stb image buffer.
        if(imgData)
        {
            stbi_image_free(imgData);
        }
    };

    MLG_CHECK(imgData, "Failed to decode image - {}", stbi_failure_reason());

    const size_t sizeofImgData = static_cast<size_t>(stb.GetWidth())
        * static_cast<size_t>(stb.GetHeight())
        * TextureFormat::kDefaultChannelCount;

    const std::span<const std::byte> pixelBytes = std::as_bytes(std::span(imgData, sizeofImgData));

    MLG_CHECK(m_StagingBuffer->StageBytes(pixelBytes), "Failed to stage bytes");

    return Result<>::Ok;
}

/// TextureLoader::StbContainer

Result<TextureLoader::StbContainer>
TextureLoader::StbContainer::Create(std::vector<std::byte>&& buffer)
{
    // stb takes image size as an int.  Make sure we don't overflow.
    MLG_CHECKV(buffer.size() <= std::numeric_limits<int>::max(),
        "Texture size exceeds maximum size for stb_image: {} > {}",
        buffer.size(),
        std::numeric_limits<int>::max());

    int width = 0, height = 0, numChannels = 0;
    const void* p = buffer.data();
    MLG_CHECK(stbi_info_from_memory(static_cast<const stbi_uc*>(p),
                  static_cast<int>(buffer.size()),
                  &width,
                  &height,
                  &numChannels),
        "Error getting image info - {}",
        stbi_failure_reason());

    return StbContainer(width, height, std::move(buffer));
}
