#pragma once

#include "Result.h"
#include "TextureFormat.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace ktx2
{
inline constexpr size_t kSizeofIdentifier = 12;
struct Identifier
{
    uint8_t Bytes[kSizeofIdentifier];
};
inline constexpr Identifier kIdentifier //
    { { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A } };
inline constexpr size_t kFileHeaderByteCount = 80;
inline constexpr size_t kGlobalDataOffsetFieldOffset = 64;
inline constexpr size_t kLevelIndexEntryByteCount = 24;
inline constexpr size_t kDescriptorByteCount = 44;
inline constexpr size_t kUint32ByteCount = sizeof(uint32_t);
inline constexpr uint32_t kDescriptorVersionAndSize = 0x00280002;
inline constexpr uint32_t kBc7SrgbStraightAlphaBt709 = 0x00020186;
inline constexpr uint32_t kAstcSrgbStraightAlphaBt709 = 0x000201A2;
inline constexpr uint32_t kBc7TexelBlockDimensions = 0x00000303;
inline constexpr uint32_t kAstc4x4TexelBlockDimensions = 0x00000303;
inline constexpr uint32_t kBc7SampleBitOffsetLengthChannel = 0x007F0000;
inline constexpr uint32_t kAstcSampleBitOffsetLengthChannel = 0x007F0000;
inline constexpr uint32_t kUnormSampleUpper = 0xFFFFFFFF;

// On-disk layouts shared by the reader and writer.
struct FileHeader
{
    Identifier Identifier;
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

// Data format descriptor with one basic block and one sample, as used by BC7 and ASTC.
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
    "KTX2 structs require a little-endian host");
static_assert(std::is_trivially_copyable_v<FileHeader>);
static_assert(std::is_trivially_copyable_v<LevelIndexEntry>);
static_assert(std::is_trivially_copyable_v<DataFormatDescriptor>);
static_assert(sizeof(FileHeader) == kFileHeaderByteCount);
static_assert(offsetof(FileHeader, VkFormat) == sizeof(kIdentifier));
static_assert(offsetof(FileHeader, GlobalDataOffset) == kGlobalDataOffsetFieldOffset);
static_assert(sizeof(LevelIndexEntry) == kLevelIndexEntryByteCount);
static_assert(sizeof(DataFormatDescriptor) == kDescriptorByteCount);

inline constexpr uint32_t kVkFormatBc7Srgb = 146;
inline constexpr uint32_t kVkFormatAstc4x4Srgb = 158;
} // namespace Ktx2Format

/// Header for 2D BC7 or ASTC 4x4 sRGB KTX2 files without supercompression.
struct Ktx2TextureInfo
{
    uint32_t Width{ 0 };
    uint32_t Height{ 0 };
    uint32_t MipCount{ 1 };
    TextureFormat Format;

    /// Checks if the buffer contains a valid KTX2 file.  Returns the texture info if valid.
    static Result<Ktx2TextureInfo> GetInfo(std::span<const std::byte> buffer);
};

/// Container for a KTX2 texture, providing access to its metadata and mip levels.
class Ktx2Container
{
public:

    static Result<Ktx2Container> Create(std::vector<std::byte>&& buffer)
    {
        const auto info = Ktx2TextureInfo::GetInfo(buffer);
        MLG_CHECK(info);
        return Ktx2Container(*info, std::move(buffer));
    }

    uint32_t GetWidth() const { return m_Info.Width; }
    uint32_t GetHeight() const { return m_Info.Height; }
    uint32_t GetMipCount() const { return m_Info.MipCount; }
    TextureFormat GetFormat() const { return m_Info.Format; }

    Result<std::span<const std::byte>> GetMipBytes(uint32_t mipLevel) const;

    std::span<const std::byte> GetBytes() const { return std::span<const std::byte>(m_Buffer); }

private:
    Ktx2TextureInfo m_Info;
    std::vector<std::byte> m_Buffer;

    Ktx2Container(const Ktx2TextureInfo& info, std::vector<std::byte>&& buffer)
        : m_Info(info), m_Buffer(std::move(buffer))
    {
    }
};