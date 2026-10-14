#pragma once

#include "AssertHelper.h"

#include <limits>
#include <string_view>
#include <webgpu/webgpu_cpp.h>

/// Encapsulates information about a texture format, including its compression type, block size, and
/// encoding.
struct TextureFormat final
{
    enum class FormatIdentifier
    {
        Unknown,
        RGBA,
        BC7,
        // ETC2,
        // ASTC
    };

    /// Four-character code (FourCC) used to identify the texture format in a file.
    /// Typically part of the file's metadata or header.
    class FourCC final
    {
    public:
        consteval FourCC() = default;
        consteval explicit FourCC(const char a, const char b, const char c, const char d) noexcept
            : m_Value{ a, b, c, d }
        {
        }

        constexpr friend bool operator==(const FourCC& lhs, const FourCC& rhs) noexcept = default;

        constexpr std::string_view AsStringView() const noexcept
        {
            return std::string_view(&m_Value[0], std::size(m_Value));
        }

    private:
        char m_Value[4]{ 'u', 'n', 'k', '\0' };
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

    /// Returns the human-readable name of the texture format.
    constexpr std::string_view GetName() const noexcept { return HeaderFourcc.AsStringView(); }

    /// Returns the number of image blocks in a vertical column of the texture.
    /// The number of blocks per column can vary depending on the compression scheme.
    /// Raw RGBA images have one block per texel row.
    /// BC7 compressed images have one block per 4x4 texel region.
    constexpr size_t GetBlockRowCount(const uint32_t imageHeight) const
    {
        MLG_ABORTIF(BlockTexelWidth == 0, "Compressed block texel width is zero");
        MLG_ABORTIF(std::numeric_limits<size_t>::max() - BlockTexelWidth + 1 < imageHeight,
            "Image height too large: {}",
            imageHeight);
        return (imageHeight + BlockTexelWidth - 1) / BlockTexelWidth;
    }

    /// Returns the number of bytes in a row of the texture.
    /// The number of bytes per row can vary depending on the compression scheme.
    /// Raw RGBA images have width * 4 bytes per row.
    /// BC7 compressed images have 16 byte blocks, and (width + 3) / 4 blocks per row, so 16 *
    /// ((width + 3) / 4) bytes per row.
    constexpr size_t GetBytesPerRow(const uint32_t imageWidth) const
    {
        MLG_ABORTIF(BlockTexelWidth == 0, "Block texel width is zero");
        MLG_ABORTIF(std::numeric_limits<size_t>::max() - BlockTexelWidth + 1 < imageWidth,
            "Image width too large: {}",
            imageWidth);
        const size_t blocksPerRow = (imageWidth + BlockTexelWidth - 1) / BlockTexelWidth;

        MLG_ABORTIF(std::numeric_limits<size_t>::max() / BlockTexelWidth <= blocksPerRow,
            "Row width too large: {}",
            imageWidth);

        return blocksPerRow * BytesPerBlock;
    }

    constexpr size_t GetAlignedBytesPerRow(const uint32_t imageWidth,
        const uint32_t alignment) const
    {
        MLG_ABORTIF(alignment == 0, "Alignment must be non-zero");

        const size_t bytesPerRow = GetBytesPerRow(imageWidth);

        MLG_ABORTIF(std::numeric_limits<size_t>::max() - alignment + 1 < bytesPerRow,
            "Alignment too large for the given row size: {} > {}",
            alignment,
            bytesPerRow);

        const size_t alignmentsPerRow = (bytesPerRow + alignment - 1) / alignment;

        MLG_ABORTIF(std::numeric_limits<size_t>::max() / alignment < alignmentsPerRow,
            "Aligned row width too large: {}",
            bytesPerRow);

        return alignmentsPerRow * alignment;
    }

    /// Returns the size of the texture data in bytes, given the image width and height.
    constexpr size_t GetSizeofData(const uint32_t imageWidth, const uint32_t imageHeight) const
    {
        const size_t blockRows = GetBlockRowCount(imageHeight);
        const size_t bytesPerRow = GetBytesPerRow(imageWidth);

        MLG_ABORTIF(std::numeric_limits<size_t>::max() / bytesPerRow < blockRows,
            "Texture size too large: {} x {}",
            imageWidth,
            imageHeight);

        return blockRows * bytesPerRow;
    }

