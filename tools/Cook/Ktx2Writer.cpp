#include "Ktx2Writer.h"

#include "MipMapChain.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace
{
constexpr uint8_t kIdentifier[] //
    { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
constexpr size_t kFileHeaderByteCount = 80;
constexpr size_t kGlobalDataOffsetFieldOffset = 64;
constexpr size_t kLevelIndexEntryByteCount = 24;
constexpr size_t kDescriptorByteCount = 44;
constexpr uint32_t kDescriptorVersionAndSize = 0x00280002;
constexpr uint32_t kBc7SrgbStraightAlphaBt709 = 0x00020186;
constexpr uint32_t kBc7TexelBlockDimensions = 0x00000303;
constexpr uint32_t kBc7SampleBitOffsetLengthChannel = 0x007F0000;
constexpr uint32_t kUnormSampleUpper = 0xFFFFFFFF;

// On-disk layouts written directly as bytes.
struct FileHeader
{
    uint8_t Identifier[sizeof(kIdentifier)];
    uint32_t VkFormat;
    uint32_t TypeSize;
    uint32_t PixelWidth;
    uint32_t PixelHeight;
    uint32_t PixelDepth;
    uint32_t LayerCount;
    uint32_t FaceCount;
    uint32_t LevelCount;
    uint32_t SupercompressionScheme;
    uint32_t DescriptorOffset;
    uint32_t DescriptorSize;
    uint32_t KeyValueOffset;
    uint32_t KeyValueSize;
    // Supercompression global data.
    uint64_t GlobalDataOffset;
    uint64_t GlobalDataSize;
};

struct LevelIndexEntry
{
    uint64_t ByteOffset;
    uint64_t ByteLength;
    uint64_t UncompressedByteLength;
};

// Data format descriptor with one basic block and one sample, as used by BC7.
struct DataFormatDescriptor
{
    uint32_t TotalSize;
    uint32_t VendorAndDescriptorType;
    uint32_t VersionAndBlockSize;
    uint32_t ColorModelPrimariesTransferFlags;
    uint32_t TexelBlockDimensions;
    uint32_t BytesPlane0To3;
    uint32_t BytesPlane4To7;
    uint32_t SampleBitOffsetLengthChannel;
    uint32_t SamplePosition;
    uint32_t SampleLower;
    uint32_t SampleUpper;
};

static_assert(std::endian::native == std::endian::little,
    "KTX2 struct writes require a little-endian host");
static_assert(std::is_trivially_copyable_v<FileHeader>);
static_assert(std::is_trivially_copyable_v<LevelIndexEntry>);
static_assert(std::is_trivially_copyable_v<DataFormatDescriptor>);
static_assert(sizeof(FileHeader) == kFileHeaderByteCount);
static_assert(offsetof(FileHeader, VkFormat) == sizeof(kIdentifier));
static_assert(offsetof(FileHeader, GlobalDataOffset) == kGlobalDataOffsetFieldOffset);
static_assert(sizeof(LevelIndexEntry) == kLevelIndexEntryByteCount);
static_assert(sizeof(DataFormatDescriptor) == kDescriptorByteCount);

constexpr uint32_t kVkFormatBc7Srgb = 146;

Result<>
WriteBytes(std::ofstream& stream, const std::span<const std::byte> bytes)
{
    MLG_CHECK(bytes.size() <= static_cast<size_t>(std::numeric_limits<std::streamsize>::max()),
        "KTX2 data exceeds stream size limit");
    const void* data = bytes.data();
    stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes.size()));
    MLG_CHECK(stream.good(), "Failed to write KTX2 data");
    return Result<>::Ok;
}
} // namespace

