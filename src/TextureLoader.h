#pragma once

#include "CoopTask.h"
#include "FileFetcher.h"
#include "FilePath.h"
#include "GpuTypes.h"
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
    uint32_t m_MipCount{ 1 };
    TextureFormat m_Format = TextureFormat::GetUnknown();
    std::optional<FilePath> m_Path;
    FetchRequestId m_FetchRequestId;
    std::optional<GpuTextureStagingBuffer> m_StagingBuffer;
    wgpu::Texture m_Texture{ nullptr };
    std::vector<std::byte> m_FileBytes; // Raw file data for the texture
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };
    Result<> m_DecodeResult;
    std::atomic<bool> m_DecodeCompletionFlag{ false };

    Stage m_Stage{ Stage::None };
};