    /// Returns the row-aligned size of the texture data in bytes, given the image width and height.
    constexpr size_t GetAlignedSizeofData(
        const uint32_t imageWidth, const uint32_t imageHeight, const uint32_t alignment) const
    {
        const size_t blockRows = GetBlockRowCount(imageHeight);
        const size_t bytesPerRow = GetAlignedBytesPerRow(imageWidth, alignment);

        MLG_ABORTIF(std::numeric_limits<size_t>::max() / bytesPerRow < blockRows,
            "Texture size too large: {} x {}",
            imageWidth,
            imageHeight);

        return blockRows * bytesPerRow;
    }

    constexpr bool IsUnknown() const noexcept { return FormatId == FormatIdentifier::Unknown; }
    constexpr bool IsNone() const noexcept { return FormatId == FormatIdentifier::RGBA; }
    constexpr bool IsBC7() const noexcept { return FormatId == FormatIdentifier::BC7; }

    /// Returns the texture format for raw RGBA images.
    static consteval const TextureFormat& GetRGBA()
    {
        static constexpr const TextureFormat RGBA //
            {
                .FormatId = FormatIdentifier::RGBA,
                .WgpuFormat = wgpu::TextureFormat::RGBA8UnormSrgb,
                .BytesPerBlock = 4,
                .BlockTexelWidth = 1,
                .Extension = TextureFormat::FileExtension{ "" },
                .HeaderFourcc = TextureFormat::FourCC{ 'r', 'g', 'b', 'a' },
            };
        return RGBA;
    }

    /// Returns the texture format for BC7 compressed images.
    static consteval const TextureFormat& GetBC7()
    {
        static constexpr const TextureFormat BC7 //
            {
                .FormatId = FormatIdentifier::BC7,
                .WgpuFormat = wgpu::TextureFormat::BC7RGBAUnormSrgb,
                .BytesPerBlock = 16,
                .BlockTexelWidth = 4,
                .Extension = TextureFormat::FileExtension{ ".bc7" },
                .HeaderFourcc = TextureFormat::FourCC{ 'b', 'c', '7', ' ' },
            };
        return BC7;
    }
    /// Returns an unknown texture format.
    static consteval const TextureFormat& GetUnknown()
    {
        static constexpr const TextureFormat Unknown{};
        return Unknown;
    }

    /// Returns the texture format corresponding to the format identifier.
    static constexpr TextureFormat GetFormat(const FormatIdentifier formatId);
    /// Returns the texture format corresponding to the WebGPU texture format.
    static constexpr TextureFormat GetFormat(const wgpu::TextureFormat wgpuTextureFormat);
    /// Returns the texture format corresponding to the FourCC encoding ID.
    static constexpr TextureFormat GetFormat(const FourCC encodingId);
    /// Returns the texture format corresponding to the file extension.
    static constexpr TextureFormat GetFormat(const FileExtension fileExtension);

    FormatIdentifier FormatId{ FormatIdentifier::Unknown };
    wgpu::TextureFormat WgpuFormat{ wgpu::TextureFormat::Undefined };
    uint32_t BytesPerBlock{ 0 };   // Number of bytes per compressed block
    uint32_t BlockTexelWidth{ 0 }; // Width/height of a block in texels
    FileExtension Extension;       // File extension associated with the texture format
    FourCC HeaderFourcc;           // FourCC code used in the texture header to identify the format
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
        default:
            return GetUnknown();
    }
}
constexpr TextureFormat
TextureFormat::GetFormat(const wgpu::TextureFormat wgpuTextureFormat)
{
    switch(wgpuTextureFormat)
    {
        case GetRGBA().WgpuFormat:
            return GetRGBA();
        case GetBC7().WgpuFormat:
            return GetBC7();
        default:
            return GetUnknown();
    }
}
constexpr TextureFormat
TextureFormat::GetFormat(const FourCC encodingId)
{
    if(encodingId == GetRGBA().HeaderFourcc)
    {
        return GetRGBA();
    }
    if(encodingId == GetBC7().HeaderFourcc)
    {
        return GetBC7();
    }

    return GetUnknown();
}

constexpr TextureFormat
TextureFormat::GetFormat(const TextureFormat::FileExtension fileExtension)
{
    if(fileExtension == GetRGBA().Extension)
    {
        return GetRGBA();
    }
    if(fileExtension == GetBC7().Extension)
    {
        return GetBC7();
    }

    return GetUnknown();
}