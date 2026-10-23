#pragma once

#include "InplaceString.h"
#include "Result.h"

#include <string_view>
#include <webgpu/webgpu_cpp.h>

/// Encapsulates information about a texture format, including its compression type, block size, and
/// encoding.
class TextureFormat final
{
public:
    static constexpr uint32_t kMaxWidth = 4096;
    static constexpr uint32_t kMaxHeight = 4096;
    static constexpr uint32_t kDefaultChannelCount = 4;

    static_assert(std::vector<std::byte>().max_size() / kMaxWidth >= kMaxHeight,
        "Maximum image dimensions out of bounds");

    static constexpr size_t kMaxPixelCount = static_cast<size_t>(kMaxWidth) * kMaxHeight;

    static_assert(std::vector<std::byte>().max_size() / kMaxPixelCount >= kDefaultChannelCount,
        "Maximum image dimensions out of bounds for the number of texture channels");

    static constexpr size_t kMaxBytesPerRow = static_cast<size_t>(kMaxWidth) * kDefaultChannelCount;
    static constexpr size_t kMaxByteCount = kMaxBytesPerRow * kMaxHeight;

    enum class FormatIdentifier
    {
        Unknown,
        RGBA,
        BC7,
        ASTC
    };

    /// Represents the file extension used for files that contain textures of this format.
    class FileExtension final
    {
    public:
        consteval FileExtension() = default;
        consteval explicit FileExtension(std::string_view value) noexcept
            : m_Value(value)
        {
        }

        constexpr friend bool operator==(const FileExtension& lhs,
            const FileExtension& rhs) noexcept = default;

        constexpr std::string_view AsStringView() const noexcept { return m_Value; }

    private:
        std::string_view m_Value;
    };

private:
    consteval TextureFormat(const FormatIdentifier formatId,
        const wgpu::TextureFormat wgpuFormat,
        const uint32_t channelCount,
        const uint32_t bytesPerBlock,
        const uint32_t blockDimension,
        const FileExtension extension,
        const InplaceString<>& name)
        : m_FormatId(formatId),
          m_WgpuFormat(wgpuFormat),
          m_ChannelCount(channelCount),
          m_BytesPerBlock(bytesPerBlock),
          m_BlockDimension(blockDimension),
          m_Extension(extension),
          m_Name(name)
    {
    }

public:
    constexpr FormatIdentifier GetFormatId() const noexcept { return m_FormatId; }
    constexpr wgpu::TextureFormat GetWgpuFormat() const noexcept { return m_WgpuFormat; }
    constexpr size_t GetChannelCount() const noexcept { return m_ChannelCount; }
    constexpr size_t GetBytesPerBlock() const noexcept { return m_BytesPerBlock; }
    constexpr uint32_t GetBlockDimension() const noexcept { return m_BlockDimension; }
    constexpr FileExtension GetExtension() const noexcept { return m_Extension; }

    constexpr friend bool operator==(const TextureFormat& lhs,
        const TextureFormat& rhs) noexcept = default;

    constexpr bool IsUnknown() const noexcept { return m_FormatId == FormatIdentifier::Unknown; }
    constexpr bool IsRgba() const noexcept { return m_FormatId == FormatIdentifier::RGBA; }
    constexpr bool IsBC7() const noexcept { return m_FormatId == FormatIdentifier::BC7; }
    constexpr bool IsASTC() const noexcept { return m_FormatId == FormatIdentifier::ASTC; }

    /// Returns the human-readable name of the texture format.
    constexpr std::string_view GetName() const noexcept { return m_Name; }

    /// Returns the number of image blocks in a horizontal row of the texture.
    /// The number of blocks per row can vary depending on the compression scheme.
    /// Raw RGBA images have one block per texel.
    /// BC7 compressed images have one block per 4x4 texel region.
    size_t GetBlocksPerRow(const uint32_t imageWidth) const;

    /// Returns the number of image blocks in a vertical column of the texture.
    /// The number of blocks per column can vary depending on the compression scheme.
    /// Raw RGBA images have one block per texel.
    /// BC7 compressed images have one block per 4x4 texel region.
    size_t GetBlockRowCount(const uint32_t imageHeight) const;

    /// Returns the width of the image rounded up to the nearest block boundary.
    uint32_t GetBlockAlignedWidth(const uint32_t imageWidth) const noexcept;

    /// Returns the height of the image rounded up to the nearest block boundary.
    uint32_t GetBlockAlignedHeight(const uint32_t imageHeight) const noexcept;

    /// Returns the number of bytes in a block-aligned row of the texture.
    /// The number of bytes per row can vary depending on the compression scheme.
    /// Raw RGBA images have width * 4 bytes per row.
    /// BC7 compressed images have 16 byte blocks, and (width + 3) / 4 blocks per row, so 16 *
    /// ((width + 3) / 4) bytes per row.
    size_t GetBlockAlignedBytesPerRow(const uint32_t imageWidth) const;

    /// Returns the number of block-aligned bytes in a row of the texture, aligned to the specified
    /// alignment.
    size_t GetBlockAlignedBytesPerRowWithPadding(const uint32_t imageWidth,
        const uint32_t rowStrideAlignment) const;

    /// Returns the block-aligned size of the texture data in bytes, given the image width and
    /// height. Width/height are used to calculate the block column/row count for the texture. If
    /// width/height are not multiples of the block texel dimensions, the calculation will round up
    /// to the next full block.
    size_t GetBlockAlignedSizeofData(const uint32_t imageWidth, const uint32_t imageHeight) const;

