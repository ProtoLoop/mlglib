#pragma once

#include "AssertHelper.h"
#include "Result.h"
#include "shaders/ShaderInterop.h"
#include "Vertex.h"

#include <string_view>
#include <type_traits>
#include <webgpu/webgpu_cpp.h>

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

    const wgpu::Texture& Get() const { return m_Texture; }

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

/// Identifies the intended usage of a GpuBuffer.
enum class GpuBufferUsage
{
    Vertex,
    Index,
    Indirect,
    Uniform,
    Storage
};

/// A strongly-typed GPU buffer that wraps a wgpu::Buffer, guarantees
/// its validity, and provides type-safe access to its contents.
template<typename T, GpuBufferUsage BufferUsage>
class GpuBuffer
{
    static_assert(std::is_trivially_copyable_v<T>,
        "GpuBuffer can only be used with trivially copyable types");
    static_assert(!std::is_pointer_v<T>, "GpuBuffer can only be used with non-pointer types");
    static_assert(!std::is_reference_v<T>, "GpuBuffer can only be used with non-reference types");

public:
    using value_type = T;

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
        if(this != &other)
        {
            return *this = static_cast<const GpuBuffer&>(other);
        }
        return *this;
    }

    static Result<GpuBuffer> Create(wgpu::Device gpuDevice, wgpu::Buffer buffer)
    {
        MLG_CHECKV(gpuDevice, "Invalid wgpu::Device");
        MLG_CHECKV(buffer, "Invalid wgpu::Buffer");

        return GpuBuffer(std::move(gpuDevice), std::move(buffer));
    }

    const wgpu::Buffer& GetGpuBuffer() const { return m_GpuBuffer; }

    size_t BufferSize() const { return static_cast<size_t>(m_GpuBuffer.GetSize()); }

    size_t Count() const { return BufferSize() / sizeof(T); }

    // Stores a single value at the given index.
    void Store(size_t index, const T& value) { Store(index, std::span<const T>(&value, 1)); }

    // Stores an array of values starting at the given index.
    void Store(size_t index, std::span<const T> values)
    {
        const size_t offset = index * sizeof(T);

        MLG_ASSERT((offset + (values.size() * sizeof(T))) <= BufferSize(), "Index out of bounds");

        m_Device.GetQueue().WriteBuffer(GetGpuBuffer(),
            offset,
            values.data(),
            values.size() * sizeof(T));
    }

    // Stores an array of values starting at the zero index.
    void Store(std::span<const T> values) { Store(0, values); }

    friend bool operator==(const GpuBuffer& a, const GpuBuffer& b)
    {
        return a.m_Device.Get() == b.m_Device.Get() && a.m_GpuBuffer.Get() == b.m_GpuBuffer.Get();
    }

private:
    GpuBuffer(wgpu::Device gpuDevice, wgpu::Buffer buffer)
        : m_Device(std::move(gpuDevice)),
          m_GpuBuffer(std::move(buffer))
    {
    }

    wgpu::Device m_Device{ nullptr };
    wgpu::Buffer m_GpuBuffer{ nullptr };
};

/// Type traits to determine if a type is a GpuBuffer of a specific buffer type.
#define MLG_DEFINE_GPU_BUFFER_TRAITS(typeName, bufferUsage)                                        \
    template<typename T>                                                                           \
    struct is_gpu_##typeName##_buffer_type : std::false_type                                       \
    {                                                                                              \
    };                                                                                             \
    template<typename T>                                                                           \
    struct is_gpu_##typeName##                                                                     \
        _buffer_type<GpuBuffer<T, GpuBufferUsage::bufferUsage>> : std::true_type                   \
    {                                                                                              \
    };                                                                                             \
    template<typename T>                                                                           \
    inline constexpr bool is_gpu_##typeName##_buffer_type_v =                                      \
        is_gpu_##typeName##_buffer_type<T>::value;

MLG_DEFINE_GPU_BUFFER_TRAITS(vertex, Vertex)
MLG_DEFINE_GPU_BUFFER_TRAITS(index, Index)
MLG_DEFINE_GPU_BUFFER_TRAITS(indirect, Indirect)
MLG_DEFINE_GPU_BUFFER_TRAITS(uniform, Uniform)
MLG_DEFINE_GPU_BUFFER_TRAITS(storage, Storage)

// Strongly-typed GPU storage buffer classes.
using GpuVertexBuffer = GpuBuffer<Vertex, GpuBufferUsage::Vertex>;
using GpuIndexBuffer = GpuBuffer<VertexIndex, GpuBufferUsage::Index>;
using GpuDrawIndirectBuffer =
    GpuBuffer<ShaderInterop::DrawIndirectParams, GpuBufferUsage::Indirect>;
using GpuWorldTransformBuffer = GpuBuffer<ShaderInterop::WorldTransform, GpuBufferUsage::Storage>;
using GpuClipSpaceBuffer = GpuBuffer<ShaderInterop::ClipSpaceTransform, GpuBufferUsage::Storage>;
using GpuMeshInstanceParamsBuffer =
    GpuBuffer<ShaderInterop::MeshInstanceParams, GpuBufferUsage::Storage>;
using GpuCameraParamsBuffer = GpuBuffer<ShaderInterop::CameraParams, GpuBufferUsage::Uniform>;
using GpuMaterialConstantsBuffer =
    GpuBuffer<ShaderInterop::MaterialConstants, GpuBufferUsage::Uniform>;

inline bool
operator==(const wgpu::Texture& a, const wgpu::Texture& b)
{
    return a.Get() == b.Get();
}