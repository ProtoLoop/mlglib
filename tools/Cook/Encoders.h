#pragma once

#include "MipMapChain.h"
#include "Result.h"

/// Encodes a mipmap chain into the BC7 texture format.
Result<MipMapChain> EncodeBC7(const MipMapChain& mipChain, const std::string_view texturePath);

/// Encodes a mipmap chain into the ASTC 4x4 sRGB texture format.
Result<MipMapChain> EncodeASTC(const MipMapChain& mipChain, const std::string_view texturePath);