    /// Returns the row-aligned size of the texture data in bytes, given the image width and height.
    size_t GetBlockAlignedSizeofDataWithPadding(const uint32_t imageWidth,
        const uint32_t imageHeight,
        const uint32_t rowStrideAlignment) const;

    /// Returns the total number of block-aligned byte in the texture for all mip levels, given the
    /// image width, height, and mip count. This does not include row padding.
    size_t GetTotalSizeofMipData(
        const uint32_t imageWidth, const uint32_t imageHeight, const uint32_t mipCount) const;

    /// Returns the total size of the texture data for all mip levels, given the image width,
    /// height, mip count, and row alignment. This includes row padding.
    size_t GetTotalSizeofMipDataWithPadding(const uint32_t imageWidth,
        const uint32_t imageHeight,
        const uint32_t mipCount,
        const uint32_t alignment) const;

    /// Returns the texture format for raw RGBA images.
    static consteval const TextureFormat& GetRGBA()
    {
        static constexpr const TextureFormat RGBA(FormatIdentifier::RGBA,
            wgpu::TextureFormat::RGBA8UnormSrgb,
            kDefaultChannelCount,
            4,
            1,
            TextureFormat::FileExtension{ "" },
            InplaceString<>("RGBA"));
        return RGBA;
    }

    /// Returns the texture format for BC7 compressed images.
    static consteval const TextureFormat& GetBC7()
    {
        static constexpr const TextureFormat BC7(FormatIdentifier::BC7,
            wgpu::TextureFormat::BC7RGBAUnormSrgb,
            kDefaultChannelCount,
            16,
            4,
            TextureFormat::FileExtension{ ".bc7" },
            InplaceString<>("BC7"));
        return BC7;
    }

    /// Returns the texture format for ASTC 4x4 sRGB compressed images.
    static consteval const TextureFormat& GetASTC()
    {
        constexpr uint32_t kBytesPerBlock = 16;
        constexpr uint32_t kBlockDimension = 4;
        static constexpr const TextureFormat ASTC(FormatIdentifier::ASTC,
            wgpu::TextureFormat::ASTC4x4UnormSrgb,
            kDefaultChannelCount,
            kBytesPerBlock,
            kBlockDimension,
            TextureFormat::FileExtension{ ".astc" },
            InplaceString<>("ASTC"));
        return ASTC;
    }

    /// Returns an unknown texture format.
    static consteval const TextureFormat& GetUnknown()
    {
        static constexpr const TextureFormat Unknown(FormatIdentifier::Unknown,
            wgpu::TextureFormat::Undefined,
            0,
            0,
            0,
            TextureFormat::FileExtension{ "" },
            InplaceString<>("Unknown"));
        return Unknown;
    }

    /// Returns the maximum number of mip levels for the given texture dimensions.
    static uint32_t GetMaxMipCount(const uint32_t width, const uint32_t height);

    /// Checks if the texture dimensions are within the allowed limits.
    static Result<> CheckDimensions(
        const uint32_t width, const uint32_t height, const uint32_t mipCount);

    /// Returns the texture format corresponding to the format identifier.
    static constexpr TextureFormat GetFormat(const FormatIdentifier formatId);
    /// Returns the texture format corresponding to the WebGPU texture format.
    static constexpr TextureFormat GetFormat(const wgpu::TextureFormat wgpuTextureFormat);
    /// Returns the texture format corresponding to the file extension.
    static constexpr TextureFormat GetFormat(const FileExtension fileExtension);

private:
    FormatIdentifier m_FormatId{ FormatIdentifier::Unknown };
    wgpu::TextureFormat m_WgpuFormat{ wgpu::TextureFormat::Undefined };
    size_t m_ChannelCount{ 0 };     // Number of color channels for the texture format
    size_t m_BytesPerBlock{ 0 };    // Number of bytes per compressed block
    uint32_t m_BlockDimension{ 0 }; // Width/height of a block in texels
    FileExtension m_Extension;      // File extension associated with the texture format
    InplaceString<> m_Name;         // Name of the texture format
};

constexpr TextureFormat
TextureFormat::GetFormat(const FormatIdentifier formatId)
{
    switch(formatId)
    {
        case FormatIdentifier::RGBA:
            return GetRGBA();
        case FormatIdentifier::BC7:
            return GetBC7();
        case FormatIdentifier::ASTC:
            return GetASTC();
        case FormatIdentifier::Unknown:
            return GetUnknown();
    }
    
    return GetUnknown();
}
constexpr TextureFormat
TextureFormat::GetFormat(const wgpu::TextureFormat wgpuTextureFormat)
{
    switch(wgpuTextureFormat)
    {
        case GetRGBA().m_WgpuFormat:
            return GetRGBA();
        case GetBC7().m_WgpuFormat:
            return GetBC7();
        case GetASTC().m_WgpuFormat:
            return GetASTC();
        default:
            return GetUnknown();
    }
}

constexpr TextureFormat
TextureFormat::GetFormat(const TextureFormat::FileExtension fileExtension)
{
    if(fileExtension == GetRGBA().m_Extension)
    {
        return GetRGBA();
    }
    if(fileExtension == GetBC7().m_Extension)
    {
        return GetBC7();
    }
    if(fileExtension == GetASTC().m_Extension)
    {
        return GetASTC();
    }

    return GetUnknown();
}