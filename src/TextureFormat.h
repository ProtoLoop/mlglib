#pragma once

#include "AssertHelper.h"
#include "Result.h"

#include <limits>
#include <span>
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
        const uint32_t channelCount,
        const uint32_t bytesPerBlock,
        const uint32_t blockDimension,
        const FileExtension extension,
        const FourCC headerFourcc)
        : m_FormatId(formatId),
          m_WgpuFormat(wgpuFormat),
          m_ChannelCount(channelCount),
          m_BytesPerBlock(bytesPerBlock),
          m_BlockDimension(blockDimension),
          m_Extension(extension),
          m_HeaderFourcc(headerFourcc)
    {
    }

public:

    constexpr FormatIdentifier GetFormatId() const noexcept { return m_FormatId; }
    constexpr wgpu::TextureFormat GetWgpuFormat() const noexcept { return m_WgpuFormat; }
    constexpr size_t GetChannelCount() const noexcept { return m_ChannelCount; }
    constexpr size_t GetBytesPerBlock() const noexcept { return m_BytesPerBlock; }
    constexpr uint32_t GetBlockDimension() const noexcept { return m_BlockDimension; }
    constexpr FileExtension GetExtension() const noexcept { return m_Extension; }
    constexpr FourCC GetHeaderFourcc() const noexcept { return m_HeaderFourcc; }

    /// Returns the human-readable name of the texture format.
    constexpr std::string_view GetName() const noexcept { return m_HeaderFourcc.AsStringView(); }

    /// Returns the number of image blocks in a horizontal row of the texture.
    /// The number of blocks per row can vary depending on the compression scheme.
    /// Raw RGBA images have one block per texel.
    /// BC7 compressed images have one block per 4x4 texel region.
    constexpr size_t GetBlocksPerRow(const uint32_t imageWidth) const
    {
        MLG_ABORTIF(IsUnknown(), "Texture format is unknown");
        MLG_ABORTIF(m_BlockDimension == 0, "Block dimension is zero");
        MLG_ABORTIF(std::numeric_limits<size_t>::max() - m_BlockDimension + 1 < imageWidth,
            "Image width too large: {}",
            imageWidth);

        // Round up to the next full block
        return (imageWidth + m_BlockDimension - 1) / m_BlockDimension;
    }

    /// Returns the number of image blocks in a vertical column of the texture.
    /// The number of blocks per column can vary depending on the compression scheme.
    /// Raw RGBA images have one block per texel.
    /// BC7 compressed images have one block per 4x4 texel region.
    constexpr size_t GetBlockRowCount(const uint32_t imageHeight) const
    {
        MLG_ABORTIF(IsUnknown(), "Texture format is unknown");
        MLG_ABORTIF(m_BlockDimension == 0, "Block dimension is zero");
        MLG_ABORTIF(std::numeric_limits<size_t>::max() - m_BlockDimension + 1 < imageHeight,
            "Image height too large: {}",
            imageHeight);

        // Round up to the next full block
        return (imageHeight + m_BlockDimension - 1) / m_BlockDimension;
    }

    /// Returns the number of bytes in a row of the texture.
    /// The number of bytes per row can vary depending on the compression scheme.
    /// Raw RGBA images have width * 4 bytes per row.
    /// BC7 compressed images have 16 byte blocks, and (width + 3) / 4 blocks per row, so 16 *
    /// ((width + 3) / 4) bytes per row.
    constexpr size_t GetBytesPerRow(const uint32_t imageWidth) const
    {
        // Round up to the next full block
        const size_t blocksPerRow = GetBlocksPerRow(imageWidth);

        MLG_ABORTIF(std::numeric_limits<size_t>::max() / m_BlockDimension <= blocksPerRow,
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
        const size_t bytesPerRow = GetBytesPerRow(imageWidth);
        const size_t rowCount = GetBlockRowCount(imageHeight);
        MLG_ABORTIF(bytesPerRow > 0 && std::numeric_limits<size_t>::max() / bytesPerRow < rowCount,
            "Texture size too large: {} x {}",
            imageWidth,
            imageHeight);

        return bytesPerRow * rowCount;
    }

    /// Returns the row-aligned size of the texture data in bytes, given the image width and height.
    constexpr size_t GetPaddedSizeofData(
        const uint32_t imageWidth, const uint32_t imageHeight, const uint32_t alignment) const
    {
        const size_t bytesPerRow = GetPaddedBytesPerRow(imageWidth, alignment);
        const size_t rowCount = GetBlockRowCount(imageHeight);

        MLG_ABORTIF(bytesPerRow > 0 && std::numeric_limits<size_t>::max() / bytesPerRow < rowCount,
            "Texture size too large: {} x {}",
            imageWidth,
            imageHeight);

        return rowCount * bytesPerRow;
    }

    /// Returns the total size of the texture data for all mip levels, given the image width,
    /// height, and mip count. This does not include row padding.
    constexpr size_t GetTotalSizeofMipData(
        const uint32_t imageWidth, const uint32_t imageHeight, const uint32_t mipCount) const
    {
        const size_t kMaxSize = std::vector<std::byte>().max_size();

        size_t sizeofData = 0;

        for(uint32_t mip = 0; mip < mipCount; ++mip)
        {
            const uint32_t mipWidth = std::max(imageWidth >> mip, 1u);
            const uint32_t mipHeight = std::max(imageHeight >> mip, 1u);
            const size_t sizeOfMip = GetSizeofData(mipWidth, mipHeight);

            MLG_ABORTIF(kMaxSize - sizeofData < sizeOfMip,
                "Texture size too large for mip level {}: {} + {} > {}",
                mip,
                sizeofData,
                sizeOfMip,
                kMaxSize);

            sizeofData += sizeOfMip;
        }

        return sizeofData;
    }

    /// Returns the total size of the texture data for all mip levels, given the image width,
    /// height, mip count, and row alignment. This includes row padding.
    constexpr size_t GetTotalPaddedSizeofMipData(const uint32_t imageWidth,
        const uint32_t imageHeight,
        const uint32_t mipCount,
        const uint32_t alignment) const
    {
        const size_t kMaxSize = std::vector<std::byte>().max_size();

        size_t sizeofData = 0;

        for(uint32_t mip = 0; mip < mipCount; ++mip)
        {
            const uint32_t mipWidth = std::max(imageWidth >> mip, 1u);
            const uint32_t mipHeight = std::max(imageHeight >> mip, 1u);
            const size_t sizeOfMip = GetPaddedSizeofData(mipWidth, mipHeight, alignment);

            MLG_ABORTIF(kMaxSize - sizeofData < sizeOfMip,
                "Texture size too large for mip level {}: {} + {} > {}",
                mip,
                sizeofData,
                sizeOfMip,
                kMaxSize);

            sizeofData += sizeOfMip;
        }

        return sizeofData;
    }

    constexpr bool IsUnknown() const noexcept { return m_FormatId == FormatIdentifier::Unknown; }
    constexpr bool IsRgba() const noexcept { return m_FormatId == FormatIdentifier::RGBA; }
    constexpr bool IsBC7() const noexcept { return m_FormatId == FormatIdentifier::BC7; }

    /// Returns the maximum number of mip levels for the given texture dimensions.
    static constexpr uint32_t GetMipCount(const uint32_t width, const uint32_t height)
    {
        uint32_t mipCount = 1;
        while(width >= (1u << mipCount) || height >= (1u << mipCount))
        {
            ++mipCount;
        }
        return mipCount;
    }

    /// Checks if the texture dimensions are within the allowed limits.
    constexpr Result<> CheckDimensions(const uint32_t width, const uint32_t height, const uint32_t mipCount) const 
    {
        MLG_CHECK(!IsUnknown(), "Texture format is unknown");

        MLG_CHECK(width > 0 && height > 0, "Image width and height must be greater than zero");
        MLG_CHECK(width <= TextureFormat::kMaxWidth && height <= TextureFormat::kMaxHeight,
            "Image dimensions exceed maximum allowed size: {}x{}",
            width,
            height);

        MLG_CHECK(width % GetBlockDimension() == 0 && height % GetBlockDimension() == 0,
            "Image width and height {}x{} must be multiples of the block dimension: {}",
            width,
            height,
            GetBlockDimension());

        MLG_CHECK(mipCount > 0, "Mip count must be greater than zero");
        const uint32_t expectedMipCount = GetMipCount(width, height);
        MLG_CHECK(mipCount <= expectedMipCount,
            "Mip count exceeds maximum allowed for the given dimensions: {} > {}",
            mipCount,
            expectedMipCount);

        return Result<>::Ok;
    }

    /// Returns the texture format for raw RGBA images.
    static consteval const TextureFormat& GetRGBA()
    {
        static constexpr const TextureFormat RGBA(
                FormatIdentifier::RGBA,
                wgpu::TextureFormat::RGBA8UnormSrgb,
                kDefaultChannelCount,
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
            kDefaultChannelCount,
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
    size_t m_ChannelCount{ 0 };   // Number of color channels for the texture format
    size_t m_BytesPerBlock{ 0 };  // Number of bytes per compressed block
    uint32_t m_BlockDimension{ 0 }; // Width/height of a block in texels
    FileExtension m_Extension;      // File extension associated with the texture format
    FourCC m_HeaderFourcc;          // FourCC code used in the texture header to identify the format
};

/// Header for a cooked texture file.
struct TextureHeader
{
    static constexpr TextureFormat::FourCC kMagic0{ 'M', 'L', 'G', ' ' };
    static constexpr TextureFormat::FourCC kMagic1{ 'T', 'E', 'X', ' ' };
    static constexpr uint32_t kVersion = 1;

    /// Extracts a texture header from the buffer.
    /// Returns a pointer to the texture header if the buffer contains a valid header, or nullptr otherwise.
    static const TextureHeader* GetTextureHeader(const std::span<const std::byte> buffer);

    static Result<> ValidateFile(const std::span<const std::byte> fileBuffer);

    static_assert(sizeof(TextureFormat::FourCC) == 4, "Fourcc must be 4 bytes in size");

    TextureHeader(const uint32_t width,
        const uint32_t height,
        const uint32_t mipCount,
        const TextureFormat::FourCC fourCC)
        : Width(width),
          Height(height),
          MipCount(mipCount),
          FourCC(fourCC)
    {
    }

    // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
    const TextureFormat::FourCC Magic0{ kMagic0 };
    const TextureFormat::FourCC Magic1{ kMagic1 };
    const uint32_t Version{ kVersion };
    const uint32_t Width{ 0 };
    const uint32_t Height{ 0 };
    const uint32_t MipCount{ 1 };
    const TextureFormat::FourCC FourCC;
    // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
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