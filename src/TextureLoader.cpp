#define MLG_LOGGER_NAME "TLOD"

#include "TextureLoader.h"

#include "Defer.h"
#include "FileFetcher.h"
#include "GpuHelper.h"
#include "ResourceBundle.h"
#include "System.h"
#include "ThreadPool.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <stb_image.h>
#include <string_view>
#include <webgpu/webgpu_cpp.h>

namespace
{

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

/// Checks if the buffer contains the correct magic numbers for a texture header.
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

/// Checks if the buffer contains the correct version for a texture header.
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

/// Retrieves the texture header from the buffer.
/// Returns a pointer to the texture header if the buffer contains a valid header, or nullptr
/// otherwise. A valid header must have the correct magic numbers and version, and valid
/// dimensions.
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

    if(!TextureHeader::CheckDimensions(header->Width, header->Height))
    {
        return nullptr;
    }

    return header;
}
} // namespace

/// TextureHeader

/// Checks if the texture dimensions are within the allowed limits.
Result<>
TextureHeader::CheckDimensions(const uint32_t width, const uint32_t height)
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

Result<>
TextureHeader::ValidateFile(const std::span<const std::byte> fileBuffer)
{
    const TextureHeader* header = GetTextureHeader(fileBuffer);
    MLG_CHECK(header, "Invalid texture header");

    const TextureFormat& format = TextureFormat::GetFormat(header->FourCC);
    MLG_CHECK(!format.IsUnknown(), "Unknown texture format: {}", header->FourCC.AsStringView());

    const size_t expectedSize =
        format.GetSizeofData(header->Width, header->Height) + sizeof(TextureHeader);

    MLG_CHECK(fileBuffer.size() == expectedSize,
        "File buffer size does not match expected size: {} != {}",
        fileBuffer.size(),
        expectedSize);

    return Result<>::Ok;
}

/// TextureLoader

Result<wgpu::Texture>
TextureLoader::LoadFromMemory(const GpuHelper& gpuHelper,
    const std::string_view name,
    const uint32_t width,
    const uint32_t height,
    const TextureFormat& format,
    const std::span<const std::byte> pixelBytes)
{
    auto stagingBufferResult = gpuHelper.CreateTextureStagingBuffer(width, height, format, name);
    MLG_CHECK(stagingBufferResult, "Failed to create staging buffer");

    auto textureResult = gpuHelper.CreateTexture(width, height, format, name);
    MLG_CHECK(textureResult, "Failed to create texture");

    void* mapped = stagingBufferResult->GetMappedRange();
    MLG_CHECK(mapped);

    const std::span dstBytes(static_cast<std::byte*>(mapped), stagingBufferResult->GetSize());

    MLG_CHECK(CopyBytesToStagingBuffer(width, height, pixelBytes, dstBytes, format),
        "Failed to copy bytes to staging buffer");

    MLG_CHECK(gpuHelper.CommitTextureStagingBuffer(*textureResult, *stagingBufferResult),
        "Failed to commit staging buffer");
        
    return *textureResult;
}

