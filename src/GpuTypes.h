#pragma once

#include "AssertHelper.h"
#include "Result.h"
#include "shaders/ShaderInterop.h"
#include "TextureFormat.h"
#include "Vertex.h"

#include <string_view>
#include <type_traits>
#include <webgpu/webgpu_cpp.h>

class GpuHelper;

/// Owns a mapped staging buffer and metadata for creating a GPU texture.
/// Create, move, commit, and destroy this object on the main thread.
/// StageBytes() and StageMipBytes() may run on a worker with exclusive access to the object and its
/// mapped memory. That work must finish before moving, committing, or destroying it.
///
/// Commit() creates and returns the GPU texture, consumes the staging buffer,
/// and invalidates all previously returned mapped spans.
class GpuTextureStagingBuffer
{
public:
    GpuTextureStagingBuffer() = delete;
    ~GpuTextureStagingBuffer() = default;
    GpuTextureStagingBuffer(const GpuTextureStagingBuffer&) = delete;
    GpuTextureStagingBuffer& operator=(const GpuTextureStagingBuffer&) = delete;
    GpuTextureStagingBuffer(GpuTextureStagingBuffer&&) = default;
    GpuTextureStagingBuffer& operator=(GpuTextureStagingBuffer&&) = default;

    /// Returns a span representing the mapped memory of the staging buffer.
    Result<std::span<std::byte>> GetMappedRange();

    /// Copies tightly packed mip levels, ordered from largest to smallest, into the
    /// staging buffer. Source rows must not include upload-alignment padding.
    /// Adds the required row padding separately for each mip level.
    Result<> StageBytes(const std::span<const std::byte> srcBytes);

    /// Copies one tightly packed mip into its staging range, adding padding if necessary.
    Result<> StageMipBytes(uint32_t mipLevel, std::span<const std::byte> srcBytes);

    /// Commits staged bytes to the GPU texture.
    /// Internally creates a command encoder and submits the resulting command buffer to the GPU.
    /// After calling this function, the staging buffer's mapped memory is no longer valid.
    Result<wgpu::Texture> Commit();

    /// Commits staged bytes to the GPU texture using the provided command encoder.
    /// The caller is responsible submitting the result command buffer to the GPU.
    /// After calling this function, the staging buffer's mapped memory is no longer valid.
    Result<wgpu::Texture> Commit(wgpu::CommandEncoder cmdEncoder);

private:
    friend class GpuHelper;

    GpuTextureStagingBuffer(const GpuHelper& gpuHelper,
        wgpu::Buffer stagingBuffer,
        const uint32_t width,
        const uint32_t height,
        const uint32_t mipCount,
        const TextureFormat& format,
        const std::string_view name);

    const GpuHelper* m_GpuHelper{ nullptr };
    wgpu::Buffer m_StagingBuffer;
    uint32_t m_Width{ 0 };
    uint32_t m_Height{ 0 };
    uint32_t m_MipCount{ 0 };
    std::span<std::byte> m_MappedMemory;
    TextureFormat m_Format;
    InplaceString<> m_Name;
};

template<typename Tag>
class GpuTextureTarget
{
public:
    GpuTextureTarget() = delete;
    ~GpuTextureTarget() = default;
    GpuTextureTarget(const GpuTextureTarget&) = default;
    GpuTextureTarget& operator=(const GpuTextureTarget&) = default;

    // Because GpuRenderTarget guarantees validity of the underlying texture we must
    // not allow resource stealing.  Therefore move ctor and assignment delegate
    // to the copy ctor and assignment operator.

    GpuTextureTarget(GpuTextureTarget&& other) noexcept
        : GpuTextureTarget(static_cast<const GpuTextureTarget&>(other))
    {
    }

    GpuTextureTarget& operator=(GpuTextureTarget&& other) noexcept
    {
        if(this != &other)
        {
            return *this = static_cast<const GpuTextureTarget&>(other);
        }
        return *this;
    }

    const wgpu::Texture& GetTexture() const { return m_Texture; }

