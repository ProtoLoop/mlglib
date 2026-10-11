
#include "Defer.h"
#include "GltfLoader.h"
#include "GpuHelper.h"
#include "Log.h"
#include "ResourceBundle.h"
#include "Result.h"
#include "Scene.h"
#include "SceneTypes.h"
#include "TextureHelper.h"
#include "ThreadPool.h"
#include "Timer.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <ktx.h>
#include <latch>
#include <limits>
#include <memory>
#include <span>
#include <stb_image.h>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace
{

/// Texture row alignment in bytes.
constexpr uint32_t kTextureRowAlignment = 256;

static_assert(std::is_trivially_copyable_v<TextureHelper::TextureHeader>);
static_assert(std::is_standard_layout_v<TextureHelper::TextureHeader>);
static_assert(sizeof(TextureHelper::TextureHeader) == 20); // NOLINT(readability-magic-numbers)

static_assert(GpuHelper::kNumTextureChannels == 4);
// Ensures that the texture row alignment is a power of two.
static_assert(std::has_single_bit(kTextureRowAlignment));
static_assert(kTextureRowAlignment % GpuHelper::kNumTextureChannels == 0);

struct KtxTextureDeleter
{
    void operator()(ktxTexture2* texture) const noexcept
    {
        if(texture != nullptr)
        {
            ktxTexture_Destroy(ktxTexture(texture));
        }
    }
};

struct StbImageDeleter
{
    void operator()(stbi_uc* pixels) const noexcept
    {
        if(pixels != nullptr)
        {
            stbi_image_free(pixels);
        }
    }
};

using KtxTexturePtr = std::unique_ptr<ktxTexture2, KtxTextureDeleter>;
using StbDataPtr = std::unique_ptr<stbi_uc, StbImageDeleter>;

struct RawImageData
{
    RawImageData(
        const uint32_t width, const uint32_t height, const uint32_t channelCount, stbi_uc* pixels)
        : Width(width),
          Height(height),
          ChannelCount(channelCount),
          StbData(pixels)
    {
    }

    std::span<const std::byte> GetBytes() const noexcept
    {
        const void* p = static_cast<void*>(StbData.get());
        const size_t sizeofData = static_cast<size_t>(Width) * static_cast<size_t>(Height) * static_cast<size_t>(ChannelCount);
        return std::span<const std::byte>(static_cast<const std::byte*>(p), sizeofData);
    }

    uint32_t Width{ 0 };
    uint32_t Height{ 0 };
    uint32_t ChannelCount{ 0 };
    StbDataPtr StbData;
};

/// Command line arguments for the cook tool.
struct CmdLinArgs
{
    const char* InputFile{ nullptr };
    const char* OutputDir{ nullptr };

    bool IsValid() const { return InputFile != nullptr && OutputDir != nullptr; }
};

/// Input and output directories for the cook tool.
struct CookDirs
{
    std::filesystem::path InputDir;
    std::filesystem::path OutputDir;

    /// Creates a CookDirs instance from command line arguments.
    static Result<CookDirs> FromArgs(const CmdLinArgs& args);
};

/// Parameters for a worker that cooks a texture.
struct CookTextureWorkerParams
{
    const CookDirs* CookDirs;
    SceneDefs::FilePathString InTexturePath;
    SceneDefs::FilePathString OutTexturePath;
    std::latch* Latch{ nullptr };
    Result<> Result;
};

void
Usage()
{
    std::cout << "Usage: cook -i <input file> -o <output directory>" << std::endl;
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
CmdLinArgs
ParseArgs(int argc, char** argv)
{
    CmdLinArgs args;
    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-i") == 0 && i + 1 < argc)
        {
            args.InputFile = argv[++i];
        }
        else if(strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        {
            args.OutputDir = argv[++i];
        }
    }
    return args;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

Result<CookDirs>
CookDirs::FromArgs(const CmdLinArgs& args)
{
    std::filesystem::path inDir = args.InputFile;
    inDir = inDir.parent_path();

    if(inDir.empty())
    {
        std::error_code ec;
        inDir = std::filesystem::current_path(ec);
        MLG_CHECKV(!ec, "Failed to get current working directory: {}", ec.message());
    }

    const std::filesystem::path outDir = args.OutputDir;

    MLG_CHECKV(!outDir.empty(), "Output directory is not specified");

    std::error_code ec;
    std::filesystem::create_directory(outDir, ec);

    MLG_CHECKV(!ec, "Failed to create output directory: {}", ec.message());

    ec.clear();

    const std::filesystem::file_status outDirStatus = std::filesystem::status(outDir, ec);

    MLG_CHECKV(!ec, "Failed to get status of output directory: {}", ec.message());

    ec.clear();

    MLG_CHECKV(std::filesystem::exists(outDirStatus),
        "Output directory does not exist: {}",
        outDir.string());

    MLG_CHECKV(std::filesystem::is_directory(outDirStatus),
        "Output path is not a directory: {}",
        outDir.string());

    return CookDirs //
        {
            .InputDir = inDir,
            .OutputDir = outDir,
        };
}

/// Aligns the given value up to the specified alignment.
[[maybe_unused]]
uint32_t
AlignUp(uint32_t value, uint32_t alignment)
{
    MLG_ABORTIF(alignment == 0, "Alignment must be non-zero");

    constexpr uint32_t kMaxValue = std::numeric_limits<uint32_t>::max();

    MLG_ABORTIF(value > kMaxValue - (alignment - 1), "Value overflow on alignment");

    return (value + (alignment - 1)) & ~(alignment - 1);
}

Result<std::unordered_map<SceneDefs::FilePathString, SceneDefs::FilePathString>>
ReplaceTexturePaths(std::span<ModelDef> modelDefs)
{
    std::unordered_map<SceneDefs::FilePathString, SceneDefs::FilePathString> texPathMap;
    for(ModelDef& modelDef : modelDefs)
    {
        for(MeshDef& meshDef : modelDef.MeshDefs)
        {
            SceneDefs::FilePathString& texPath = meshDef.MaterialDef.BaseTexturePath;

            if(!texPath.empty())
            {
                if(!texPathMap.contains(texPath))
                {
                    std::filesystem::path texPathFsPath = std::string_view(texPath);

                    const SceneDefs::FilePathString newPath(
                        texPathFsPath.replace_extension(".ctex").string());

                    texPathMap.emplace(texPath, newPath);
                }

                texPath = texPathMap.at(texPath);
            }
        }
    }

    return texPathMap;
}

using ByteSpan = std::span<const std::byte>;

Result<>
WriteFile(const std::filesystem::path& path, std::initializer_list<ByteSpan> byteSpans)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    MLG_CHECKV(stream.is_open(), "Failed to open output file: {}", path.string());

    for(const ByteSpan byteSpan : byteSpans)
    {
        MLG_CHECK(
            byteSpan.size() <= static_cast<size_t>(std::numeric_limits<std::streamsize>::max()));

        const void* p = byteSpan.data();

        stream.write(static_cast<const char*>(p), static_cast<std::streamsize>(byteSpan.size()));

        MLG_CHECKV(stream.good(), "Failed to write output file: {}", path.string());
    }

    stream.close();
    MLG_CHECKV(!stream.fail(), "Failed to close output file: {}", path.string());

    return Result<>::Ok;
}

Result<RawImageData>
LoadRawImage(const CookDirs& cookDirs, const std::string_view texturePath)
{
    const std::filesystem::path textureFullPath = (cookDirs.InputDir / texturePath);
    std::ifstream stream(textureFullPath, std::ios::binary);
    MLG_CHECKV(stream.is_open(),
        "Failed to open image file for reading: {}",
        textureFullPath.string());

    std::error_code ec;
    const uintmax_t fileSize = std::filesystem::file_size(textureFullPath, ec);
    MLG_CHECKV(!ec, "Failed to get image file size: {}", ec.message());

    // Make sure the file size fits within an int for stb_image.
    MLG_CHECKV(fileSize <= static_cast<uintmax_t>(std::numeric_limits<int>::max()),
        "Image file too large: {}",
        textureFullPath.string());

    MLG_CHECKV(fileSize > 0, "Image file is empty: {}", textureFullPath.string());

    std::vector<char> fileData;

    MLG_CHECKV(fileSize <= fileData.max_size(),
        "Image file too large: {}",
        textureFullPath.string());

    fileData.resize(static_cast<size_t>(fileSize));

    stream.read(fileData.data(), static_cast<std::streamsize>(fileData.size()));
    MLG_CHECKV(stream.good(), "Failed to read image file: {}", textureFullPath.string());

    MLG_INFO("Loading image: {}", textureFullPath.string());

    constexpr int kDesiredChannels = GpuHelper::kNumTextureChannels;

    int imgWidth = 0, imgHeight = 0, imgChannelsInFile = 0;
    const int len = static_cast<int>(fileData.size());
    const void* p = fileData.data();
    stbi_uc* imgData = stbi_load_from_memory(static_cast<const stbi_uc*>(p),
        len,
        &imgWidth,
        &imgHeight,
        &imgChannelsInFile,
        kDesiredChannels);

    MLG_CHECKV(imgData, "Failed to decode image: {}", stbi_failure_reason());

    // Construct the RawImageData structure from the loaded image data NOW
    // so if we early exit due to error the stb image data will be freed.
    RawImageData rawImageData(static_cast<uint32_t>(imgWidth),
        static_cast<uint32_t>(imgHeight),
        static_cast<uint32_t>(kDesiredChannels),
        imgData);

    // Free the memory used by the original image data as it is no longer needed.
    std::vector<char>().swap(fileData);

    MLG_CHECKV(rawImageData.ChannelCount > 0,
        "Invalid number of image channels: {}",
        rawImageData.ChannelCount);

    MLG_CHECK(TextureHelper::ValidateDimensions(rawImageData.Width, rawImageData.Height),
        "Invalid image dimensions: {}x{}",
        rawImageData.Width,
        rawImageData.Height);

    return std::move(rawImageData);
}

[[maybe_unused]]
Result<std::vector<std::byte>>
CompressRawImageToBc7(const RawImageData& rawImageData)
{
    const uint32_t width = rawImageData.Width;
    const uint32_t height = rawImageData.Height;
    const uint32_t channelCount = rawImageData.ChannelCount;
    const uint32_t dataSize = width * height * channelCount;

    MLG_CHECKV(width > 0 && height > 0, "Texture dimensions must be nonzero");

    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

    MLG_CHECKV(dataSize == expectedSize,
        "RGBA input must contain {} * {} * 4 bytes",
        width,
        height);

    const std::span<const std::byte> rgba = rawImageData.GetBytes();

    ktxTextureCreateInfo createInfo{};
    createInfo.vkFormat = VK_FORMAT_R8G8B8A8_SRGB;
    createInfo.baseWidth = width;
    createInfo.baseHeight = height;
    createInfo.baseDepth = 1;
    createInfo.numDimensions = 2;
    createInfo.numLevels = 1;
    createInfo.numLayers = 1;
    createInfo.numFaces = 1;
    createInfo.isArray = KTX_FALSE;
    createInfo.generateMipmaps = KTX_FALSE;

    ktxTexture2* rawTexture = nullptr;

    ktx_error_code_e ktxe =
        ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &rawTexture);
    MLG_CHECKV(ktxe == KTX_SUCCESS,
        "ktxTexture2_Create failed with error code {}",
        ktxErrorString(ktxe));

    const KtxTexturePtr texture{ rawTexture };

    const void* p = rgba.data();
    const uint8_t* rgbaBytes = static_cast<const uint8_t*>(p);

    ktxe = ktxTexture_SetImageFromMemory(ktxTexture(texture.get()),
        0, // Mipmap level
        0, // Array layer
        0, // Cube face
        rgbaBytes,
        rgba.size());

    MLG_CHECKV(ktxe == KTX_SUCCESS,
        "ktxTexture_SetImageFromMemory failed with error code {}",
        ktxErrorString(ktxe));

    ktxBasisParams basisParams{};
    basisParams.structSize = sizeof(basisParams);
    basisParams.codec = KTX_BASIS_CODEC_UASTC_LDR_4x4;
    basisParams.uastcFlags = KTX_PACK_UASTC_LEVEL_DEFAULT | KTX_PACK_UASTC_FAVOR_BC7_ERROR;
    basisParams.uastcRDO = KTX_FALSE;
    basisParams.verbose = KTX_TRUE;
    basisParams.threadCount = std::max(1u, std::thread::hardware_concurrency());

    /*ktxBasisParams basisParams{};
    basisParams.structSize = sizeof(basisParams);

    // libktx 5 replacement for basisParams.uastc = KTX_TRUE.
    basisParams.codec = KTX_BASIS_CODEC_UASTC_LDR_4x4;

    // High-quality UASTC encoding tuned for subsequent BC7 transcoding.
    basisParams.uastcFlags = KTX_PACK_UASTC_LEVEL_SLOWER | KTX_PACK_UASTC_FAVOR_BC7_ERROR;

    // RDO helps losslessly compress stored UASTC data, but can reduce
    // image quality. It provides no size benefit after extracting raw BC7.
    basisParams.uastcRDO = KTX_FALSE;

    // Zero uses the encoder default.
    basisParams.threadCount = 0;*/

    ktxe = ktxTexture2_CompressBasisEx(texture.get(), &basisParams);
    MLG_CHECKV(ktxe == KTX_SUCCESS,
        "ktxTexture2_CompressBasisEx failed with error code {}",
        ktxErrorString(ktxe));

    ktxe = ktxTexture2_TranscodeBasis(texture.get(), KTX_TTF_BC7_RGBA, 0);
    MLG_CHECKV(ktxe == KTX_SUCCESS,
        "ktxTexture2_TranscodeBasis failed with error code {}",
        ktxErrorString(ktxe));

    ktx_size_t imageOffset = 0;

    ktxe = ktxTexture_GetImageOffset(ktxTexture(texture.get()),
        0, // Mipmap level
        0, // Array layer
        0, // Cube face
        &imageOffset);
    MLG_CHECKV(ktxe == KTX_SUCCESS,
        "ktxTexture_GetImageOffset failed with error code {}",
        ktxErrorString(ktxe));

    const ktx_uint8_t* textureData = ktxTexture_GetData(ktxTexture(texture.get()));
    const ktx_size_t textureDataSize = ktxTexture_GetDataSize(ktxTexture(texture.get()));

    MLG_CHECK(textureData != nullptr, "Transcoded texture contains no image data");

    p = static_cast<const void*>(textureData);
    const std::byte* textureBytes = static_cast<const std::byte*>(p);
    const std::span<const std::byte> texDataSpan(textureBytes, textureDataSize);

    const ktx_size_t imageSize = ktxTexture_GetImageSize(ktxTexture(texture.get()), 0);

    const std::span<const std::byte> imageDataSpan = texDataSpan.subspan(imageOffset, imageSize);

    return std::vector<std::byte>(imageDataSpan.begin(), imageDataSpan.end());
}

Result<>
WriteTexture(const CookDirs& cookDirs,
    const std::string_view texturePath,
    const uint32_t width,
    const uint32_t height,
    const std::span<const std::byte> textureData)
{
    const std::filesystem::path outPath = cookDirs.OutputDir / std::filesystem::path(texturePath);

    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);
    MLG_CHECKV(!ec, "Failed to create output directory: {}", ec.message());

    MLG_INFO("Writing cooked texture to: {}", outPath.string());

    const TextureHelper::TextureHeader header //
        {
            .Width = width,
            .Height = height,
        };

    const void* p = static_cast<const void*>(&header);
    const std::span<const std::byte> headerSpan(static_cast<const std::byte*>(p), sizeof(header));

    MLG_CHECK(WriteFile(outPath, { headerSpan, textureData }));

    MLG_INFO("Wrote texture: {}", outPath.string());

    return Result<>::Ok;
}

