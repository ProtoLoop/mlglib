#pragma once

#include "CoopTask.h"
#include "FileFetcher.h"
#include "FilePath.h"
#include "TextureFormat.h"

#include <atomic>
#include <string_view>
#include <webgpu/webgpu_cpp.h>

class GpuHelper;
class ThreadPool;
class ResourceBundle;
struct TextureResource;

namespace wgpu
{
class CommandEncoder;
}

/// Header for a cooked texture file.
struct TextureHeader
{
    static constexpr TextureFormat::FourCC kMagic0{ 'M', 'L', 'G', ' ' };
    static constexpr TextureFormat::FourCC kMagic1{ 'T', 'E', 'X', ' ' };
    static constexpr uint32_t kVersion = 1;

    static constexpr uint32_t kMaxImageWidth = 4096;
    static constexpr uint32_t kMaxImageHeight = 4096;

    /// Checks if the texture dimensions are within the allowed limits.
    static Result<> CheckDimensions(const uint32_t width, const uint32_t height);

    static Result<> ValidateFile(const std::span<const std::byte> fileBuffer);

    static_assert(sizeof(TextureFormat::FourCC) == 4, "Fourcc must be 4 bytes in size");

    TextureHeader(const uint32_t width, const uint32_t height, const TextureFormat::FourCC fourCC)
        : Width(width),
          Height(height),
          FourCC(fourCC)
    {
    }

    // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
    const TextureFormat::FourCC Magic0{ kMagic0 };
    const TextureFormat::FourCC Magic1{ kMagic1 };
    const uint32_t Version{ kVersion };
    const uint32_t Width{ 0 };
    const uint32_t Height{ 0 };
    const TextureFormat::FourCC FourCC;
    // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
};

/// Loader for textures. Handles loading from memory, files, and resource bundles.
class TextureLoader : public ICoopTask<>
{
public:
    TextureLoader() = delete;
    ~TextureLoader() override = default;
    TextureLoader(const TextureLoader&) = delete;
    TextureLoader& operator=(const TextureLoader&) = delete;
    TextureLoader(TextureLoader&&) = delete;
    TextureLoader& operator=(TextureLoader&&) = delete;

    Result<wgpu::Texture> Take();

    static Result<wgpu::Texture> LoadFromMemory(const GpuHelper& gpuHelper,
        const std::string_view name,
        const uint32_t width,
        const uint32_t height,
        const TextureFormat& format,
        const std::span<const std::byte> pixelBytes);

    static Result<std::unique_ptr<TextureLoader>> LoadFromFile(const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        ThreadPool& threadPool,
        const FilePath& path,
        wgpu::CommandEncoder& commandEncoder);

    static Result<std::unique_ptr<TextureLoader>> LoadFromResource(const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        ThreadPool& threadPool,
        const DirectoryPath& workingDirectory,
        const ResourceBundle& resourceBundle,
        const TextureResource& texRsrc,
        wgpu::CommandEncoder& commandEncoder);

private:
    enum class Stage
    {
        None,
        LoadFromFile,
        FetchingFile,
        Decoding,
        Succeeded,
        Failed
    };

    TextureLoader(const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        ThreadPool& threadPool,
        const FilePath& path,
        wgpu::CommandEncoder commandEncoder);

    Result<> OnStart() override;

    void OnUpdate() override;

    static Result<> CopyBytesToStagingBuffer(const uint32_t width,
        const uint32_t height,
        const std::span<const std::byte> srcBytes,
        std::span<std::byte> dstBytes,
        const TextureFormat& format);

    Result<> CopyBytesToStagingBuffer(const std::span<const std::byte> srcBytes);

    Result<> Commit();

    Result<> BeginDecode();

    static void DecodeMlg(void* userData);
    static void DecodeStb(void* userData);

    Result<> DecodeMlg();
    Result<> DecodeStb();

    const GpuHelper* m_GpuHelper{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    uint32_t m_Width{ 0 };
    uint32_t m_Height{ 0 };
    TextureFormat m_Format = TextureFormat::GetUnknown();
    std::optional<FilePath> m_Path;
    FetchRequestId m_FetchRequestId;
    wgpu::Buffer m_StagingBuffer{ nullptr };
    std::span<std::byte> m_MappedMemory;
    std::vector<std::byte> m_FileBytes; // Raw file data for the texture
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };
    wgpu::Texture m_Texture{ nullptr };
    Result<> m_DecodeResult;
    std::atomic<bool> m_DecodeCompletionFlag{ false };

    Stage m_Stage{ Stage::None };
};