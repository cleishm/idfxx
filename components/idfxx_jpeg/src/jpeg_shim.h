// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Private C interface to libjpeg-turbo.
//
// libjpeg reports fatal errors through a callback that must not return; the
// shim catches them with setjmp/longjmp and turns them into status codes.
// Every libjpeg call is made from jpeg_shim.c, so a longjmp only ever unwinds
// C frames.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Outcome of a shim call.
typedef enum {
    IDFXX_JPEG_OK = 0,
    IDFXX_JPEG_INVALID_DATA,  ///< Not a JPEG, or corrupt (including any corrupt-data warning).
    IDFXX_JPEG_TRUNCATED,     ///< The source ended before libjpeg had the data it needed.
    IDFXX_JPEG_NOT_SUPPORTED, ///< The image uses a feature this decoder does not support.
    IDFXX_JPEG_TOO_LARGE,     ///< Decoding would take the working memory past the memory limit.
    IDFXX_JPEG_NO_MEM,        ///< An allocation failed.
    IDFXX_JPEG_SOURCE_FAILED, ///< The fill callback reported a failure.
} idfxx_jpeg_status;

/// Fills @p buf with up to @p capacity bytes and stores the count in
/// @p count (0 at the end of the stream). Returns false if reading failed.
typedef bool (*idfxx_jpeg_fill_fn)(void* user, uint8_t* buf, size_t capacity, size_t* count);

/// Image properties read from the headers.
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t output_width;  ///< At full scale.
    uint32_t output_height; ///< At full scale.
    bool progressive;
    /// The Orientation tag (1-8) from IFD0 of the first APP1 segment starting
    /// with "Exif\0\0", or 0 if there is no such tag or it is invalid.
    uint8_t orientation;
} idfxx_jpeg_header;

typedef struct idfxx_jpeg_ctx idfxx_jpeg_ctx;

/// Allocates a context and creates the libjpeg decompressor. If @p limited,
/// the decoder's working memory (the context and what libjpeg allocates,
/// approximately) is limited to @p memory_limit bytes. It is counted from
/// creation, and the limit is enforced from idfxx_jpeg_start() on: that call
/// fails with IDFXX_JPEG_TOO_LARGE if the count is already over the limit,
/// and any later call fails with it instead of allocating past the limit.
/// Sets @p *out to the context, or to NULL if it could not be allocated; a
/// non-NULL context must be released with idfxx_jpeg_destroy() even when an
/// error is returned.
idfxx_jpeg_status idfxx_jpeg_create(
    idfxx_jpeg_fill_fn fill,
    void* user,
    bool limited,
    size_t memory_limit,
    idfxx_jpeg_ctx** out
);

/// Reads the headers, rejects unsupported images, and selects RGB888 output
/// at full scale.
idfxx_jpeg_status idfxx_jpeg_read_header(idfxx_jpeg_ctx* ctx, idfxx_jpeg_header* out);

/// Reports the output size at a scale of @p eighths / 8.
idfxx_jpeg_status idfxx_jpeg_calc_output(idfxx_jpeg_ctx* ctx, unsigned eighths, uint32_t* width, uint32_t* height);

/// Starts decompression at a scale of @p eighths / 8, and reports the output
/// size it produces. From here on, the memory limit is enforced.
idfxx_jpeg_status idfxx_jpeg_start(idfxx_jpeg_ctx* ctx, unsigned eighths, uint32_t* width, uint32_t* height);

/// Allocates a buffer for one output row (output_width RGB888 pixels) from
/// the decoder's working memory, so it counts towards the memory limit. Call
/// after idfxx_jpeg_start(). The buffer is freed when the decompression
/// finishes or is abandoned.
idfxx_jpeg_status idfxx_jpeg_alloc_row(idfxx_jpeg_ctx* ctx, uint8_t** row);

/// Decodes the next output row into @p row (output_width RGB888 pixels).
idfxx_jpeg_status idfxx_jpeg_read_row(idfxx_jpeg_ctx* ctx, uint8_t* row);

/// Finishes decompression, reading up to the end-of-image marker. Frees the
/// row from idfxx_jpeg_alloc_row().
idfxx_jpeg_status idfxx_jpeg_finish(idfxx_jpeg_ctx* ctx);

/// Abandons a decompression in progress. Frees the row from
/// idfxx_jpeg_alloc_row().
void idfxx_jpeg_abort(idfxx_jpeg_ctx* ctx);

/// Destroys the decompressor and frees the context. Accepts NULL.
void idfxx_jpeg_destroy(idfxx_jpeg_ctx* ctx);

#ifdef __cplusplus
}
#endif
