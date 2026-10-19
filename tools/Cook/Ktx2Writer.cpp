#include "Ktx2Writer.h"

#include "MipMapChain.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <vector>

namespace
{
void
AppendInteger(std::vector<std::byte>& bytes, uint64_t value, const size_t byteCount)
{
    constexpr uint64_t kByteMask = 0xFFu;
    constexpr uint64_t kByteShift = 8;

    for(size_t i = 0; i < byteCount; ++i)
    {
        bytes.push_back(static_cast<std::byte>(value & kByteMask));
        value >>= kByteShift;
    }
}

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
        MLG_CHECK(mips[mip].GetBytes().size() == format.GetSizeofData(width, height),
            "Invalid KTX2 mip {} byte count",
            mip);
    }

    constexpr uint32_t kHeaderSize = 80;
    constexpr uint32_t kLevelIndexSize = 24;
    constexpr uint32_t kVkFormatBc7Srgb = 146;
    constexpr uint32_t kBlockSize = 16;

    // Khronos basic Data Format Descriptor (DFD): BC7, BT.709 primaries, sRGB transfer, straight
    // alpha. Each 4x4 BC7 block occupies 128 bits (16 bytes).
    constexpr uint32_t kDfd[11] //
        {
            44,         // Total descriptor size.
            0,          // Khronos vendor, basic descriptor type.
            0x00280002, // 40-byte descriptor block, version 2.
            0x00020186, // Straight alpha, sRGB, BT.709, BC7 model (134).
            0x00000303, // Block dimensions minus one: 4x4x1x1.
            16,         // Bytes in plane zero.
            0,          // Remaining planes unused.
            0x007f0000, // Color channel zero, 128 bits, bit offset zero.
            0,          // Sample position.
            0,          // Sample lower bound.
            0xffffffff, // Sample upper bound.
        };
    constexpr uint32_t kDfdSize = sizeof(kDfd);
    const uint32_t dfdOffset = kHeaderSize + (kLevelIndexSize * mipCount);
    const uint32_t metadataSize = dfdOffset + kDfdSize;
    const uint32_t paddedMetadataSize = (metadataSize + kBlockSize - 1) / kBlockSize * kBlockSize;

    // The index starts with level zero, but the payload starts with the smallest mip.
    std::vector<uint64_t> offsets(mipCount);
    uint64_t offset = paddedMetadataSize;
    for(size_t mip = mips.size(); mip > 0; --mip)
    {
        offsets[mip - 1] = offset;
        const size_t byteCount = mips[mip - 1].GetBytes().size();
        MLG_CHECK(byteCount <= std::numeric_limits<uint64_t>::max() - offset,
            "KTX2 file size overflow");
        offset += byteCount;
    }

    std::vector<std::byte> metadata;
    metadata.reserve(paddedMetadataSize);
    constexpr uint8_t kIdentifier[12] //
        {
            0xab,
            0x4b,
            0x54,
            0x58,
            0x20,
            0x32,
            0x30,
            0xbb,
            0x0d,
            0x0a,
            0x1a,
            0x0a,
        };
    for(const uint8_t byte : kIdentifier)
    {
        metadata.push_back(static_cast<std::byte>(byte));
    }

    constexpr size_t k4Bytes = 4;
    constexpr size_t k8Bytes = 8;
    
    AppendInteger(metadata, kVkFormatBc7Srgb, k4Bytes);
    AppendInteger(metadata, 1, k4Bytes); // Type size.
    AppendInteger(metadata, chain.GetWidth(), k4Bytes);
    AppendInteger(metadata, chain.GetHeight(), k4Bytes);
    AppendInteger(metadata, 0, k4Bytes); // Depth: 2D texture.
    AppendInteger(metadata, 0, k4Bytes); // Layers: not an array.
    AppendInteger(metadata, 1, k4Bytes); // Faces.
    AppendInteger(metadata, mipCount, k4Bytes);
    AppendInteger(metadata, 0, k4Bytes); // No supercompression.
    AppendInteger(metadata, dfdOffset, k4Bytes);
    AppendInteger(metadata, kDfdSize, k4Bytes);
    AppendInteger(metadata, 0, k4Bytes); // No key/value data offset.
    AppendInteger(metadata, 0, k4Bytes); // No key/value data length.
    AppendInteger(metadata, 0, k8Bytes); // No supercompression global data offset.
    AppendInteger(metadata, 0, k8Bytes); // No supercompression global data length.
    for(uint32_t mip = 0; mip < mipCount; ++mip)
    {
        AppendInteger(metadata, offsets[mip], k8Bytes);
        AppendInteger(metadata, mips[mip].GetBytes().size(), k8Bytes);
        AppendInteger(metadata, mips[mip].GetBytes().size(), k8Bytes);
    }
    for(const uint32_t word : kDfd)
    {
        AppendInteger(metadata, word, k4Bytes);
    }
    metadata.resize(paddedMetadataSize, std::byte{ 0 });

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    MLG_CHECK(stream.is_open(), "Failed to open KTX2 output: {}", path.string());
    MLG_CHECK(WriteBytes(stream, metadata));
    // BC7 payload sizes are multiples of 16, so no padding is needed between levels.
    for(size_t mip = mips.size(); mip > 0; --mip)
    {
        MLG_CHECK(WriteBytes(stream, mips[mip - 1].GetBytes()));
    }
    stream.close();
    MLG_CHECK(!stream.fail(), "Failed to close KTX2 output: {}", path.string());
    return Result<>::Ok;
}
