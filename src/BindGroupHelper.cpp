#include "BindGroupHelper.h"

namespace detail
{
Result<wgpu::BindGroupLayout>
CreateBindGroupLayout(const wgpu::Device& gpuDevice,
    const std::span<const wgpu::BindGroupLayoutEntry> entries,
    const std::string_view label)
{
    const wgpu::BindGroupLayoutDescriptor bgLayoutDesc //
        {
            .label = label,
            .entryCount = entries.size(),
            .entries = entries.data(),
        };

    wgpu::BindGroupLayout layout = gpuDevice.CreateBindGroupLayout(&bgLayoutDesc);
    MLG_CHECK(layout, "Failed to create bind group layout");

    return layout;
}

Result<wgpu::BindGroup>
CreateBindGroup(const wgpu::Device& gpuDevice,
    const wgpu::BindGroupLayout& layout,
    const std::span<const wgpu::BindGroupEntry> entries,
    const std::string_view label)
{
    const wgpu::BindGroupDescriptor bgDesc //
        {
            .label = label,
            .layout = layout,
            .entryCount = entries.size(),
            .entries = entries.data(),
        };

    wgpu::BindGroup bindGroup = gpuDevice.CreateBindGroup(&bgDesc);
    MLG_CHECK(bindGroup, "Failed to create bind group");

    return bindGroup;
}
} // namespace detail