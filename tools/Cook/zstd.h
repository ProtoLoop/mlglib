#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Returns the maximum compressed size for an input of srcSize bytes.
/// Check the result with ZSTD_isError().
size_t ZSTD_compressBound(size_t srcSize);

/// Compresses one buffer into a Zstd frame.
/// A compressionLevel of 0 selects the library default.
/// Returns bytes written or an error code. Check with ZSTD_isError().
size_t ZSTD_compress(void* dst,
    size_t dstCapacity,
    const void* src,
    size_t srcSize,
    int compressionLevel);

/// Decompresses into dst, which must be large enough for the decoded data.
/// Returns bytes written or an error code. Check with ZSTD_isError().
size_t ZSTD_decompress(void* dst,
    size_t dstCapacity,
    const void* src,
    size_t srcSize);

/// Returns the minimum supported compression level, which can be negative.
int ZSTD_minCLevel(void);

/// Returns the maximum supported compression level.
int ZSTD_maxCLevel(void);

/// Returns the compression level used when 0 is requested.
int ZSTD_defaultCLevel(void);

/// Opaque compression context. Reuse across mip levels, with one context per worker.
typedef struct ZSTD_CCtx_s ZSTD_CCtx;

/// Allocates a compression context. Returns NULL on allocation failure.
ZSTD_CCtx* ZSTD_createCCtx(void);

/// Releases a compression context. Accepts NULL.
/// Returns a status that can be checked with ZSTD_isError().
size_t ZSTD_freeCCtx(ZSTD_CCtx* context);

/// Compresses one mip into an independent frame using reusable working memory.
/// No reset is needed between calls. Advanced parameters are reset by this call.
/// A compressionLevel of 0 selects the library default.
/// Returns bytes written or an error code. Check with ZSTD_isError().
size_t ZSTD_compressCCtx(ZSTD_CCtx* context,
    void* dst,
    size_t dstCapacity,
    const void* src,
    size_t srcSize,
    int compressionLevel);

/// Opaque decompression context. Reuse across mip levels, with one context per worker.
typedef struct ZSTD_DCtx_s ZSTD_DCtx;

/// Allocates a decompression context. Returns NULL on allocation failure.
ZSTD_DCtx* ZSTD_createDCtx(void);

/// Releases a decompression context. Accepts NULL.
/// Returns a status that can be checked with ZSTD_isError().
size_t ZSTD_freeDCtx(ZSTD_DCtx* context);

/// Decompresses a mip using reusable working memory. No reset is needed between calls.
/// dstCapacity must accommodate the mip's uncompressed byte count.
/// Returns bytes written or an error code. Check with ZSTD_isError().
size_t ZSTD_decompressDCtx(ZSTD_DCtx* context,
    void* dst,
    size_t dstCapacity,
    const void* src,
    size_t srcSize);

/// Returns nonzero if a Zstd result represents an error.
unsigned ZSTD_isError(size_t result);

/// Returns an error description in static storage owned by Zstd. Do not free it.
const char* ZSTD_getErrorName(size_t result);

#ifdef __cplusplus
}
#endif
