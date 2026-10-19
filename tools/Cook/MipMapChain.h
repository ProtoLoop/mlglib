#pragma once

#include "Result.h"
#include "TextureFormat.h"
#include "Containers.h"

#include <memory>
#include <span>
#include <stb_image.h>

struct StbImageDeleter
{
    void operator()(stbi_uc* pixels) const noexcept
    {
        if(pixels != nullptr)
        {
            stbi_image_free(pixels);
        }
    }
};

/// RAII wrapper for stb image data. Frees the image buffer when it goes out of scope.
using StbDataPtr = std::unique_ptr<stbi_uc, StbImageDeleter>;

/// Represents a single mip level imagea.
class MipImage
{
public:
    MipImage() = delete;
    ~MipImage() = default;
    MipImage(const MipImage&) = delete;
    MipImage& operator=(const MipImage&) = delete;
    MipImage(MipImage&&) = default;
    MipImage& operator=(MipImage&&) = default;

    /// Creates a mip image from raw byte data.
    static Result<MipImage> Create(const uint32_t w,
        const uint32_t h,
        const TextureFormat& format,
        std::vector<std::byte>&& bytes)
    {
        MLG_CHECK(!format.IsUnknown(), "Unknown texture format");
        MLG_CHECKV(format.CheckDimensions(w, h, 1));
        MLG_CHECK(!bytes.empty(), "Byte data cannot be empty");

        return MipImage(w, h, format, std::move(bytes));
    }

    /// Creates a mip image from stb image data, typically loaded from a file.
    static Result<MipImage> Create(
        const uint32_t w, const uint32_t h, const TextureFormat& format, StbDataPtr&& stbData)
    {
        MLG_CHECK(!format.IsUnknown(), "Unknown texture format");
        MLG_CHECKV(format.CheckDimensions(w, h, 1));
        MLG_CHECK(stbData, "Byte data cannot be empty");

        return MipImage(w, h, format, std::move(stbData));
    }

    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }
    const TextureFormat& GetFormat() const { return m_Format; }
    const std::span<std::byte>& GetBytes() const { return m_ByteSpan; }

private:
    MipImage(const uint32_t w,
        const uint32_t h,
        const TextureFormat& format,
        std::vector<std::byte>&& bytes)
        : m_Width(w),
          m_Height(h),
          m_Format(format),
          m_Bytes(std::move(bytes)),
          m_ByteSpan(m_Bytes)
    {
    }

    MipImage(const uint32_t w, const uint32_t h, const TextureFormat& format, StbDataPtr&& stbData)
        : m_Width(w),
          m_Height(h),
          m_Format(format),
          m_StbData(std::move(stbData))
    {
        void* p = m_StbData.get();
        const size_t size = static_cast<size_t>(w) * h * format.GetChannelCount();
        m_ByteSpan = std::span<std::byte>(static_cast<std::byte*>(p), size);
    }

    uint32_t m_Width;
    uint32_t m_Height;
    TextureFormat m_Format;
    std::vector<std::byte> m_Bytes;
    StbDataPtr m_StbData;
    std::span<std::byte> m_ByteSpan;
};

/// Represents a chain of mipmap images for a texture.
class MipMapChain
{
public:
    MipMapChain() = delete;
    ~MipMapChain() = default;
    MipMapChain(const MipMapChain&) = delete;
    MipMapChain& operator=(const MipMapChain&) = delete;
    MipMapChain(MipMapChain&&) = default;
    MipMapChain& operator=(MipMapChain&&) = default;

    static Result<MipMapChain> Create(BoundedVector<MipImage>&& mipmaps);

    uint32_t GetWidth() const noexcept { return m_Width; }
    uint32_t GetHeight() const noexcept { return m_Height; }
    uint32_t GetMipCount() const noexcept { return m_MipCount; }
    TextureFormat GetFormat() const noexcept { return m_Format; }
    std::span<const MipImage> GetMipMaps() const noexcept { return m_MipMaps; }

private:

    MipMapChain(const uint32_t w,
        const uint32_t h,
        const uint32_t mipCount,
        TextureFormat format,
        BoundedVector<MipImage>&& mipmaps)
        : m_Width(w),
          m_Height(h),
          m_MipCount(mipCount),
          m_Format(format),
          m_MipMaps(std::move(mipmaps))
    {
    }
    uint32_t m_Width{ 0 };
    uint32_t m_Height{ 0 };
    uint32_t m_MipCount{ 1 };
    TextureFormat m_Format = TextureFormat::GetUnknown();
    BoundedVector<MipImage> m_MipMaps;
};