
#include "Compressors.h"
#include "Defer.h"
#include "GltfLoader.h"
#include "Ktx2Writer.h"
#include "Log.h"
#include "MipMapChain.h"
#include "ResourceBundle.h"
#include "Result.h"
#include "Scene.h"
#include "SceneTypes.h"
#include "TextureFormat.h"
#include "TextureLoader.h"
#include "ThreadPool.h"
#include "Timer.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <latch>
#include <limits>
#include <span>
#include <stb_image.h>
#include <string_view>
#include <sys/types.h>
#include <system_error>
#include <vector>

namespace
{
/// Command line arguments for the cook tool.
struct CmdLineArgs
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
    static Result<CookDirs> FromArgs(const CmdLineArgs& args);
};

/// Parameters for a worker that cooks a texture.
struct CookTextureWorkerParams
{
    CookDirs CookDirs;
    RelativeFilePath TexturePath;
    std::latch* Latch{ nullptr };
    Result<> Result;
};

void
Usage()
{
    std::cout << "Usage: cook -i <input file> -o <output directory>" << std::endl;
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
CmdLineArgs
ParseArgs(int argc, char** argv)
{
    CmdLineArgs args;
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

/// Creates a CookDirs instance from command line arguments.
Result<CookDirs>
CookDirs::FromArgs(const CmdLineArgs& args)
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

using ByteSpan = std::span<const std::byte>;

/// Writes the byte spans in order to a file at the specified path.
Result<>
WriteFile(const std::filesystem::path& path, std::span<ByteSpan> byteSpans)
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

/// Loads a raw texture from the input directory specified in CookDirs.
Result<MipImage>
LoadRawTexture(const CookDirs& cookDirs, const std::string_view path)
{
    MLG_INFO("Loading image: {}...", path);

    const std::filesystem::path fullPath = (cookDirs.InputDir / path);
    std::ifstream stream(fullPath, std::ios::binary);
    MLG_CHECKV(stream.is_open(), "Failed to open image file for reading: {}", fullPath.string());

    std::error_code ec;
    const uintmax_t fileSize = std::filesystem::file_size(fullPath, ec);
    MLG_CHECKV(!ec, "Failed to get image file size: {}", ec.message());

    MLG_CHECKV(fileSize > 0, "Image file is empty: {}", fullPath.string());

    // Make sure the file size fits within an int for stb_image.
    MLG_CHECKV(fileSize <= static_cast<uintmax_t>(std::numeric_limits<int>::max()),
        "Image file too large: {}",
        fullPath.string());

    std::vector<char> fileData;

    MLG_CHECKV(fileSize <= fileData.max_size(), "Image file too large: {}", fullPath.string());

    fileData.resize(static_cast<size_t>(fileSize));

    stream.read(fileData.data(), static_cast<std::streamsize>(fileData.size()));
    MLG_CHECKV(stream.good(), "Failed to read image file: {}", fullPath.string());

    stream.close();

    constexpr int kDesiredChannels = TextureFormat::kDefaultChannelCount;

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

    StbDataPtr stbData(imgData);

    const uint32_t uImgWidth = static_cast<uint32_t>(imgWidth);
    const uint32_t uImgHeight = static_cast<uint32_t>(imgHeight);

    MLG_CHECKV(imgWidth > 0 && imgHeight > 0,
        "Invalid image dimensions: {}x{}",
        imgWidth,
        imgHeight);

    return MipImage::Create(uImgWidth, uImgHeight, TextureFormat::GetRGBA(), std::move(stbData));
}

/// Writes a mipmapped texture to the output directory in KTX2 format.
Result<>
WriteTexture(const CookDirs& cookDirs, const std::string_view path, const MipMapChain& mipmapChain)
{
    const std::filesystem::path srcPath = (cookDirs.InputDir / path);
    const std::filesystem::path dstPath = (cookDirs.OutputDir / path);

    // Ensure the output directory exists before writing the texture.
    std::error_code ec;
    std::filesystem::create_directories(dstPath.parent_path(), ec);
    MLG_CHECKV(!ec, "Failed to create output directory: {}", ec.message());

    MLG_INFO("Writing: {} -> {}...", srcPath.string(), dstPath.string());

    std::filesystem::path curDstPath = dstPath;

    const std::string extension =
        std::format("{}.ktx2", mipmapChain.GetFormat().GetExtension().AsStringView());

    curDstPath.replace_extension(extension);

    MLG_INFO("Writing: {} -> {}...", srcPath.string(), curDstPath.string());

    MLG_CHECK(WriteKtx2(curDstPath, mipmapChain));

    return Result<>::Ok;
}

Result<>
CookTexture(CookTextureWorkerParams& params)
{
    Timer timer;
    timer.Start();

    auto rawImageResult = LoadRawTexture(params.CookDirs, params.TexturePath);
    MLG_CHECK(rawImageResult);

    MipImage baseMip = std::move(*rawImageResult);

    // Create one block aligned mip chain for all encodings.
    // This works ok when using bc7 and astc4x4 because they have the same block size.
    // But if we ever use a different block size for ASTC, we may need to handle it differently.

    const TextureFormat& bc7Format = TextureFormat::GetBC7();
    const TextureFormat& astcFormat = TextureFormat::GetASTC();

    const size_t bc7BlockAlignedWidth = bc7Format.GetBlockAlignedWidth(baseMip.GetWidth());
    const size_t bc7BlockAlignedHeight = bc7Format.GetBlockAlignedHeight(baseMip.GetHeight());
    const size_t astcBlockAlignedWidth = astcFormat.GetBlockAlignedWidth(baseMip.GetWidth());
    const size_t astcBlockAlignedHeight = astcFormat.GetBlockAlignedHeight(baseMip.GetHeight());

    const size_t blockAlignedWidth = std::max(bc7BlockAlignedWidth, astcBlockAlignedWidth);
    const size_t blockAlignedHeight = std::max(bc7BlockAlignedHeight, astcBlockAlignedHeight);

    if(baseMip.GetWidth() != blockAlignedWidth || baseMip.GetHeight() != blockAlignedHeight)
    {
        auto resizeResult = baseMip.Resize(static_cast<uint32_t>(blockAlignedWidth),
            static_cast<uint32_t>(blockAlignedHeight));
        MLG_CHECK(resizeResult, "Failed to resize base image to block aligned dimensions.");
        baseMip = std::move(*resizeResult);
    }

    auto mipChainResult = MipMapChain::Create(std::move(baseMip));
    MLG_CHECK(mipChainResult, "Failed to generate mip chain for texture: {}", params.TexturePath);

    auto bc7Result = EncodeBC7(*mipChainResult, params.TexturePath);
    MLG_CHECK(bc7Result, "Failed to encode BC7 texture: {}", params.TexturePath);

    auto astcResult = EncodeASTC(*mipChainResult, params.TexturePath);
    MLG_CHECK(astcResult, "Failed to encode ASTC texture: {}", params.TexturePath);

    MLG_CHECK(WriteTexture(params.CookDirs, params.TexturePath, *bc7Result));
    MLG_CHECK(WriteTexture(params.CookDirs, params.TexturePath, *astcResult));
    MLG_CHECK(WriteTexture(params.CookDirs, params.TexturePath, *mipChainResult));

    MLG_INFO("Cooked texture: {} in {} ms", params.TexturePath, timer.GetElapsedSeconds());

    return Result<>::Ok;
}

/// Worker function for cooking a texture in a background thread.
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

Result<>
WriteResourceBundle(const CookDirs& cookDirs,
    const std::string_view inputFile,
    const ResourceBundle& resourceBundle)
{
    const std::filesystem::path inPath = inputFile;
    std::filesystem::path outputFilePath = cookDirs.OutputDir / inPath.filename();
    outputFilePath.replace_extension(".bin");

    ByteSpan byteSpans[] = { resourceBundle.GetBuffer() };
    MLG_CHECK(WriteFile(outputFilePath, byteSpans));

    MLG_INFO("Wrote resource bundle: {}", outputFilePath.string());

    return Result<>::Ok;
}

Result<>
Cook(const CmdLineArgs& args, ThreadPool& threadPool)
{
    Timer timer;
    timer.Start();

    MLG_CHECK(args.IsValid());

    const Result<CookDirs> cookDirs = CookDirs::FromArgs(args);
    MLG_CHECK(cookDirs);

    auto loadResult = GltfLoader::Load(args.InputFile);
    MLG_CHECK(loadResult);
    const SceneDef sceneDef = std::move(*loadResult);

    ResourceBundleBuilder builder;
    const ResourceBundleBuilder::Options options //
        {
            .TextureEncodings = TextureResourceEncoding::All,
        };

    auto buildResult = builder.Build(sceneDef, options);
    MLG_CHECK(buildResult);

    const ResourceBundle bundle = std::move(*buildResult);

    const std::span<const TextureResource> textureRsrcs = bundle.GetTextures();

    std::vector<std::string_view> texturePaths;
    texturePaths.reserve(textureRsrcs.size());

    for(const TextureResource& texRsrc : textureRsrcs)
    {
        texturePaths.push_back(bundle.GetStringView(texRsrc.TexturePath));
    }

    std::vector<CookTextureWorkerParams> cookTextureParams;
    cookTextureParams.reserve(texturePaths.size());

    // Initialize a latch to wait for texture cooking tasks to complete.
    std::latch cookTextureLatch(static_cast<int>(texturePaths.size()));

    MLG_DEFER
    {
        // In case of early exit due to error wait for all texture cooking tasks to complete before
        // exiting the scope.
        cookTextureLatch.wait();
    };

    for(const auto& inTexPath : texturePaths)
    {
        const auto inPathResult = RelativeFilePath::Create(inTexPath);
        MLG_CHECK(inPathResult);

        const CookTextureWorkerParams tmpParams //
            {
                .CookDirs = *cookDirs,
                .TexturePath = *inPathResult,
                .Latch = &cookTextureLatch,
            };

        CookTextureWorkerParams& params = cookTextureParams.emplace_back(tmpParams);

        // CookTextureWorker(&params);

        if(!threadPool.Enqueue(CookTextureWorker, &params))
        {
            params.Result = Result<>::Fail;
            params.Latch->count_down();
        }
    }

    // Wait for completion
    cookTextureLatch.wait();

    Result<> cookTexResult = Result<>::Ok;

    for(const CookTextureWorkerParams& params : cookTextureParams)
    {
        if(!params.Result)
        {
            cookTexResult = params.Result;
            break;
        }
    }

    MLG_CHECK(cookTexResult, "One or more texture cooking tasks failed");

    MLG_CHECK(WriteResourceBundle(*cookDirs, args.InputFile, bundle));

    timer.Stop();
    MLG_INFO("Cook completed in {} ms", std::trunc(timer.GetElapsedSeconds() * 1000));

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

    const CmdLineArgs args = ParseArgs(argc, argv);

    const Result<> result = Cook(args, threadPool);
    if(!result)
    {
        Usage();
        return 1;
    }

    return 0;
}