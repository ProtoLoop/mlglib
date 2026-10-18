#pragma once

#include "CoopTask.h"
#include "GpuTypes.h"
#include "TextureFormat.h"
#include "VecMath.h"

#include <atomic>
#include <memory>
#include <string_view>

struct SDL_Window;
using SDL_MetalView = void*;

class GpuHelper final
{
public:
    /// The preferred texture format for render targets.
    static constexpr wgpu::TextureFormat kRenderTargetFormat = wgpu::TextureFormat::RGBA8Unorm;
    static constexpr wgpu::TextureFormat kDepthBufferFormat = wgpu::TextureFormat::Depth24Plus;

    /// The alignment requirement for a row of texture data in bytes when uploading to the GPU.
    static constexpr size_t kTextureStrideAlignment = 256;

    /// A task that creates a GpuHelper instance asynchronously.
    class CreateTask;

    GpuHelper() = delete;
    ~GpuHelper();
    GpuHelper(const GpuHelper&) = delete;
    GpuHelper& operator=(const GpuHelper&) = delete;
    GpuHelper(GpuHelper&&) = delete;
    GpuHelper& operator=(GpuHelper&&) = delete;

    SDL_Window* GetWindow() const;
    const wgpu::Instance& GetInstance() const;
    const wgpu::Device& GetDevice() const;
    const wgpu::Texture& GetDefaultTexture() const;
    Dimension2 GetScreenDimensions() const;
    Result<GpuRenderTarget> GetSwapChainTexture() const;
    wgpu::TextureFormat GetSwapChainFormat() const;
    bool HasTextureFormat(TextureFormat::FormatIdentifier formatId) const;

    /// Presents the current swap chain texture to the screen.
    Result<> Present() const;

    /// Resizes the swap chain to the given width and height.
    Result<> Resize(const uint32_t width, const uint32_t height);

    /// Creates a render target with the given dimensions and name.
    Result<GpuRenderTarget> CreateRenderTarget(
        const uint32_t width, const uint32_t height, const std::string_view name) const;

    /// Creates a depth buffer with the given dimensions and name.
    Result<GpuDepthTarget> CreateDepthBuffer(
        const uint32_t width, const uint32_t height, const std::string_view name) const;

    /// Creates a staging buffer for copying texture data to the GPU.
    Result<GpuTextureStagingBuffer> CreateTextureStagingBuffer(const uint32_t width,
        const uint32_t height,
        const uint32_t mipCount,
        const TextureFormat& format,
        const std::string_view name) const;

    /// Creates a vertex buffer with capacity for the given number of vertices.
    Result<GpuVertexBuffer> CreateVertexBuffer(const size_t count,
        const std::string_view name) const;

    /// Creates an index buffer with capacity for the given number of indices.
    Result<GpuIndexBuffer> CreateIndexBuffer(const size_t count,
        const std::string_view name) const;

    /// Creates a semantically-typed storage buffer.
    template<typename T>
    Result<T> CreateStorageBuffer(const size_t count, const std::string_view name) const
    {
        static_assert(is_gpu_storage_buffer_type_v<T>,
            "T must be a GpuBuffer type with GpuBufferUsage::Storage");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateStorageBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T::Create(GetDevice(), *bufferResult);
    }

    /// Creates a semantically-typed uniform buffer.
    template<typename T>
    Result<T> CreateUniformBuffer(const size_t count, const std::string_view name) const
    {
        static_assert(is_gpu_uniform_buffer_type_v<T>,
            "T must be a GpuBuffer type with GpuBufferUsage::Uniform");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateUniformBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T::Create(GetDevice(), *bufferResult);
    }

    /// Creates a semantically-typed indirect buffer.
    template<typename T>
    Result<T> CreateIndirectBuffer(const size_t count, const std::string_view name) const
    {
        static_assert(is_gpu_indirect_buffer_type_v<T>,
            "T must be a GpuBuffer type with GpuBufferUsage::Indirect");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateIndirectBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T::Create(GetDevice(), *bufferResult);
    }

private:
    GpuHelper(SDL_Window* window,
        SDL_MetalView metalView,
        wgpu::Instance instance,
        wgpu::Adapter adapter,
        wgpu::Device device,
        wgpu::Surface surface,
        wgpu::TextureFormat surfaceFormat);

    Result<wgpu::Buffer> CreateIndirectBuffer(const size_t size, const std::string_view name) const;
    Result<wgpu::Buffer> CreateStorageBuffer(const size_t size, const std::string_view name) const;
    Result<wgpu::Buffer> CreateUniformBuffer(const size_t size, const std::string_view name) const;

    SDL_Window* m_Window{ nullptr };
    SDL_MetalView m_MetalView{ nullptr };
    wgpu::Instance m_Instance{ nullptr };
    wgpu::Adapter m_Adapter{ nullptr };
    wgpu::Device m_Device{ nullptr };
    wgpu::Surface m_Surface{ nullptr };
    mutable wgpu::TextureFormat m_SurfaceFormat{ wgpu::TextureFormat::Undefined };
    wgpu::Texture m_DefaultTexture{ nullptr };
};

class GpuHelper::CreateTask : public ICoopTask<std::string_view>
{
public:
    // Passed to the adapter request callback to store the result of the request.
    struct AdapterRequestData
    {
        Result<WGPUAdapter> Result;
        std::atomic<bool> IsComplete{ false };
    };

    // Passed to the device request callback to store the result of the request.
    struct DeviceRequestData
    {
        Result<WGPUDevice> Result;
        std::atomic<bool> IsComplete{ false };
    };

    CreateTask() = default;
    ~CreateTask() override;
    CreateTask(const CreateTask&) = delete;
    CreateTask& operator=(const CreateTask&) = delete;
    CreateTask(CreateTask&&) = delete;
    CreateTask& operator=(CreateTask&&) = delete;

    Result<std::unique_ptr<GpuHelper>> Take();

private:
    friend GpuHelper;

    enum class Stage
    {
        None,
        CreateAdapter,
        CreatingAdapter,
        CreatingDevice,
        Succeeded,
        Failed
    };

    Result<> OnStart(const std::string_view appName) override;

    void OnUpdate() override;

    Result<> CreateAdapter();
    Result<> FinalizeAdapter();
    Result<> CreateDevice();
    Result<> FinalizeDevice();
    Result<> Configure();

    AdapterRequestData m_AdapterRequestData;
    DeviceRequestData m_DeviceRequestData;

    SDL_Window* m_Window{ nullptr };
    SDL_MetalView m_MetalView{ nullptr };
    wgpu::Instance m_Instance{ nullptr };
    wgpu::Adapter m_Adapter{ nullptr };
    wgpu::Device m_Device{ nullptr };
    wgpu::Surface m_Surface{ nullptr };
    mutable wgpu::TextureFormat m_SurfaceFormat{ wgpu::TextureFormat::Undefined };

    Stage m_Stage{ Stage::None };

    bool m_Consumed{ false };
};