    const wgpu::Texture* operator->() const { return &m_Texture; }

    friend bool operator==(const GpuTextureTarget& a, const GpuTextureTarget& b)
    {
        return a.m_Texture.Get() == b.m_Texture.Get();
    }

private:
    friend class GpuHelper;

    explicit GpuTextureTarget(wgpu::Texture gpuObject)
        : m_Texture(std::move(gpuObject))
    {
        MLG_ASSERT(m_Texture, "Invalid GPU object");
    }

    wgpu::Texture m_Texture;
};

using GpuRenderTarget = GpuTextureTarget<struct RenderTarget>;
using GpuDepthTarget = GpuTextureTarget<struct DepthTarget>;

template<typename T>
class GpuSimpleBuffer
{
    static constexpr size_t GetMinSize();

public:
    static constexpr size_t min_size = GetMinSize();

    GpuSimpleBuffer() = delete;
    ~GpuSimpleBuffer() = default;
    GpuSimpleBuffer(const GpuSimpleBuffer&) = default;
    GpuSimpleBuffer& operator=(const GpuSimpleBuffer&) = default;

    // Because GpuBuffer guarantess validity of the underlying buffer we must
    // not allow resource stealing.  Therefore move ctor and assignment delegate
    // to the copy ctor and assignment operator.

    GpuSimpleBuffer(GpuSimpleBuffer&& other) noexcept
        : GpuSimpleBuffer(static_cast<const GpuSimpleBuffer&>(other))
    {
    }
    GpuSimpleBuffer& operator=(GpuSimpleBuffer&& other) noexcept
    {
        return *this = static_cast<const GpuSimpleBuffer&>(other);
    }

    static Result<GpuSimpleBuffer<T>> Create(wgpu::Buffer buffer)
    {
        MLG_CHECK(buffer, "Invalid GPU buffer");
        return GpuSimpleBuffer<T>(std::move(buffer));
    }

    const wgpu::Buffer& GetBuffer() const { return m_Buffer; }

    size_t GetSize() const { return static_cast<size_t>(m_Buffer.GetSize()); }

    bool IsValid() const { return m_Buffer.Get(); }

    explicit operator wgpu::Buffer() const { return m_Buffer; }

    friend bool operator==(const GpuSimpleBuffer& a, const GpuSimpleBuffer& b)
    {
        return a.m_Buffer.Get() == b.m_Buffer.Get();
    }

    friend bool operator!=(const GpuSimpleBuffer& a, const GpuSimpleBuffer& b) { return !(a == b); }

private:
    explicit GpuSimpleBuffer(wgpu::Buffer buffer)
        : m_Buffer(std::move(buffer))
    {
    }

    wgpu::Buffer m_Buffer;
};

/// A strongly-typed GPU buffer that wraps a wgpu::Buffer, guarantees
/// its validity, and provides type-safe access to its contents.
template<typename T, wgpu::BufferUsage BufferUsage>
class GpuBuffer
{
    static_assert(std::is_trivially_copyable_v<T>,
        "GpuBuffer can only be used with trivially copyable types");
    static_assert(!std::is_pointer_v<T>, "GpuBuffer can only be used with non-pointer types");
    static_assert(!std::is_reference_v<T>, "GpuBuffer can only be used with non-reference types");

public:
    using value_type = T;
    static constexpr size_t min_size = sizeof(T);
    static constexpr wgpu::BufferUsage usage = BufferUsage;

    GpuBuffer() = delete;
    ~GpuBuffer() = default;
    GpuBuffer(const GpuBuffer&) = default;
    GpuBuffer& operator=(const GpuBuffer&) = default;

    // Because GpuBuffer guarantess validity of the underlying buffer we must
    // not allow resource stealing.  Therefore move ctor and assignment delegate
    // to the copy ctor and assignment operator.

    GpuBuffer(GpuBuffer&& other) noexcept
        : GpuBuffer(static_cast<const GpuBuffer&>(other))
    {
    }
    GpuBuffer& operator=(GpuBuffer&& other) noexcept
    {
        return *this = static_cast<const GpuBuffer&>(other);
    }

