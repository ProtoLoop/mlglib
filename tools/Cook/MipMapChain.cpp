#include "MipMapChain.h"

Result<MipMapChain>
MipMapChain::Create(BoundedVector<MipImage>&& mipmaps)
{
    MLG_CHECK(!mipmaps.empty(), "Mipmaps cannot be empty");
    MLG_CHECK(mipmaps.size() <= TextureFormat::GetMaxMipCount(mipmaps[0].GetWidth(), mipmaps[0].GetHeight()),
        "Too many mipmaps");

    const uint32_t w = mipmaps[0].GetWidth();
    const uint32_t h = mipmaps[0].GetHeight();
    const TextureFormat format = mipmaps[0].GetFormat();

    uint32_t mipW = w;
    uint32_t mipH = h;

    for(const MipImage& mip : mipmaps)
    {
        const uint32_t expectedWidth = format.GetBlockAlignedWidth(mipW);
        const uint32_t expectedHeight = format.GetBlockAlignedHeight(mipH);
        MLG_CHECKV(mip.GetFormat() == format, "All mipmaps must have the same format");
        MLG_CHECKV(mip.GetWidth() == expectedWidth, "Unexpected mip width: {}", mip.GetWidth());
        MLG_CHECKV(mip.GetHeight() == expectedHeight, "Unexpected mip height: {}", mip.GetHeight());

        mipW = std::max(1u, mipW >> 1);
        mipH = std::max(1u, mipH >> 1);
    }

    const uint32_t mipCount = static_cast<uint32_t>(mipmaps.size());

    return MipMapChain(w, h, mipCount, format, std::move(mipmaps));
}