#include "Ktx2Writer.h"

#include "Ktx2.h"
#include "MipMapChain.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

static_assert(std::is_trivially_copyable_v<ktx2::FileHeader>);
static_assert(std::is_standard_layout_v<ktx2::FileHeader>);
static_assert(sizeof(ktx2::FileHeader) == ktx2::kFileHeaderByteCount);

namespace
{
constexpr uint32_t kDescriptorVersion = 2;
constexpr uint32_t kDescriptorBlockSizeShift = 16;

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
    MLG_CHECK(format.IsBC7() || format.IsASTC() || format.IsRgba(),
        "KTX2 writer requires BC7, ASTC, or RGBA data");
    MLG_CHECK(format.CheckDimensions(chain.GetWidth(), chain.GetHeight(), mipCount));
    MLG_CHECK(mips.size() == mipCount, "KTX2 mip count does not match chain data");

    for(uint32_t mip = 0; mip < mipCount; ++mip)
    {
        const uint32_t width = std::max(chain.GetWidth() >> mip, 1u);
        const uint32_t height = std::max(chain.GetHeight() >> mip, 1u);
        MLG_CHECK(mips[mip].GetFormat() == format,
            "KTX2 mip {} does not match the expected format",
            mip);
        MLG_CHECK(mips[mip].GetBytes().size() == format.GetBlockAlignedSizeofData(width, height),
            "Invalid KTX2 mip {} byte count",
            mip);
    }

    ktx2::DataFormatDescriptorHeader descriptor{};
    std::array<ktx2::DataFormatSample, ktx2::kRgbaSampleCount> samples{};
    size_t sampleCount = 1;
    uint32_t vkFormat = 0;
    if(format.IsRgba())
    {
        vkFormat = ktx2::kVkFormatRgba8Srgb;
        descriptor.ColorModelPrimariesTransferFlags = ktx2::kRgba8SrgbStraightAlphaBt709;
        descriptor.TexelBlockDimensions = ktx2::kRgba8TexelBlockDimensions;
        sampleCount = ktx2::kRgbaSampleCount;
        samples = { {
            {
                .BitOffsetLengthChannel = ktx2::kRgba8RedSampleBitOffsetLengthChannel,
                .Position = 0,
                .Lower = 0,
                .Upper = ktx2::kRgba8SampleUpper,
            },
            {
                .BitOffsetLengthChannel = ktx2::kRgba8GreenSampleBitOffsetLengthChannel,
                .Position = 0,
                .Lower = 0,
                .Upper = ktx2::kRgba8SampleUpper,
            },
            {
                .BitOffsetLengthChannel = ktx2::kRgba8BlueSampleBitOffsetLengthChannel,
                .Position = 0,
                .Lower = 0,
                .Upper = ktx2::kRgba8SampleUpper,
            },
            {
                .BitOffsetLengthChannel = ktx2::kRgba8AlphaSampleBitOffsetLengthChannel,
                .Position = 0,
                .Lower = 0,
                .Upper = ktx2::kRgba8SampleUpper,
            },
        } };
    }
    else if(format.IsBC7())
    {
        vkFormat = ktx2::kVkFormatBc7Srgb;
        descriptor.ColorModelPrimariesTransferFlags = ktx2::kBc7SrgbStraightAlphaBt709;
        descriptor.TexelBlockDimensions = ktx2::kBc7TexelBlockDimensions;
        samples[0] = {
            .BitOffsetLengthChannel = ktx2::kBc7SampleBitOffsetLengthChannel,
            .Position = 0,
            .Lower = 0,
            .Upper = ktx2::kUnormSampleUpper,
        };
    }
    else
    {
        vkFormat = ktx2::kVkFormatAstc4x4Srgb;
        descriptor.ColorModelPrimariesTransferFlags = ktx2::kAstcSrgbStraightAlphaBt709;
        descriptor.TexelBlockDimensions = ktx2::kAstc4x4TexelBlockDimensions;
        samples[0] = {
            .BitOffsetLengthChannel = ktx2::kAstcSampleBitOffsetLengthChannel,
            .Position = 0,
            .Lower = 0,
            .Upper = ktx2::kUnormSampleUpper,
        };
    }

