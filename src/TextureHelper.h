#pragma once

#include "CoopTask.h"
#include "FileFetcher.h"
#include "FilePath.h"

#include <atomic>
#include <webgpu/webgpu_cpp.h>

class GpuHelper;
class ThreadPool;

namespace wgpu
{
class CommandEncoder;
}

namespace TextureHelper
{

/// Header for a cooked texture file.
struct TextureHeader
{
    static constexpr uint32_t kMagicRaw0 = (static_cast<uint32_t>('M') << 24)
        | (static_cast<uint32_t>('L') << 16)
        | (static_cast<uint32_t>('G') << 8)
        | (static_cast<uint32_t>(' '));

    static constexpr uint32_t kMagicRaw1 = (static_cast<uint32_t>('T') << 24)
        | (static_cast<uint32_t>('E') << 16)
        | (static_cast<uint32_t>('X') << 8)
        | (static_cast<uint32_t>(' '));

    static constexpr uint32_t kMagic0 =
        std::endian::native == std::endian::big ? kMagicRaw0 : std::byteswap(kMagicRaw0);
    static constexpr uint32_t kMagic1 =
        std::endian::native == std::endian::big ? kMagicRaw1 : std::byteswap(kMagicRaw1);
    static constexpr uint32_t kVersion = 1;

    static constexpr uint32_t kMaxImageWidth = 4096;
    static constexpr uint32_t kMaxImageHeight = 4096;

    static Result<> Validate(const std::span<const std::byte> buffer);

    const uint32_t Magic0{ kMagic0 };   // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    const uint32_t Magic1{ kMagic1 };   // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    const uint32_t Version{ kVersion }; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    uint32_t Width{ 0 };
    uint32_t Height{ 0 };
};

/// Checks if the buffer contains the correct magic numbers for a texture header.
bool CheckHeaderMagic(const std::span<const std::byte> buffer);

/// Checks if the buffer contains the correct version for a texture header.
bool CheckHeaderVersion(const std::span<const std::byte> buffer);

/// Retrieves the texture header from the buffer.
/// Returns a pointer to the texture header if the buffer contains a valid header, or nullptr otherwise.
/// A valid header must have the correct magic numbers and version, and valid dimensions.
const TextureHeader* GetTextureHeader(const std::span<const std::byte> buffer);

/// Checks if the texture dimensions are within the allowed limits.
Result<> ValidateDimensions(const uint32_t width, const uint32_t height);

/// Helper class for staging textures to the GPU.  Copying pixels (StagePixels()) to a staging
/// buffer is separate from uploading (Commit()) the texture to the GPU.  This enables
/// decoding/staging to occur in a background thread, while committing to the GPU can be done on the
/// main thread.
class TextureBuilder
{
public:
    TextureBuilder() = delete;
    ~TextureBuilder() = default;
    TextureBuilder(const TextureBuilder&) = delete;
    TextureBuilder& operator=(const TextureBuilder&) = delete;
    TextureBuilder(TextureBuilder&&) = default;
    TextureBuilder& operator=(TextureBuilder&&) = default;

    static Result<TextureBuilder> Create(const GpuHelper& gpuHelper,
        const FilePath& path,
        const uint32_t width,
        const uint32_t height);

    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }

    /// Stages the given pixel data into the mapped memory of the staging buffer.
    /// The source byte size must match the expected size based on the texture dimensions.
    Result<> StagePixels(const std::span<const std::byte> srcBytes);

    /// Commits the staged pixel data from the staging buffer to a GPU texture and
    /// returns the texture.
    /// The command encoder is used to record GPU commands.
    /// The caller is responsible for submitting the recorded command buffer to the GPU queue.
    ///
    /// *** MUST RUN ON THE SAME THREAD AS THE CREATION OF THE STAGING BUFFER ***
    ///
    /// After commit, staging and further commits are invalid.
    Result<wgpu::Texture> Commit(const wgpu::CommandEncoder& commandEncoder);

private:
    TextureBuilder(const GpuHelper& gpuHelper,
        const FilePath& path,
        wgpu::Buffer stagingBuffer,
        std::span<std::byte> mappedMemory,
        const uint32_t width,
        const uint32_t height);

