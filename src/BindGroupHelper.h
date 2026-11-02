#pragma once

#include "GpuTypes.h"

#include <tuple>
#include <array>
#include <webgpu/webgpu_cpp.h>

/// Provides templated structures for defining GPU bind group layouts and entries.
/// Includes BufferBinding, TextureBinding, SamplerBinding, and the BindGroup class.

/// Represents a GPU buffer (wgpu::Buffer)binding with a specific type and visibility.
template<typename T, wgpu::BufferBindingType BindingType, wgpu::ShaderStage Visibility>
struct BufferBinding
{
    using ResourceType = T;

    /// Returns a bind group layout entry for this buffer binding at the specified slot.
    /// The slot corresponds to the binding index in the bind group layout.
    static constexpr wgpu::BindGroupLayoutEntry MakeLayoutEntry(const uint32_t slot)
    {
        return wgpu::BindGroupLayoutEntry//
        {
            .binding = slot,
            .visibility = Visibility,
            .buffer =
            {
                .type = BindingType,
                .minBindingSize = ResourceType::min_size,
            },
        };
    }

    /// Returns a bind group entry for this buffer binding at the specified slot.
    /// The slot corresponds to the binding index in the bind group.
    wgpu::BindGroupEntry MakeBindGroupEntry(ResourceType buffer, const uint32_t slot) const
    {
        return wgpu::BindGroupEntry //
            {
                .binding = slot,
                .buffer = wgpu::Buffer(buffer),
                .offset = 0,
                .size = buffer.GetSize(),
            };
    }

    bool IsValid() const { return Resource.IsValid(); }

    friend bool operator==(const BufferBinding& lhs, const BufferBinding& rhs)
    {
        return lhs.Resource == rhs.Resource;
    }

    friend bool operator!=(const BufferBinding& lhs, const BufferBinding& rhs)
    {
        return !(lhs == rhs);
    }

    /// The GPU buffer resource associated with this binding.
    ResourceType Resource;
};

/// Represents a GPU texture (wgpu::Texture) binding with a specific visibility.
template<wgpu::ShaderStage Visibility>
struct TextureBinding
{
    using ResourceType = wgpu::Texture;

    /// Returns a bind group layout entry for this texture binding at the specified slot.
    /// The slot corresponds to the binding index in the bind group layout.
    static constexpr wgpu::BindGroupLayoutEntry MakeLayoutEntry(const uint32_t slot)
    {
        return wgpu::BindGroupLayoutEntry//
        {
            .binding = slot,
            .visibility = Visibility,
            .texture =
            {
                .sampleType = wgpu::TextureSampleType::Float,
                .viewDimension = wgpu::TextureViewDimension::e2D,
                .multisampled = false,
            },
        };
    }

    /// Returns a bind group entry for this texture binding at the specified slot.
    /// The slot corresponds to the binding index in the bind group.
    wgpu::BindGroupEntry MakeBindGroupEntry(const wgpu::Texture& texture, const uint32_t slot) const
    {
        return wgpu::BindGroupEntry //
            {
                .binding = slot,
                .textureView = texture.CreateView(),
            };
    }

    bool IsValid() const { return Resource.Get() != nullptr; }

    friend bool operator==(const TextureBinding& lhs, const TextureBinding& rhs)
    {
        return lhs.Resource == rhs.Resource;
    }

    friend bool operator!=(const TextureBinding& lhs, const TextureBinding& rhs)
    {
        return !(lhs == rhs);
    }

    /// The GPU texture resource associated with this binding.
    ResourceType Resource;
};

/// Represents a GPU texture (wgpu::Texture) binding that is used as a depth buffer,
/// e.g. a shadow map depth texture.
template<wgpu::ShaderStage Visibility>
struct DepthBufferBinding
{
    using ResourceType = wgpu::Texture;

    /// Returns a bind group layout entry for this texture binding at the specified slot.
    /// The slot corresponds to the binding index in the bind group layout.
    static constexpr wgpu::BindGroupLayoutEntry MakeLayoutEntry(const uint32_t slot)
    {
        return wgpu::BindGroupLayoutEntry//
        {
            .binding = slot,
            .visibility = Visibility,
            .texture =
            {
                .sampleType = wgpu::TextureSampleType::Depth,
                .viewDimension = wgpu::TextureViewDimension::e2D,
                .multisampled = false,
            },
        };
    }