    static Result<GpuBuffer> Create(wgpu::Device gpuDevice, wgpu::Buffer buffer)
    {
        MLG_CHECKV(gpuDevice, "Invalid wgpu::Device");
        MLG_CHECKV(buffer, "Invalid wgpu::Buffer");

        return GpuBuffer(std::move(gpuDevice), std::move(buffer));
    }

    const wgpu::Buffer& GetBuffer() const { return m_Buffer; }

    size_t GetSize() const { return static_cast<size_t>(m_Buffer.GetSize()); }

    size_t Count() const { return GetSize() / sizeof(T); }

    // Stores an array of values starting at the given index.
    void Store(size_t index, std::span<const T> values)
    {
        const size_t offset = index * sizeof(T);

        MLG_ASSERT((offset + (values.size() * sizeof(T))) <= GetSize(), "Index out of bounds");

        m_Device.GetQueue().WriteBuffer(GetBuffer(),
            offset,
            values.data(),
            values.size() * sizeof(T));
    }

    // Stores an array of values starting at the zero index.
    void Store(std::span<const T> values) { Store(0, values); }

    bool IsValid() const { return m_Device && m_Buffer; }

    explicit operator wgpu::Buffer() const { return m_Buffer; }

    friend bool operator==(const GpuBuffer& a, const GpuBuffer& b)
    {
        return a.m_Device.Get() == b.m_Device.Get() && a.m_Buffer.Get() == b.m_Buffer.Get();
    }

    friend bool operator!=(const GpuBuffer& a, const GpuBuffer& b) { return !(a == b); }

private:
    GpuBuffer(wgpu::Device gpuDevice, wgpu::Buffer buffer)
        : m_Device(std::move(gpuDevice)),
          m_Buffer(std::move(buffer))
    {
    }

    wgpu::Device m_Device{ nullptr };
    wgpu::Buffer m_Buffer{ nullptr };
};

// Strongly-typed GPU storage buffer classes.
using GpuVertexBuffer = GpuBuffer<Vertex, wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst>;
using GpuIndexBuffer =
    GpuBuffer<VertexIndex, wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst>;
using GpuDrawIndirectBuffer = GpuBuffer<ShaderInterop::DrawIndirectParams,
    wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst>;
using GpuWorldTransformBuffer = GpuBuffer<ShaderInterop::WorldTransform,
    wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst>;
using GpuClipSpaceTransformBuffer = GpuBuffer<ShaderInterop::ClipSpaceTransform,
    wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst>;
using GpuMeshInstanceParamsBuffer = GpuBuffer<ShaderInterop::MeshInstanceParams,
    wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst>;
using GpuCameraParamsBuffer =
    GpuBuffer<ShaderInterop::CameraParams, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst>;
using GpuMaterialConstantsBuffer = GpuBuffer<ShaderInterop::MaterialConstants,
    wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst>;

/// This gives strongly-typed GPU buffers for point and directional lights.
/// Because light buffers have a header we can't simply use GpuBuffer because
/// is expects a tightly-packed array of elements without any headers.
using GpuPointLightBuffer = GpuSimpleBuffer<struct PointLightTag>;
using GpuDirectionalLightBuffer = GpuSimpleBuffer<struct DirectionalLightTag>;

template<>
constexpr size_t
GpuSimpleBuffer<struct PointLightTag>::GetMinSize()
{
    return ShaderInterop::PointLightBufferHeader::CalcBufferSize(0);
}

template<>
constexpr size_t
GpuSimpleBuffer<struct DirectionalLightTag>::GetMinSize()
{
    return ShaderInterop::DirectionalLightBufferHeader::CalcBufferSize(0);
}

inline bool
operator==(const wgpu::Texture& a, const wgpu::Texture& b)
{
    return a.Get() == b.Get();
}

inline bool
operator==(const wgpu::Sampler& a, const wgpu::Sampler& b)
{
    return a.Get() == b.Get();
}