Result<std::unique_ptr<TextureLoader>>
TextureLoader::LoadFromFile(const GpuHelper& gpuHelper,
    FileFetcher& fileFetcher,
    ThreadPool& threadPool,
    const FilePath& path,
    wgpu::CommandEncoder& commandEncoder)
{
    std::unique_ptr<TextureLoader> loader(new TextureLoader(gpuHelper, fileFetcher, threadPool, path, commandEncoder));

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
        const auto bc7PathResult =
            relPath.ReplaceExtension(TextureFormat::GetBC7().GetExtension().AsStringView());
        MLG_CHECK(bc7PathResult, "Failed to replace extension for {}", relPath);
        relPath = *bc7PathResult;
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
            MLG_DEBUG("Loading texture: {}...", m_Path->GetStem());
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
TextureLoader::CopyBytesToStagingBuffer(const uint32_t width,
    const uint32_t height,
    const std::span<const std::byte> srcBytes,
    std::span<std::byte> dstBytes,
    const TextureFormat& format)
{
    MLG_CHECKV(!srcBytes.empty(), "Source byte span is empty");

    const size_t expectedSize = format.GetSizeofData(width, height);

    MLG_CHECK(expectedSize == srcBytes.size(),
        "Source byte size does not match expected size: {} != {}",
        srcBytes.size(),
        expectedSize);

    MLG_CHECKV(srcBytes.size() <= dstBytes.size(),
        "Source byte size exceeds mapped memory size: {} > {}",
        srcBytes.size(),
        dstBytes.size());

    const size_t srcRowStride = format.GetBytesPerRow(width);

    // Bytes must be padded to the aligned row stride
    const size_t dstRowStride =
        format.GetPaddedBytesPerRow(width, GpuHelper::kTextureStrideAlignment);

    if(dstRowStride == srcRowStride)
    {
        ::memcpy(dstBytes.data(), srcBytes.data(), srcBytes.size());
    }
    else
    {
        size_t dstOffset = 0, srcOffset = 0;
        const size_t numRows = format.GetBlockRowCount(height);

        for(uint32_t y = 0; y < numRows; ++y, dstOffset += dstRowStride, srcOffset += srcRowStride)
        {
            ::memcpy(&dstBytes[dstOffset], &srcBytes[srcOffset], srcRowStride);
        }
    }

    return Result<>::Ok;
}

Result<>
TextureLoader::CopyBytesToStagingBuffer(const std::span<const std::byte> srcBytes)
{
    return CopyBytesToStagingBuffer(m_Width,
        m_Height,
        srcBytes,
        m_MappedMemory,
        m_Format);
}

Result<>
TextureLoader::Commit()
{
    MLG_DEFER
    {
        m_StagingBuffer = nullptr;
        m_MappedMemory = {};
        m_Width = 0;
        m_Height = 0;
    };

    MLG_CHECKV(m_StagingBuffer, "Invalid staging buffer");

    wgpu::CommandEncoder commandEncoder = m_CommandEncoder;
    if(!commandEncoder)
    {
        commandEncoder = m_GpuHelper->GetDevice().CreateCommandEncoder();
        MLG_CHECK(commandEncoder, "Failed to create command encoder");
    }

    auto textureResult = m_GpuHelper->CreateTexture(m_Width, m_Height, m_Format, m_Path->GetStem());
    MLG_CHECK(textureResult);

    MLG_CHECK(
        m_GpuHelper->CommitTextureStagingBuffer(*textureResult, m_StagingBuffer, commandEncoder),
        "Failed to commit staging buffer");

    if(!m_CommandEncoder)
    {
        const wgpu::CommandBuffer commandBuffer = commandEncoder.Finish();
        m_GpuHelper->GetDevice().GetQueue().Submit(1, &commandBuffer);
    }

    m_Texture = std::move(*textureResult);

    return Result<>::Ok;
}

Result<>
TextureLoader::BeginDecode()
{
    MLG_DEBUG("Decoding texture: {}...", m_Path->GetStem());

    void (*decodeFunction)(void*) = nullptr;

    if(CheckHeaderMagic(m_FileBytes) && CheckHeaderVersion(m_FileBytes))
    {
        MLG_CHECK(TextureHeader::ValidateFile(m_FileBytes));

        const TextureHeader* header = GetTextureHeader(m_FileBytes);
        MLG_CHECK(header, "Failed to get texture header");

        MLG_DEBUG("Image info - {} x {} - {}",
            header->Width,
            header->Height,
            header->FourCC.AsStringView());

        m_Width = header->Width;
        m_Height = header->Height;
        m_Format = TextureFormat::GetFormat(header->FourCC);

        decodeFunction = DecodeMlg;
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
        m_Format = TextureFormat::GetRGBA();

        decodeFunction = DecodeStb;
    }

    auto stagingBufferResult =
        m_GpuHelper->CreateTextureStagingBuffer(m_Width, m_Height, m_Format, m_Path->GetStem());
    MLG_CHECK(stagingBufferResult);

    void* mapped = stagingBufferResult->GetMappedRange();
    MLG_CHECK(mapped);

    m_StagingBuffer = std::move(*stagingBufferResult);
    m_MappedMemory = std::span<std::byte>(static_cast<std::byte*>(mapped), m_StagingBuffer.GetSize());

    MLG_CHECK(m_ThreadPool->Enqueue(decodeFunction, this), "Failed to enqueue texture decode task");

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
    MLG_LOG_SCOPE(m_Path->GetStem());

    MLG_DEBUG("Decoding MLG texture: {}...", m_Path->GetStem());

    MLG_DEFER
    {
        // Free the buffer to reclaim memory.
        std::vector<std::byte>().swap(m_FileBytes);
    };

    const TextureHeader* header = GetTextureHeader(m_FileBytes);
    MLG_CHECK(header, "Failed to get texture header");

    MLG_DEBUG("Image info - {} x {} - {}",
        header->Width,
        header->Height,
        header->FourCC.AsStringView());

    const std::span<const std::byte> pixelBytes =
        std::span(m_FileBytes).subspan(sizeof(TextureHeader));

    MLG_CHECK(CopyBytesToStagingBuffer(pixelBytes), "Failed to copy bytes to staging buffer");

    return Result<>::Ok;
}

Result<>
TextureLoader::DecodeStb()
{
    MLG_LOG_SCOPE(m_Path->GetStem());

    MLG_DEBUG("Decoding stb texture: {}...", m_Path->GetStem());

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
        GpuHelper::kNumTextureChannels);

    MLG_CHECK(imgData, "Failed to decode image - {}", stbi_failure_reason());

    MLG_DEFER
    {
        // Free the stb image buffer.
        stbi_image_free(imgData);
    };

    const size_t sizeofImgData = static_cast<size_t>(m_Width)
        * static_cast<size_t>(m_Height)
        * GpuHelper::kNumTextureChannels;
        
    const std::span<const std::byte> pixelBytes =
        std::span<const std::byte>(static_cast<const std::byte*>(imgData), sizeofImgData);

    MLG_CHECK(CopyBytesToStagingBuffer(pixelBytes), "Failed to copy bytes to staging buffer");

    return Result<>::Ok;
}