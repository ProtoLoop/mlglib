#pragma once

#include "MipMapChain.h"
#include "Result.h"

/// Encodes a mipmap chain into the BC7 texture format.
Result<MipMapChain> EncodeBC7(MipImage&& baseMip, const std::string_view texturePath);