Result<>
CookTexture(CookTextureWorkerParams& params)
{
    const CookDirs& cookDirs = *params.CookDirs;

    auto rawImageResult = LoadRawImage(cookDirs, params.InTexturePath);
    MLG_CHECK(rawImageResult);

    MLG_CHECK(rawImageResult->ChannelCount == 4, "Expected 4 channels in the raw image data.");

    MLG_CHECK(WriteTexture(cookDirs,
        params.OutTexturePath,
        rawImageResult->Width,
        rawImageResult->Height,
        rawImageResult->GetBytes()));

    return Result<>::Ok;
}

void
CookTextureWorker(void* arg)
{
    CookTextureWorkerParams* params = static_cast<CookTextureWorkerParams*>(arg);
    if(params)
    {
        params->Result = CookTexture(*params);

        params->Latch->count_down();
    }
}

[[maybe_unused]] Result<>
WriteResourceBundle(const CookDirs& cookDirs,
    const std::string_view inputFile,
    const ResourceBundle& resourceBundle)
{
    const std::filesystem::path inPath = inputFile;
    std::filesystem::path outputFilePath = cookDirs.OutputDir / inPath.filename();
    outputFilePath.replace_extension(".bin");

    const std::span<const std::byte> buffer = resourceBundle.GetBuffer();
    MLG_CHECK(WriteFile(outputFilePath, { buffer }));

    MLG_INFO("Wrote resource bundle: {}", outputFilePath.string());

    return Result<>::Ok;
}