    /// Returns a bind group entry for this texture binding at the specified slot.
    /// The slot corresponds to the binding index in the bind group.
    wgpu::BindGroupEntry MakeBindGroupEntry(const wgpu::Texture& texture, const uint32_t slot) const
    {
        return wgpu::BindGroupEntry //
            {
                .binding = slot,
                .textureView = texture.CreateView(),
            };
    }

    bool IsValid() const { return Resource.Get() != nullptr; }

    friend bool operator==(const DepthBufferBinding& lhs, const DepthBufferBinding& rhs)
    {
        return lhs.Resource == rhs.Resource;
    }

    friend bool operator!=(const DepthBufferBinding& lhs, const DepthBufferBinding& rhs)
    {
        return !(lhs == rhs);
    }

    /// The GPU texture resource associated with this binding.
    ResourceType Resource;
};

/// Represents a GPU sampler (wgpu::Sampler) binding with a specific visibility.
template<wgpu::ShaderStage Visibility>
struct TextureSamplerBinding
{
    using ResourceType = wgpu::Sampler;

    /// Returns a bind group layout entry for this sampler binding at the specified slot.
    /// The slot corresponds to the binding index in the bind group layout.
    static constexpr wgpu::BindGroupLayoutEntry MakeLayoutEntry(const uint32_t slot)
    {
        return wgpu::BindGroupLayoutEntry//
        {
            .binding = slot,
            .visibility = Visibility,
            .sampler =
            {
                .type = wgpu::SamplerBindingType::Filtering,
            },
        };
    }

    /// Returns a bind group entry for this sampler binding at the specified slot.
    /// The slot corresponds to the binding index in the bind group.
    wgpu::BindGroupEntry MakeBindGroupEntry(const wgpu::Sampler& sampler, const uint32_t slot) const
    {
        return wgpu::BindGroupEntry //
            {
                .binding = slot,
                .sampler = sampler,
            };
    }

    bool IsValid() const { return Resource.Get() != nullptr; }


    friend bool operator==(const TextureSamplerBinding& lhs, const TextureSamplerBinding& rhs)
    {
        return lhs.Resource == rhs.Resource;
    }

    friend bool operator!=(const TextureSamplerBinding& lhs, const TextureSamplerBinding& rhs)
    {
        return !(lhs == rhs);
    }

    /// The GPU sampler resource associated with this binding.
    ResourceType Resource;
};

/// Represents a GPU sampler (wgpu::Sampler) binding that is used to sample depth textures.
template<wgpu::ShaderStage Visibility>
struct DepthSamplerBinding
{
    using ResourceType = wgpu::Sampler;

    /// Returns a bind group layout entry for this sampler binding at the specified slot.
    /// The slot corresponds to the binding index in the bind group layout.
    static constexpr wgpu::BindGroupLayoutEntry MakeLayoutEntry(const uint32_t slot)
    {
        return wgpu::BindGroupLayoutEntry//
        {
            .binding = slot,
            .visibility = Visibility,
            .sampler =
            {
                .type = wgpu::SamplerBindingType::Comparison,
            },
        };
    }

    /// Returns a bind group entry for this sampler binding at the specified slot.
    /// The slot corresponds to the binding index in the bind group.
    wgpu::BindGroupEntry MakeBindGroupEntry(const wgpu::Sampler& sampler, const uint32_t slot) const
    {
        return wgpu::BindGroupEntry //
            {
                .binding = slot,
                .sampler = sampler,
            };
    }

    bool IsValid() const { return Resource.Get() != nullptr; }


    friend bool operator==(const DepthSamplerBinding& lhs, const DepthSamplerBinding& rhs)
    {
        return lhs.Resource == rhs.Resource;
    }

    friend bool operator!=(const DepthSamplerBinding& lhs, const DepthSamplerBinding& rhs)
    {
        return !(lhs == rhs);
    }

