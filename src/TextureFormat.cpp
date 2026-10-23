#include "TextureFormat.h"

size_t
TextureFormat::GetBlocksPerRow(const uint32_t imageWidth) const
{
    MLG_ABORTIF(IsUnknown(), "Texture format is unknown");
    MLG_ABORTIF(m_BlockDimension == 0, "Block dimension is zero");
    MLG_ABORTIF(std::numeric_limits<size_t>::max() - m_BlockDimension + 1 < imageWidth,
        "Image width too large: {}",
        imageWidth);

    // Round up to the next full block
    return (imageWidth + m_BlockDimension - 1) / m_BlockDimension;
}

size_t
TextureFormat::GetBlockRowCount(const uint32_t imageHeight) const
{
    MLG_ABORTIF(IsUnknown(), "Texture format is unknown");
    MLG_ABORTIF(m_BlockDimension == 0, "Block dimension is zero");
    MLG_ABORTIF(std::numeric_limits<size_t>::max() - m_BlockDimension + 1 < imageHeight,
        "Image height too large: {}",
        imageHeight);

    // Round up to the next full block
    return (imageHeight + m_BlockDimension - 1) / m_BlockDimension;
}

uint32_t
TextureFormat::GetBlockAlignedWidth(const uint32_t imageWidth) const noexcept
{
    return static_cast<uint32_t>(GetBlocksPerRow(imageWidth) * m_BlockDimension);
}

uint32_t
TextureFormat::GetBlockAlignedHeight(const uint32_t imageHeight) const noexcept
{
    return static_cast<uint32_t>(GetBlockRowCount(imageHeight) * m_BlockDimension);
}

size_t
TextureFormat::GetBlockAlignedBytesPerRow(const uint32_t imageWidth) const
{
    // Round up to the next full block
    const size_t blocksPerRow = GetBlocksPerRow(imageWidth);

    MLG_ABORTIF(std::numeric_limits<size_t>::max() / m_BlockDimension <= blocksPerRow,
        "Row width too large: {}",
        imageWidth);

    return blocksPerRow * m_BytesPerBlock;
}

size_t
TextureFormat::GetBlockAlignedBytesPerRowWithPadding(const uint32_t imageWidth,
    const uint32_t rowStrideAlignment) const
{
    MLG_ABORTIF(rowStrideAlignment == 0, "Alignment must be non-zero");

    const size_t bytesPerRow = GetBlockAlignedBytesPerRow(imageWidth);

    MLG_ABORTIF(std::numeric_limits<size_t>::max() - rowStrideAlignment + 1 < bytesPerRow,
        "Alignment too large for the given row size: {} > {}",
        rowStrideAlignment,
        bytesPerRow);

    const size_t alignmentsPerRow = (bytesPerRow + rowStrideAlignment - 1) / rowStrideAlignment;

    MLG_ABORTIF(std::numeric_limits<size_t>::max() / rowStrideAlignment < alignmentsPerRow,
        "Aligned row width too large: {}",
        bytesPerRow);

    return alignmentsPerRow * rowStrideAlignment;
}

size_t
TextureFormat::GetBlockAlignedSizeofData(const uint32_t imageWidth,
    const uint32_t imageHeight) const
{
    const size_t bytesPerRow = GetBlockAlignedBytesPerRow(imageWidth);
    const size_t rowCount = GetBlockRowCount(imageHeight);
    MLG_ABORTIF(bytesPerRow > 0 && std::numeric_limits<size_t>::max() / bytesPerRow < rowCount,
        "Texture size too large: {} x {}",
        imageWidth,
        imageHeight);

    return bytesPerRow * rowCount;
}

size_t
TextureFormat::GetBlockAlignedSizeofDataWithPadding(
    const uint32_t imageWidth, const uint32_t imageHeight, const uint32_t rowStrideAlignment) const
{
    const size_t bytesPerRow =
        GetBlockAlignedBytesPerRowWithPadding(imageWidth, rowStrideAlignment);
    const size_t rowCount = GetBlockRowCount(imageHeight);

    MLG_ABORTIF(bytesPerRow > 0 && std::numeric_limits<size_t>::max() / bytesPerRow < rowCount,
        "Texture size too large: {} x {}",
        imageWidth,
        imageHeight);

    return rowCount * bytesPerRow;
}

size_t
TextureFormat::GetTotalSizeofMipData(
    const uint32_t imageWidth, const uint32_t imageHeight, const uint32_t mipCount) const
{
    const size_t kMaxSize = std::vector<std::byte>().max_size();

    size_t sizeofData = 0;

    for(uint32_t mip = 0; mip < mipCount; ++mip)
    {
        const uint32_t mipWidth = std::max(imageWidth >> mip, 1u);
        const uint32_t mipHeight = std::max(imageHeight >> mip, 1u);
        const size_t sizeOfMip = GetBlockAlignedSizeofData(mipWidth, mipHeight);

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

size_t
TextureFormat::GetTotalSizeofMipDataWithPadding(const uint32_t imageWidth,
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
        const size_t sizeOfMip =
            GetBlockAlignedSizeofDataWithPadding(mipWidth, mipHeight, alignment);

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

uint32_t
TextureFormat::GetMaxMipCount(const uint32_t width, const uint32_t height)
{
    uint32_t mipCount = 1;
    while(width >= (1u << mipCount) || height >= (1u << mipCount))
    {
        ++mipCount;
    }
    return mipCount;
}

Result<>
TextureFormat::CheckDimensions(const uint32_t width, const uint32_t height, const uint32_t mipCount)
{
    MLG_CHECK(width > 0 && height > 0, "Image width and height must be greater than zero");
    MLG_CHECK(width <= kMaxWidth && height <= kMaxHeight,
        "Image dimensions exceed maximum allowed size: {}x{}",
        width,
        height);

    MLG_CHECK(mipCount > 0, "Mip count must be greater than zero");
    const uint32_t expectedMipCount = GetMaxMipCount(width, height);
    MLG_CHECK(mipCount <= expectedMipCount,
        "Mip count exceeds maximum allowed for the given dimensions: {} > {}",
        mipCount,
        expectedMipCount);

    return Result<>::Ok;
}