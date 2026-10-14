
#include "Defer.h"
#include "GltfLoader.h"
#include "GpuHelper.h"
#include "Log.h"
#include "ResourceBundle.h"
#include "Result.h"
#include "Scene.h"
#include "SceneTypes.h"
#include "TextureFormat.h"
#include "TextureHelper.h"
#include "ThreadPool.h"
#include "Timer.h"

#include <bc7enc.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <latch>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stb_image.h>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

namespace
{
static_assert(std::is_trivially_copyable_v<TextureHelper::TextureHeader>);
static_assert(std::is_standard_layout_v<TextureHelper::TextureHeader>);
static_assert(sizeof(TextureHelper::TextureHeader) == 24); // NOLINT(readability-magic-numbers)

static_assert(GpuHelper::kNumTextureChannels == 4);

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

using StbDataPtr = std::unique_ptr<stbi_uc, StbImageDeleter>;

struct RawTexture
{
    RawTexture(
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
        const size_t sizeofData = static_cast<size_t>(Width)
            * static_cast<size_t>(Height)
            * static_cast<size_t>(ChannelCount);
        return std::span<const std::byte>(static_cast<const std::byte*>(p), sizeofData);
    }

    uint32_t Width{ 0 };
    uint32_t Height{ 0 };
    uint32_t ChannelCount{ 0 };
    StbDataPtr StbData;
};

struct CookedTexture
{
    TextureFormat Format;
    uint32_t Width{ 0 };
    uint32_t Height{ 0 };
    std::unique_ptr<std::vector<std::byte>> Bytes;
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

Result<RawTexture>
LoadRawTexture(const CookDirs& cookDirs, const std::string_view texturePath)
{
    MLG_INFO("Loading image: {}...", texturePath);

    const std::filesystem::path textureFullPath = (cookDirs.InputDir / texturePath);
    std::ifstream stream(textureFullPath, std::ios::binary);
    MLG_CHECKV(stream.is_open(),
        "Failed to open image file for reading: {}",
        textureFullPath.string());

    std::error_code ec;
    const uintmax_t fileSize = std::filesystem::file_size(textureFullPath, ec);
    MLG_CHECKV(!ec, "Failed to get image file size: {}", ec.message());

    MLG_CHECKV(fileSize > 0, "Image file is empty: {}", textureFullPath.string());

    // Make sure the file size fits within an int for stb_image.
    MLG_CHECKV(fileSize <= static_cast<uintmax_t>(std::numeric_limits<int>::max()),
        "Image file too large: {}",
        textureFullPath.string());

    std::vector<char> fileData;

    MLG_CHECKV(fileSize <= fileData.max_size(),
        "Image file too large: {}",
        textureFullPath.string());

    fileData.resize(static_cast<size_t>(fileSize));

    stream.read(fileData.data(), static_cast<std::streamsize>(fileData.size()));
    MLG_CHECKV(stream.good(), "Failed to read image file: {}", textureFullPath.string());

    stream.close();

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

    // Construct the RawTexture structure from the loaded image data NOW
    // so if we early exit due to error the stb image data will be freed.
    RawTexture rawTexture(static_cast<uint32_t>(imgWidth),
        static_cast<uint32_t>(imgHeight),
        static_cast<uint32_t>(kDesiredChannels),
        imgData);

    // Free the memory used by the original image data.
    std::vector<char>().swap(fileData);

    MLG_CHECKV(rawTexture.ChannelCount > 0,
        "Invalid number of image channels: {}",
        rawTexture.ChannelCount);

    MLG_CHECK(TextureHelper::ValidateDimensions(rawTexture.Width, rawTexture.Height),
        "Invalid image dimensions: {}x{}",
        rawTexture.Width,
        rawTexture.Height);

    return std::move(rawTexture);
}

Result<CookedTexture>
EncodeBC7(const RawTexture& rawTexture, const std::string_view texturePath)
{
    MLG_INFO("Encoding {} -> BC7...", texturePath);

    const size_t widthSz = static_cast<size_t>(rawTexture.Width);
    const size_t heightSz = static_cast<size_t>(rawTexture.Height);

    std::span<const std::byte> rgba = rawTexture.GetBytes();

    MLG_CHECK(widthSz > 0 && heightSz > 0, "Invalid image dimensions: {}x{}", widthSz, heightSz);
    MLG_CHECK(rgba.size() / widthSz >= 4, "Invalid RGBA buffer size: {}", rgba.size());
    MLG_CHECK(rgba.size() / (widthSz * 4) >= heightSz, "Invalid RGBA buffer size: {}", rgba.size());
    MLG_CHECKV(rgba.size() == widthSz * heightSz * 4, "Invalid RGBA buffer size: {}", rgba.size());

    const size_t blocksWide = (widthSz + 3) / 4;
    const size_t blocksHigh = (heightSz + 3) / 4;
    const size_t finalWidth = blocksWide * 4;
    const size_t finalHeight = blocksHigh * 4;

    std::vector<std::byte> resizedRgba;

    if(finalWidth != widthSz || finalHeight != heightSz)
    {
        resizedRgba.resize(finalWidth * finalHeight * 4);

        const void* srcP = rgba.data();
        void* dstP = resizedRgba.data();

        const unsigned char* result = stbir_resize_uint8_srgb(static_cast<const stbi_uc*>(srcP),
            static_cast<int>(widthSz),
            static_cast<int>(heightSz),
            static_cast<int>(widthSz * 4),
            static_cast<stbi_uc*>(dstP),
            static_cast<int>(finalWidth),
            static_cast<int>(finalHeight),
            static_cast<int>(finalWidth * 4),
            STBIR_RGBA);

        MLG_CHECK(result, "Failed to resize image: {}", texturePath);
        
        rgba = std::span<const std::byte>(resizedRgba);
    }
    constexpr size_t kBytesPerBlock = 16;
    constexpr size_t kMaxSizeofVector = std::vector<std::byte>().max_size();

    MLG_CHECK(kMaxSizeofVector / (blocksWide * blocksHigh) >= kBytesPerBlock,
        "Number of output bytes exceeds maximum vector size");

    // BC7 always uses 16 bytes per 4x4 block.
    std::vector<std::byte> bc7Bytes(blocksWide * blocksHigh * kBytesPerBlock);

    const std::span<std::byte> bc7ByteSpan(bc7Bytes);

    // Call once before using the encoder.
    static std::once_flag bc7enc_init_flag;
    std::call_once(bc7enc_init_flag, bc7enc_compress_block_init);

    bc7enc_compress_block_params params{};
    bc7enc_compress_block_params_init(&params);

    // A temporary tightly packed 4x4 RGBA block.
    std::byte blockPixels[4 * 4 * 4];

    for(size_t blockY = 0; blockY < blocksHigh; ++blockY)
    {
        for(size_t blockX = 0; blockX < blocksWide; ++blockX)
        {
            for(size_t y = 0; y < 4; ++y)
            {
                for(size_t x = 0; x < 4; ++x)
                {
                    // Clamp partial blocks at the image edges.
                    const size_t sourceX = std::min((blockX * 4) + x, finalWidth - 1);
                    const size_t sourceY = std::min((blockY * 4) + y, finalHeight - 1);

                    const std::size_t sourceOffset = ((sourceY * finalWidth) + sourceX) * 4;

                    const std::size_t blockOffset = ((y * 4) + x) * 4;

                    std::copy_n(rgba.subspan(sourceOffset).data(), 4, &blockPixels[blockOffset]);
                }
            }

            const std::size_t outputOffset = ((blockY * blocksWide) + blockX) * 16;

            // The return value means “block contains transparency”;
            // it is not a success/failure result.
            bc7enc_compress_block(bc7ByteSpan.subspan(outputOffset).data(),
                &blockPixels[0],
                &params);
        }
    }

    return CookedTexture //
        {
            .Format = TextureFormat::GetBC7(),
            .Width = static_cast<uint32_t>(finalWidth),
            .Height = static_cast<uint32_t>(finalHeight),
            .Bytes = std::make_unique<std::vector<std::byte>>(std::move(bc7Bytes)),
        };
}

Result<>
WriteTexture(const CookDirs& cookDirs,
    const std::string_view texturePath,
    const std::span<const CookedTexture> cookedTextures)
{
    const std::filesystem::path srcPath = (cookDirs.InputDir / texturePath);
    const std::filesystem::path dstPath = (cookDirs.OutputDir / texturePath);

    // Ensure the output directory exists before writing the texture.
    std::error_code ec;
    std::filesystem::create_directories(dstPath.parent_path(), ec);
    MLG_CHECKV(!ec, "Failed to create output directory: {}", ec.message());

    MLG_INFO("Writing: {} -> {}...", srcPath.string(), dstPath.string());

    // Copy the original file - it will be the fallback for HW that doesn't support the compressed
    // formats.
    std::filesystem::copy(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing, ec);
    MLG_CHECKV(!ec,
        "Failed to copy texture from {} to {}: {}",
        srcPath.string(),
        dstPath.string(),
        ec.message());

    for(const CookedTexture& cookedTexture : cookedTextures)
    {
        const TextureHelper::TextureHeader header //
            {
                .Width = cookedTexture.Width,
                .Height = cookedTexture.Height,
                .FourCC = cookedTexture.Format.HeaderFourcc,
            };
        const std::span<const std::byte> headerSpan = std::as_bytes(std::span(&header, 1));
        const std::span<const std::byte> imageDataSpan =
            std::span(cookedTexture.Bytes->data(), cookedTexture.Bytes->size());

        std::filesystem::path curDstPath = dstPath;

        curDstPath.replace_extension(cookedTexture.Format.Extension.AsStringView());

        MLG_INFO("Writing: {} -> {}...", srcPath.string(), curDstPath.string());

        MLG_CHECK(WriteFile(curDstPath, { headerSpan, imageDataSpan }));
    }

    return Result<>::Ok;
}

Result<>
CookTexture(CookTextureWorkerParams& params)
{
    Timer timer;
    timer.Start();

    auto rawImageResult = LoadRawTexture(params.CookDirs, params.TexturePath);
    MLG_CHECK(rawImageResult);

    MLG_CHECK(rawImageResult->ChannelCount == 4,
        "Expected 4 channels in the raw image data for texture: {}",
        params.TexturePath);

    auto bc7Result = EncodeBC7(*rawImageResult, params.TexturePath);
    MLG_CHECK(bc7Result, "Failed to encode BC7 texture: {}", params.TexturePath);

    const CookedTexture cookedTextures[] //
        { std::move(*bc7Result) };

    MLG_CHECK(WriteTexture(params.CookDirs, params.TexturePath, cookedTextures));

    MLG_INFO("Cooked texture: {} in {} ms", params.TexturePath, timer.GetElapsedSeconds());

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

    const Result<CookDirs> cookDirs = CookDirs::FromArgs(args);
    MLG_CHECK(cookDirs);

    auto loadResult = GltfLoader::Load(args.InputFile);
    MLG_CHECK(loadResult);
    const SceneDef sceneDef = std::move(*loadResult);

    ResourceBundleBuilder builder;

    auto buildResult = builder.Build(sceneDef);
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

        if(!threadPool.Enqueue(CookTextureWorker, &params))
        {
            params.Result = Result<>::Fail;
            params.Latch->count_down();
        }
    }

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