    /// The GPU sampler resource associated with this binding.
    ResourceType Resource;
};

namespace detail
{
Result<wgpu::BindGroupLayout> CreateBindGroupLayout(const wgpu::Device& gpuDevice,
    const std::span<const wgpu::BindGroupLayoutEntry> entries,
    const std::string_view label);

Result<wgpu::BindGroup> CreateBindGroup(const wgpu::Device& gpuDevice,
    const wgpu::BindGroupLayout& layout,
    const std::span<const wgpu::BindGroupEntry> entries,
    const std::string_view label);
} // namespace detail

/// Represents a GPU bind group composed of multiple bindings.
/// The template parameter pack `Bindings` specifies the types of bindings included in the group.
template<class... Bindings>
class BindGroupHelper
{
    static consteval auto MakeLayoutEntries()
    {
        uint32_t index = 0;
        return std::array{ Bindings::MakeLayoutEntry(index++)... };
    }

    constexpr auto MakeBindGroupEntries() const
    {
        return std::apply(
            [&](const auto&... binding)
            {
                uint32_t index = 0;
                return std::array{ binding.MakeBindGroupEntry(binding.Resource, index++)... };
            },
            m_Bindings);
    }

public:
    /// Layout entries used for constructing the bind group layout.
    static constexpr auto LayoutEntries = MakeLayoutEntries();

    BindGroupHelper() = delete;

    /// Creates a bind group with the specified GPU resources.
    static Result<BindGroupHelper> Create(Bindings::ResourceType... resources)
    {
        const BindingsTuple bindings(std::make_tuple(Bindings{ .Resource = std::move(resources) }...));
        MLG_CHECK(Validate(bindings), "Failed to create bind group - invalid resources");

        return BindGroupHelper(bindings);
    }

    /// Returns the number of bindings in the bind group.
    static constexpr size_t size() { return std::tuple_size_v<BindingsTuple>; }

    /// Returns the array of bind group entries used for constructing the bind group.
    const std::array<wgpu::BindGroupEntry, sizeof...(Bindings)>& GetBindGroupEntries() const
    {
        return m_BindGroupEntries;
    }

    /// Creates a bind wgpu::BindGroupLayout.
    static Result<wgpu::BindGroupLayout> CreateBindGroupLayout(const wgpu::Device& gpuDevice,
        const std::string_view label)
    {
        return detail::CreateBindGroupLayout(gpuDevice, LayoutEntries, label);
    }

    Result<wgpu::BindGroup> CreateBindGroup(const wgpu::Device& gpuDevice,
        const wgpu::BindGroupLayout& layout,
        const std::string_view label) const 
    {
        MLG_CHECKV(Validate(m_Bindings), "Invalid bind group resources");
        return detail::CreateBindGroup(gpuDevice, layout, m_BindGroupEntries, label);
    }

    /// Returns the GPU resource associated with the specified binding type.

    template<typename RsrcType>
    const RsrcType& GetResource() const
    {
        // Creates a tuple of the resources from each binding and retrieves the one matching RsrcType.
        return std::apply([](const auto&... binding) -> const RsrcType&
            { return std::get<const RsrcType&>(std::tie(binding.Resource...)); },
            m_Bindings);
    }

    friend bool operator==(const BindGroupHelper& lhs, const BindGroupHelper& rhs)
    {
        return lhs.m_Bindings == rhs.m_Bindings;
    }

    friend bool operator!=(const BindGroupHelper& lhs, const BindGroupHelper& rhs)
    {
        return !(lhs == rhs);
    }

private:
    using BindingsTuple = std::tuple<Bindings...>;

    explicit BindGroupHelper(BindingsTuple bindings)
        : m_Bindings(std::move(bindings)),
          m_BindGroupEntries(MakeBindGroupEntries())
    {
    }

    /// Validates that all bindings in the bind group have valid GPU resources.
    static bool Validate(const BindingsTuple& binding)
    {
        return std::apply([](const auto&... b) { return (b.IsValid() && ...); }, binding);
    }

    BindingsTuple m_Bindings;
    std::array<wgpu::BindGroupEntry, sizeof...(Bindings)> m_BindGroupEntries;
};