Result<>
Cook(const CmdLinArgs& args, ThreadPool& threadPool)
{
    Timer timer;
    timer.Start();

    MLG_CHECK(args.IsValid());

    auto loadResult = GltfLoader::Load(args.InputFile);
    MLG_CHECK(loadResult);
    SceneDef sceneDef = std::move(*loadResult);

    auto texPathMap = ReplaceTexturePaths(sceneDef.ModelDefs);
    MLG_CHECK(texPathMap);

    const Result<CookDirs> cookDirs = CookDirs::FromArgs(args);
    MLG_CHECK(cookDirs);

    std::vector<CookTextureWorkerParams> cookTextureParams;
    cookTextureParams.reserve(texPathMap->size());

    std::latch cookTextureLatch(static_cast<int>(texPathMap->size()));

    MLG_DEFER
    {
        // Wait for all texture cooking tasks to complete before exiting the scope.
        cookTextureLatch.wait();
    };

    for(const auto [inTexPath, outTexPath] : *texPathMap)
    {
        const CookTextureWorkerParams tmpParams //
            {
                .CookDirs = &*cookDirs,
                .InTexturePath{ inTexPath },
                .OutTexturePath{ outTexPath },
                .Latch = &cookTextureLatch,
            };

        CookTextureWorkerParams& params = cookTextureParams.emplace_back(tmpParams);

        if(!threadPool.Enqueue(CookTextureWorker, &params))
        {
            params.Result = Result<>::Fail;
            params.Latch->count_down();
        }
    }

    ResourceBundleBuilder builder;

    auto buildResult = builder.Build(sceneDef);
    MLG_CHECK(buildResult);

    const ResourceBundle bundle = std::move(*buildResult);

    cookTextureLatch.wait();

    Result<> cookTexResult = Result<>::Ok;

    for(const CookTextureWorkerParams& params : cookTextureParams)
    {
        if(!params.Result)
        {
            cookTexResult = params.Result;
        }
    }

    MLG_CHECK(cookTexResult, "One or more texture cooking tasks failed");

    MLG_CHECK(WriteResourceBundle(*cookDirs, args.InputFile, bundle));

    timer.Stop();
    MLG_INFO("Cook completed in {} ms", std::trunc(timer.GetElapsedSeconds() * 1000));

    return Result<>::Ok;
}

