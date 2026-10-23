#pragma once

#include "CoopTask.h"
#include "FileFetcher.h"
#include "FilePath.h"
#include "GpuTypes.h"
#include "Ktx2.h"
#include "TextureFormat.h"

#include <atomic>
#include <optional>
#include <string_view>
#include <variant>
#include <webgpu/webgpu_cpp.h>

class GpuHelper;
class ThreadPool;
class ResourceBundle;
struct TextureResource;

namespace wgpu
{
class CommandEncoder;
}

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

    class StbContainer;

    TextureLoader(const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        ThreadPool& threadPool,
        const FilePath& path,
        wgpu::CommandEncoder commandEncoder);

    Result<> OnStart() override;

    void OnUpdate() override;

    Result<> Commit();

    Result<> BeginDecode();

    static void Decode(void* userData);

    Result<> Decode(const Ktx2Container& ktx2);
    Result<> Decode(const StbContainer& stb);

    /// Container for an stb texture, providing access to its metadata and mip levels.
    class StbContainer
    {
    public:
        static Result<StbContainer> Create(std::vector<std::byte>&& buffer);

        uint32_t GetWidth() const { return static_cast<uint32_t>(m_Width); }
        uint32_t GetHeight() const { return static_cast<uint32_t>(m_Height); }
        // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
        uint32_t GetMipCount() const { return 1; }
        // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
        TextureFormat GetFormat() const { return TextureFormat::GetRGBA(); }

        Result<std::span<const std::byte>> GetMipBytes(uint32_t mipLevel) const
        {
            MLG_CHECK(mipLevel == 0, "Invalid mip level for stb texture: {}", mipLevel);
            return GetBytes();
        }

        std::span<const std::byte> GetBytes() const { return std::span<const std::byte>(m_Buffer); }

    private:
        int m_Width;
        int m_Height;
        std::vector<std::byte> m_Buffer;

        StbContainer(int width, int height, std::vector<std::byte>&& buffer)
            : m_Width(width),
              m_Height(height),
              m_Buffer(std::move(buffer))
        {
        }
    };

    const GpuHelper* m_GpuHelper{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    FilePath m_Path;
    FetchRequestId m_FetchRequestId;
    std::variant<std::monostate, Ktx2Container, StbContainer> m_Container;
    std::optional<GpuTextureStagingBuffer> m_StagingBuffer;
    wgpu::Texture m_Texture{ nullptr };
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };
    Result<> m_DecodeResult;
    std::atomic<bool> m_DecodeCompletionFlag{ false };

    Stage m_Stage{ Stage::None };
};