    const size_t descriptorSize =
        sizeof(descriptor) + (sampleCount * sizeof(ktx2::DataFormatSample));
    descriptor.TotalSize = static_cast<uint32_t>(descriptorSize);
    // The descriptor block size excludes TotalSize.
    descriptor.VersionAndBlockSize =
        ((descriptor.TotalSize - static_cast<uint32_t>(sizeof(uint32_t)))
            << kDescriptorBlockSizeShift)
        | kDescriptorVersion;
    descriptor.BytesPlane0To3 = static_cast<uint32_t>(format.GetBytesPerBlock());

    const size_t descriptorOffset =
        sizeof(ktx2::FileHeader) + (sizeof(ktx2::LevelIndexEntry) * mipCount);
    const size_t metadataSize = descriptorOffset + descriptorSize;
    const size_t alignment = format.GetBytesPerBlock();
    const size_t paddedMetadataSize = (metadataSize + alignment - 1) / alignment * alignment;

    ktx2::FileHeader fileHeader //
        {
            .Identifier = ktx2::kIdentifier,
            .VkFormat = vkFormat,
            .TypeSize = 1,
            .PixelWidth = chain.GetWidth(),
            .PixelHeight = chain.GetHeight(),
            .PixelDepth = 0,
            .LayerCount = 0,
            .FaceCount = 1,
            .LevelCount = mipCount,
            .SupercompressionScheme = 0,
            .DescriptorOffset = static_cast<uint32_t>(descriptorOffset),
            .DescriptorSize = static_cast<uint32_t>(descriptorSize),
            .KeyValueOffset = 0,
            .KeyValueSize = 0,
            .GlobalDataOffset = 0,
            .GlobalDataSize = 0,
        };

    // The index starts with level zero, but the payload starts with the smallest mip.
    std::vector<ktx2::LevelIndexEntry> levels(mipCount);
    uint64_t offset = paddedMetadataSize;
    for(size_t mip = mips.size(); mip > 0; --mip)
    {
        const size_t byteCount = mips[mip - 1].GetBytes().size();
        MLG_CHECK(byteCount <= std::numeric_limits<uint64_t>::max() - offset,
            "KTX2 file size overflow");
        levels[mip - 1] = ktx2::LevelIndexEntry //
            {
                .ByteOffset = offset,
                .ByteLength = byteCount,
                .UncompressedByteLength = byteCount,
            };
        offset += byteCount;
    }

    constexpr size_t kPaddingByteCount = std::max(TextureFormat::GetBC7().GetBytesPerBlock(),
        TextureFormat::GetASTC().GetBytesPerBlock());
    const std::byte padding[kPaddingByteCount]{};

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    MLG_CHECK(stream.is_open(), "Failed to open KTX2 output: {}", path.string());
    MLG_CHECK(WriteBytes(stream, std::as_bytes(std::span(&fileHeader, 1))));
    MLG_CHECK(WriteBytes(stream, std::as_bytes(std::span(levels))));
    MLG_CHECK(WriteBytes(stream, std::as_bytes(std::span(&descriptor, 1))));
    MLG_CHECK(WriteBytes(stream, std::as_bytes(std::span(samples).first(sampleCount))));
    MLG_CHECK(WriteBytes(stream, std::span(padding).first(paddedMetadataSize - metadataSize)));
    // BC7 and ASTC payload sizes are multiples of 16, so no padding is needed between levels.
    for(size_t mip = mips.size(); mip > 0; --mip)
    {
        MLG_CHECK(WriteBytes(stream, mips[mip - 1].GetBytes()));
    }
    stream.close();
    MLG_CHECK(!stream.fail(), "Failed to close KTX2 output: {}", path.string());
    return Result<>::Ok;
}
