#pragma once

#include "Result.h"

#include <filesystem>

class MipMapChain;

/// Writes a 2D BC7, ASTC 4x4, or RGBA8 sRGB mip chain, overwriting the destination.
/// The destination's parent directory must already exist.
Result<> WriteKtx2(const std::filesystem::path& path, const MipMapChain& chain);