[[maybe_unused]]
Result<>
Cook2(const CmdLinArgs& args, ThreadPool& /*threadPool*/)
{
    Timer timer;
    timer.Start();

    MLG_CHECK(args.IsValid());

    auto loadResult = GltfLoader::Load(args.InputFile);
    MLG_CHECK(loadResult);
    SceneDef sceneDef = std::move(*loadResult);

    auto texPathMap = ReplaceTexturePaths(sceneDef.ModelDefs);
    MLG_CHECK(texPathMap);

    const Result<CookDirs> cookDirs = CookDirs::FromArgs(args);
    MLG_CHECK(cookDirs);

    std::vector<CookTextureWorkerParams> cookTextureParams;
    cookTextureParams.reserve(texPathMap->size());

    std::latch cookTextureLatch(static_cast<int>(texPathMap->size()));

    MLG_DEFER
    {
        // Wait for all texture cooking tasks to complete before exiting the scope.
        cookTextureLatch.wait();
    };

    const bool stop = false;
    for(const auto [inTexPath, outTexPath] : *texPathMap)
    {
        const CookTextureWorkerParams tmpParams //
            {
                .CookDirs = &*cookDirs,
                .InTexturePath{ inTexPath },
                .OutTexturePath{ outTexPath },
                .Latch = &cookTextureLatch,
            };

        CookTextureWorkerParams& params = cookTextureParams.emplace_back(tmpParams);

        Timer timer2;
        timer2.Start();
        CookTextureWorker(&params);
        timer2.Stop();
        MLG_INFO("Cooked texture {} in {} ms",
            inTexPath,
            std::trunc(timer2.GetElapsedSeconds() * 1000));

        if(!stop)
        {
            break;
        }

        /*if(!threadPool.Enqueue(CookTextureWorker, &params))
        {
            params.Result = Result<>::Fail;
            params.Latch->count_down();
        }*/
    }
    std::abort();

    /*ResourceBundleBuilder builder;

    auto buildResult = builder.Build(sceneDef);
    MLG_CHECK(buildResult);

    const ResourceBundle bundle = std::move(*buildResult);

    cookTextureLatch.wait();

    Result<> cookTexResult = Result<>::Ok;

    for(const CookTextureWorkerParams& params : cookTextureParams)
    {
        if(!params.Result)
        {
            cookTexResult = params.Result;
        }
    }

    MLG_CHECK(cookTexResult, "One or more texture cooking tasks failed");

    MLG_CHECK(WriteResourceBundle(*cookDirs, args.InputFile, bundle));

    timer.Stop();
    MLG_INFO("Cook completed in {} ms", std::trunc(timer.GetElapsedSeconds() * 1000));*/

    return Result<>::Ok;
}

} // namespace

int
main(int argc, char** argv)
{
    Log::SetLevel(Log::Level::Trace);
    ThreadPool threadPool;

    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    if(ec)
    {
        MLG_ERROR("Failed to get current working directory: {}", ec.message());
        return 1;
    }
    MLG_INFO("Current working directory: {}", cwd.string());

    const CmdLinArgs args = ParseArgs(argc, argv);

    const Result<> result = Cook(args, threadPool);
    if(!result)
    {
        Usage();
        return 1;
    }

    return 0;
}