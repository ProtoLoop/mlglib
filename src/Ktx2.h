#pragma once

#include "Result.h"
#include "TextureFormat.h"

#include <cstddef>
#include <cstdint>
#include <span>

/// Header for 2D BC7 sRGB KTX2 files without supercompression.
struct Ktx2Header
{
    uint32_t Width{ 0 };
    uint32_t Height{ 0 };
    uint32_t MipCount{ 1 };
    TextureFormat Format;

    /// Checks if the buffer contains a valid KTX2 file.
    static bool IsKtx2(std::span<const std::byte> buffer);

    /// Reads the header and validates all metadata and mip ranges before returning it.
    static Result<Ktx2Header> GetHeader(std::span<const std::byte> buffer);

    /// Returns one tightly packed mip. The returned span borrows the file buffer.
    static Result<std::span<const std::byte>> GetMipBytes(std::span<const std::byte> buffer,
        uint32_t mipLevel);
};
