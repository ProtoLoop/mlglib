#pragma once

#include "AssertHelper.h"

#include <limits>
#include <string_view>
#include <webgpu/webgpu_cpp.h>

/// Encapsulates information about a texture format, including its compression type, block size, and
/// encoding.
class TextureFormat final
{
public:
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

private:

    consteval TextureFormat(const FormatIdentifier formatId,
        const wgpu::TextureFormat wgpuFormat,
        const uint32_t bytesPerBlock,
        const uint32_t blockTexelWidth,
        const FileExtension extension,
        const FourCC headerFourcc)
        : m_FormatId(formatId),
          m_WgpuFormat(wgpuFormat),
          m_BytesPerBlock(bytesPerBlock),
          m_BlockTexelWidth(blockTexelWidth),
          m_Extension(extension),
          m_HeaderFourcc(headerFourcc)
    {
    }

public:

    constexpr FormatIdentifier GetFormatId() const noexcept { return m_FormatId; }
    constexpr wgpu::TextureFormat GetWgpuFormat() const noexcept { return m_WgpuFormat; }
    constexpr uint32_t GetBytesPerBlock() const noexcept { return m_BytesPerBlock; }
    constexpr uint32_t GetBlockTexelWidth() const noexcept { return m_BlockTexelWidth; }
    constexpr FileExtension GetExtension() const noexcept { return m_Extension; }
    constexpr FourCC GetHeaderFourcc() const noexcept { return m_HeaderFourcc; }

    /// Returns the human-readable name of the texture format.
    constexpr std::string_view GetName() const noexcept { return m_HeaderFourcc.AsStringView(); }

    /// Returns the number of image blocks in a vertical column of the texture.
    /// The number of blocks per column can vary depending on the compression scheme.
    /// Raw RGBA images have one block per texel row.
    /// BC7 compressed images have one block per 4x4 texel region.
    constexpr size_t GetBlockRowCount(const uint32_t imageHeight) const
    {
        MLG_ABORTIF(m_BlockTexelWidth == 0, "Block width is zero");
        MLG_ABORTIF(std::numeric_limits<size_t>::max() - m_BlockTexelWidth + 1 < imageHeight,
            "Image height too large: {}",
            imageHeight);

        // Round up to the next full block
        return (imageHeight + m_BlockTexelWidth - 1) / m_BlockTexelWidth;
    }

    /// Returns the number of bytes in a row of the texture.
    /// The number of bytes per row can vary depending on the compression scheme.
    /// Raw RGBA images have width * 4 bytes per row.
    /// BC7 compressed images have 16 byte blocks, and (width + 3) / 4 blocks per row, so 16 *
    /// ((width + 3) / 4) bytes per row.
    constexpr size_t GetBytesPerRow(const uint32_t imageWidth) const
    {
        MLG_ABORTIF(m_BlockTexelWidth == 0, "Block texel width is zero");
        MLG_ABORTIF(std::numeric_limits<size_t>::max() - m_BlockTexelWidth + 1 < imageWidth,
            "Image width too large: {}",
            imageWidth);

        // Round up to the next full block
        const size_t blocksPerRow = (imageWidth + m_BlockTexelWidth - 1) / m_BlockTexelWidth;

        MLG_ABORTIF(std::numeric_limits<size_t>::max() / m_BlockTexelWidth <= blocksPerRow,
            "Row width too large: {}",
            imageWidth);

        return blocksPerRow * m_BytesPerBlock;
    }

    /// Returns the number of bytes in a row of the texture, aligned to the specified alignment.
    constexpr size_t GetPaddedBytesPerRow(const uint32_t imageWidth,
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
    /// Width/height are used to calculate the block column/row count for the texture.
    /// If width/height are not multiples of the block texel dimensions, the calculation will round
    /// up to the next full block.
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
        const size_t bytesPerRow = GetPaddedBytesPerRow(imageWidth, alignment);

        MLG_ABORTIF(std::numeric_limits<size_t>::max() / bytesPerRow < blockRows,
            "Texture size too large: {} x {}",
            imageWidth,
            imageHeight);

        return blockRows * bytesPerRow;
    }

    constexpr bool IsUnknown() const noexcept { return m_FormatId == FormatIdentifier::Unknown; }
    constexpr bool IsRgba() const noexcept { return m_FormatId == FormatIdentifier::RGBA; }
    constexpr bool IsBC7() const noexcept { return m_FormatId == FormatIdentifier::BC7; }

    /// Returns the texture format for raw RGBA images.
    static consteval const TextureFormat& GetRGBA()
    {
        static constexpr const TextureFormat RGBA(
                FormatIdentifier::RGBA,
                wgpu::TextureFormat::RGBA8UnormSrgb,
                4,
                1,
                TextureFormat::FileExtension{ "" },
                TextureFormat::FourCC{ 'r', 'g', 'b', 'a' }
            );
        return RGBA;
    }

    /// Returns the texture format for BC7 compressed images.
    static consteval const TextureFormat& GetBC7()
    {
        static constexpr const TextureFormat BC7(FormatIdentifier::BC7,
            wgpu::TextureFormat::BC7RGBAUnormSrgb,
            16,
            4,
            TextureFormat::FileExtension{ ".bc7" },
            TextureFormat::FourCC{ 'b', 'c', '7', ' ' });
        return BC7;
    }
    /// Returns an unknown texture format.
    static consteval const TextureFormat& GetUnknown()
    {
        static constexpr const TextureFormat Unknown(
            FormatIdentifier::Unknown,
            wgpu::TextureFormat::Undefined,
            0,
            0,
            TextureFormat::FileExtension{ "" },
            TextureFormat::FourCC{ 'u', 'n', 'k', ' ' }
        );
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

private:

    FormatIdentifier m_FormatId{ FormatIdentifier::Unknown };
    wgpu::TextureFormat m_WgpuFormat{ wgpu::TextureFormat::Undefined };
    uint32_t m_BytesPerBlock{ 0 };   // Number of bytes per compressed block
    uint32_t m_BlockTexelWidth{ 0 }; // Width/height of a block in texels
    FileExtension m_Extension;       // File extension associated with the texture format
    FourCC m_HeaderFourcc;           // FourCC code used in the texture header to identify the format
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
        case GetRGBA().m_WgpuFormat:
            return GetRGBA();
        case GetBC7().m_WgpuFormat:
            return GetBC7();
        default:
            return GetUnknown();
    }
}
constexpr TextureFormat
TextureFormat::GetFormat(const FourCC encodingId)
{
    if(encodingId == GetRGBA().m_HeaderFourcc)
    {
        return GetRGBA();
    }
    if(encodingId == GetBC7().m_HeaderFourcc)
    {
        return GetBC7();
    }

    return GetUnknown();
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

    return GetUnknown();
}