#pragma once

#include "Result.h"

#include <filesystem>

class MipMapChain;

/// Writes a 2D BC7 sRGB mip chain without supercompression, overwriting the destination.
/// The destination's parent directory must already exist.
Result<> WriteKtx2(const std::filesystem::path& path, const MipMapChain& chain);