    const GpuHelper* m_GpuHelper{ nullptr };
    FilePath m_Path;
    wgpu::Buffer m_StagingBuffer{ nullptr };
    std::span<std::byte> m_MappedMemory;
    uint32_t m_Width{ 0 };
    uint32_t m_Height{ 0 };
};
} // namespace TextureHelper

/// Task responsible for decoding a texture from raw image data.
/// A wgpu::CommandEncoder must be provided to record the necessary GPU commands for texture upload.
/// This task records the necessary GPU commands to the command encoderfor texture upload.
/// The caller is responsible for submitting the recorded command buffer to the GPU queue.
class TextureDecodeTask : public ICoopTask<>
{
public:
    TextureDecodeTask(const GpuHelper& gpuHelper,
        std::vector<std::byte>&& rawData,
        const FilePath& path,
        ThreadPool& threadPool,
        wgpu::CommandEncoder commandEncoder);

    TextureDecodeTask() = delete;
    ~TextureDecodeTask() override = default; // The base class will verify completion
    TextureDecodeTask(const TextureDecodeTask&) = delete;
    TextureDecodeTask& operator=(const TextureDecodeTask&) = delete;
    TextureDecodeTask(TextureDecodeTask&&) = delete;
    TextureDecodeTask& operator=(TextureDecodeTask&&) = delete;

    Result<wgpu::Texture> Take();

private:
    enum class Stage
    {
        None,
        Decoding,
        Succeeded,
        Failed
    };

    Result<> OnStart() override;

    void OnUpdate() override;

    Result<> BeginDecode();

    Result<> BeginDecodeMlg();

    Result<> BeginDecodeStb();

    Result<> DecodeMlg();
    Result<> DecodeStb();

    // Worker thread entry point for decoding the texture.
    static void DecodeMlg(void* userData);
    static void DecodeStb(void* userData);

    const GpuHelper* m_GpuHelper{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    FilePath m_Path;
    std::vector<std::byte> m_TexBytes;
    std::optional<TextureHelper::TextureBuilder> m_TextureBuilder;
    wgpu::Texture m_Texture{ nullptr };
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };
    Result<> m_DecodeResult;

    std::atomic<bool> m_CompletionFlag{ false };

    Stage m_Stage{ Stage::None };
};

/// Task responsible for fetching and decoding a texture.
/// Callers should repeatedly call FileFetcher::ProcessCompletions() while this task is running.
/// A wgpu::CommandEncoder must be provided to record the necessary GPU commands for texture upload.
class TextureFetchTask : public ICoopTask<>
{
public:
    TextureFetchTask(const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        ThreadPool& threadPool,
        const FilePath& path,
        wgpu::CommandEncoder commandEncoder);

    TextureFetchTask() = delete;
    ~TextureFetchTask() override = default; // The base class will verify completion
    TextureFetchTask(const TextureFetchTask&) = delete;
    TextureFetchTask& operator=(const TextureFetchTask&) = delete;
    TextureFetchTask(TextureFetchTask&&) = delete;
    TextureFetchTask& operator=(TextureFetchTask&&) = delete;

    Result<wgpu::Texture> Take();

private:
    enum class Stage
    {
        None,
        Fetching,
        Decoding,
        Succeeded,
        Failed
    };

    Result<> OnStart() override;

    void OnUpdate() override;

    const GpuHelper* m_GpuHelper{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    FilePath m_Path;
    FetchRequestId m_FetchRequestId;
    wgpu::Texture m_Texture{ nullptr };
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };

    std::optional<TextureDecodeTask> m_TextureDecodeTask;

    Stage m_Stage{ Stage::None };
};