Result<>
WriteKtx2(const std::filesystem::path& path, const MipMapChain& chain)
{
    const TextureFormat format = chain.GetFormat();
    const uint32_t mipCount = chain.GetMipCount();
    const auto mips = chain.GetMipMaps();
    MLG_CHECK(format.IsBC7(), "KTX2 writer requires BC7 sRGB data");
    MLG_CHECK(format.CheckDimensions(chain.GetWidth(), chain.GetHeight(), mipCount));
    MLG_CHECK(mips.size() == mipCount, "KTX2 mip count does not match chain data");

    for(uint32_t mip = 0; mip < mipCount; ++mip)
    {
        const uint32_t width = std::max(chain.GetWidth() >> mip, 1u);
        const uint32_t height = std::max(chain.GetHeight() >> mip, 1u);
        MLG_CHECK(mips[mip].GetFormat().IsBC7(), "KTX2 mip {} is not BC7", mip);
        MLG_CHECK(mips[mip].GetBytes().size() == format.GetBlockAlignedSizeofData(width, height),
            "Invalid KTX2 mip {} byte count",
            mip);
    }

    const size_t descriptorOffset = sizeof(FileHeader) + (sizeof(LevelIndexEntry) * mipCount);
    const size_t metadataSize = descriptorOffset + sizeof(DataFormatDescriptor);
    constexpr size_t kBytesPerBlock = TextureFormat::GetBC7().GetBytesPerBlock();
    const size_t paddedMetadataSize =
        (metadataSize + kBytesPerBlock - 1) / kBytesPerBlock * kBytesPerBlock;

    FileHeader fileHeader //
        {
            .Identifier = {},
            .VkFormat = kVkFormatBc7Srgb,
            .TypeSize = 1,
            .PixelWidth = chain.GetWidth(),
            .PixelHeight = chain.GetHeight(),
            .PixelDepth = 0,
            .LayerCount = 0,
            .FaceCount = 1,
            .LevelCount = mipCount,
            .SupercompressionScheme = 0,
            .DescriptorOffset = static_cast<uint32_t>(descriptorOffset),
            .DescriptorSize = sizeof(DataFormatDescriptor),
            .KeyValueOffset = 0,
            .KeyValueSize = 0,
            .GlobalDataOffset = 0,
            .GlobalDataSize = 0,
        };
    std::copy_n(&kIdentifier[0], sizeof(kIdentifier), &fileHeader.Identifier[0]);

    // The index starts with level zero, but the payload starts with the smallest mip.
    std::vector<LevelIndexEntry> levels(mipCount);
    uint64_t offset = paddedMetadataSize;
    for(size_t mip = mips.size(); mip > 0; --mip)
    {
        const size_t byteCount = mips[mip - 1].GetBytes().size();
        MLG_CHECK(byteCount <= std::numeric_limits<uint64_t>::max() - offset,
            "KTX2 file size overflow");
        levels[mip - 1] = LevelIndexEntry //
            {
                .ByteOffset = offset,
                .ByteLength = byteCount,
                .UncompressedByteLength = byteCount,
            };
        offset += byteCount;
    }

    const DataFormatDescriptor descriptor //
        {
            .TotalSize = sizeof(DataFormatDescriptor),
            .VendorAndDescriptorType = 0,
            .VersionAndBlockSize = kDescriptorVersionAndSize,
            .ColorModelPrimariesTransferFlags = kBc7SrgbStraightAlphaBt709,
            .TexelBlockDimensions = kBc7TexelBlockDimensions,
            .BytesPlane0To3 = static_cast<uint32_t>(format.GetBytesPerBlock()),
            .BytesPlane4To7 = 0,
            .SampleBitOffsetLengthChannel = kBc7SampleBitOffsetLengthChannel,
            .SamplePosition = 0,
            .SampleLower = 0,
            .SampleUpper = kUnormSampleUpper,
        };
    const std::byte padding[kBytesPerBlock]{};

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    MLG_CHECK(stream.is_open(), "Failed to open KTX2 output: {}", path.string());
    MLG_CHECK(WriteBytes(stream, std::as_bytes(std::span(&fileHeader, 1))));
    MLG_CHECK(WriteBytes(stream, std::as_bytes(std::span(levels))));
    MLG_CHECK(WriteBytes(stream, std::as_bytes(std::span(&descriptor, 1))));
    MLG_CHECK(WriteBytes(stream, std::span(padding).first(paddedMetadataSize - metadataSize)));
    // BC7 payload sizes are multiples of 16, so no padding is needed between levels.
    for(size_t mip = mips.size(); mip > 0; --mip)
    {
        MLG_CHECK(WriteBytes(stream, mips[mip - 1].GetBytes()));
    }
    stream.close();
    MLG_CHECK(!stream.fail(), "Failed to close KTX2 output: {}", path.string());
    return Result<>::Ok;
}
