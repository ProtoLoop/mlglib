#include "TextureFormat.h"

/// TextureHeader

bool
TextureHeader::CheckMagic(const std::span<const std::byte> buffer)
{
    if(buffer.size() < sizeof(TextureHeader))
    {
        return false;
    }

    const void* p = buffer.data();
    const TextureHeader* header = static_cast<const TextureHeader*>(p);
    return header->Magic0 == TextureHeader::kMagic0 && header->Magic1 == TextureHeader::kMagic1;
}

/// Checks if the buffer contains the correct version for a texture header.
bool
TextureHeader::CheckVersion(const std::span<const std::byte> buffer)
{
    if(buffer.size() < sizeof(TextureHeader))
    {
        return false;
    }

    const void* p = buffer.data();
    const TextureHeader* header = static_cast<const TextureHeader*>(p);
    return header->Version == TextureHeader::kVersion;
}

bool
TextureHeader::IsMlgTexture(const std::span<const std::byte> buffer)
{
    return buffer.size() >= sizeof(TextureHeader) && CheckMagic(buffer) && CheckVersion(buffer);
}

const TextureHeader*
TextureHeader::GetHeader(const std::span<const std::byte> buffer)
{
    if(!CheckMagic(buffer))
    {
        MLG_WARN("Invalid texture header: Magic numbers do not match");
        return nullptr;
    }

    if(!CheckVersion(buffer))
    {
        MLG_WARN("Invalid texture header: Version does not match");
        return nullptr;
    }

    const void* p = buffer.data();
    const TextureHeader* header = static_cast<const TextureHeader*>(p);

    const TextureFormat& format = TextureFormat::GetFormat(header->FourCC);
    if(format.IsUnknown())
    {
        MLG_WARN("Unknown texture format: {}", header->FourCC.AsStringView());
        return nullptr;
    }

    if(!format.CheckDimensions(header->Width, header->Height, header->MipCount))
    {
        return nullptr;
    }

    return header;
}

Result<std::span<const std::byte>>
TextureHeader::GetPixelBytes(const std::span<const std::byte> buffer)
{
    const TextureHeader* header = GetHeader(buffer);
    MLG_CHECK(header, "Invalid texture header");

    const size_t headerSize = sizeof(TextureHeader);
    MLG_CHECK(buffer.size() > headerSize, "Buffer size is smaller than header size");

    const std::span pixelBytes = buffer.subspan(headerSize);

    const TextureFormat& format = TextureFormat::GetFormat(header->FourCC);
    MLG_CHECK(!format.IsUnknown(), "Unknown texture format: {}", header->FourCC.AsStringView());

    MLG_CHECK(pixelBytes.size()
            >= format.GetTotalSizeofMipData(header->Width, header->Height, header->MipCount),
        "Pixel bytes span is smaller than expected");

    return pixelBytes;
}

Result<>
TextureHeader::ValidateFile(const std::span<const std::byte> fileBuffer)
{
    const TextureHeader* header = GetHeader(fileBuffer);
    MLG_CHECK(header, "Invalid texture header");

    const TextureFormat& format = TextureFormat::GetFormat(header->FourCC);
    MLG_CHECK(!format.IsUnknown(), "Unknown texture format: {}", header->FourCC.AsStringView());

    const size_t expectedSize =
        format.GetTotalSizeofMipData(header->Width, header->Height, header->MipCount);

    const size_t actualSize = fileBuffer.size() - sizeof(TextureHeader);

    MLG_CHECK(actualSize == expectedSize,
        "File buffer size does not match expected size: {} != {}",
        actualSize,
        expectedSize);

    return Result<>::Ok;
}
