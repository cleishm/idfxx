// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "jpeg_shim.h"

#include <esp_log.h>
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// jpeglib.h needs <stdio.h> (for FILE) first, and jerror.h needs jpeglib.h.
// clang-format off
#include <jpeglib.h>
#include <jerror.h>
// clang-format on

#if JPEG_LIB_VERSION < 80
#error "idfxx_jpeg needs libjpeg-turbo built with libjpeg v8 API emulation (WITH_JPEG8)"
#endif

#define IDFXX_JPEG_INPUT_BUFFER_SIZE 4096

static const char* TAG = "idfxx::jpeg";

struct idfxx_jpeg_ctx {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr err;
    struct jpeg_source_mgr src;
    /// libjpeg's own memory manager methods, called by the counting hooks.
    struct jpeg_memory_mgr libjpeg_mem;
    jmp_buf jump;
    idfxx_jpeg_status status;
    idfxx_jpeg_fill_fn fill;
    void* user;
    JSAMPROW row;
    size_t memory_limit;
    size_t allocated; ///< Working memory counted so far.
    size_t pending;   ///< Virtual arrays requested but not yet realized.
    bool limited;
    bool enforcing; ///< The limit applies to new allocations.
    bool over_limit;
    bool exif_done;      ///< Later APP1 segments are skipped whole.
    uint8_t orientation; ///< From the first Exif segment; 0 for none.
    bool source_failed;
    bool end_of_stream;
    bool corrupt;
    JOCTET buffer[IDFXX_JPEG_INPUT_BUFFER_SIZE];
};

static idfxx_jpeg_ctx* ctx_of(j_common_ptr cinfo) {
    return (idfxx_jpeg_ctx*)cinfo->client_data;
}

static void log_message(j_common_ptr cinfo) {
    char message[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, message);
    ESP_LOGD(TAG, "libjpeg: %s", message);
}

static idfxx_jpeg_status classify(const idfxx_jpeg_ctx* ctx, int code) {
    // The source's own outcome explains any error it caused, so it comes first.
    if (ctx->source_failed) {
        return IDFXX_JPEG_SOURCE_FAILED;
    }
    if (ctx->end_of_stream) {
        return IDFXX_JPEG_TRUNCATED;
    }
    if (ctx->corrupt) {
        return IDFXX_JPEG_INVALID_DATA;
    }
    if (ctx->over_limit) {
        return IDFXX_JPEG_TOO_LARGE;
    }
    switch (code) {
    case JERR_OUT_OF_MEMORY:
        return IDFXX_JPEG_NO_MEM;
    case JERR_NO_BACKING_STORE:
        return IDFXX_JPEG_TOO_LARGE;
    case JERR_BAD_DCTSIZE:
    case JERR_CCIR601_NOTIMPL:
    case JERR_CONVERSION_NOTIMPL:
    case JERR_EMPTY_IMAGE:
    case JERR_FRACT_SAMPLE_NOTIMPL:
    case JERR_IMAGE_TOO_BIG:
    case JERR_NOTIMPL:
    case JERR_NOT_COMPILED:
    case JERR_SOF_UNSUPPORTED:
    case JERR_WIDTH_OVERFLOW:
        return IDFXX_JPEG_NOT_SUPPORTED;
    default:
        return IDFXX_JPEG_INVALID_DATA;
    }
}

// Replaces libjpeg's default, which prints the message and calls exit().
static void error_exit(j_common_ptr cinfo) {
    idfxx_jpeg_ctx* ctx = ctx_of(cinfo);
    ctx->status = classify(ctx, cinfo->err->msg_code);
    log_message(cinfo);
    longjmp(ctx->jump, 1);
}

static void log_warning(j_common_ptr cinfo) {
    char message[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, message);
    ESP_LOGW(TAG, "libjpeg: %s", message);
}

// A negative level is a warning about corrupt data, which libjpeg would
// otherwise work around; the decode fails instead. Other levels are trace
// messages, which are discarded.
//
// Three warnings are the exception: stray bytes skipped before a marker, a
// JFIF version other than 1.x and an unrecognised Adobe colour transform.
// Real-world files commonly have them (e.g. padding before the EOI marker)
// and decode correctly with libjpeg's workaround, which is what other
// decoders do, so they are logged and the decode continues. Stray bytes can
// also follow damaged scan data, but most such damage is undetectable anyway.
static void emit_message(j_common_ptr cinfo, int msg_level) {
    if (msg_level >= 0) {
        return;
    }
    idfxx_jpeg_ctx* ctx = ctx_of(cinfo);
    switch (cinfo->err->msg_code) {
    case JWRN_EXTRANEOUS_DATA:
    case JWRN_JFIF_MAJOR:
    case JWRN_ADOBE_XFORM:
        cinfo->err->num_warnings++;
        log_warning(cinfo);
        return;
    default:
        break;
    }
    cinfo->err->num_warnings++;
    ctx->corrupt = true;
    ctx->status = classify(ctx, cinfo->err->msg_code);
    log_message(cinfo);
    longjmp(ctx->jump, 1);
}

static void init_source(j_decompress_ptr cinfo) {
    (void)cinfo;
}

static boolean fill_input_buffer(j_decompress_ptr cinfo) {
    idfxx_jpeg_ctx* ctx = ctx_of((j_common_ptr)cinfo);
    size_t count = 0;
    if (!ctx->fill(ctx->user, ctx->buffer, sizeof(ctx->buffer), &count)) {
        ctx->source_failed = true;
        ERREXIT(cinfo, JERR_FILE_READ);
    }
    if (count == 0) {
        ctx->end_of_stream = true;
        ERREXIT(cinfo, JERR_INPUT_EOF);
    }
    ctx->src.next_input_byte = ctx->buffer;
    ctx->src.bytes_in_buffer = count;
    return TRUE;
}

static void skip_input_data(j_decompress_ptr cinfo, long num_bytes) {
    struct jpeg_source_mgr* src = cinfo->src;
    if (num_bytes <= 0) {
        return;
    }
    size_t remaining = (size_t)num_bytes;
    while (remaining > src->bytes_in_buffer) {
        remaining -= src->bytes_in_buffer;
        src->bytes_in_buffer = 0;
        (void)fill_input_buffer(cinfo);
    }
    src->next_input_byte += remaining;
    src->bytes_in_buffer -= remaining;
}

static void term_source(j_decompress_ptr cinfo) {
    (void)cinfo;
}

// --- Working memory ----------------------------------------------------------
//
// The decoder's working memory is counted through the method pointers of
// cinfo->mem, which libjpeg calls for each allocation it makes outside the
// memory manager. The count starts with the context and adds what each request
// asks for, ignoring the memory manager's pool slop and alignment padding.
// jmemmgr's own nested allocations (an array's rows and row pointers, a virtual
// array's control block and storage) call its static functions directly, so
// nothing is counted twice. A virtual array is counted in full when it is
// requested, and checked against the limit when libjpeg realizes it.
//
// Sizes saturate at SIZE_MAX, so an absurd size in an untrusted header exceeds
// any limit instead of wrapping around.

static size_t add_size(size_t a, size_t b) {
    size_t sum;
    return __builtin_add_overflow(a, b, &sum) ? SIZE_MAX : sum;
}

static size_t mul_size(size_t a, size_t b) {
    size_t product;
    return __builtin_mul_overflow(a, b, &product) ? SIZE_MAX : product;
}

// The size of a numrows x row_size array and its row pointers.
static size_t array_size(size_t row_size, JDIMENSION numrows, size_t pointer_size) {
    return add_size(mul_size(row_size, numrows), mul_size(numrows, pointer_size));
}

// The size of one sample, as jmemmgr works it out.
static size_t sample_size(j_common_ptr cinfo) {
    const int precision = ((j_decompress_ptr)cinfo)->data_precision;
    return precision > 12 ? sizeof(J16SAMPLE) : (precision > 8 ? sizeof(J12SAMPLE) : sizeof(JSAMPLE));
}

// Fails the decode if allocating @p size more bytes would take the working
// memory past the limit, once the limit is enforced.
static void check_limit(j_common_ptr cinfo, size_t size) {
    idfxx_jpeg_ctx* ctx = ctx_of(cinfo);
    if (!ctx->limited || !ctx->enforcing || add_size(ctx->allocated, size) <= ctx->memory_limit) {
        return;
    }
    ESP_LOGD(
        TAG,
        "%zu more bytes would exceed the memory limit (%zu of %zu bytes in use)",
        size,
        ctx->allocated,
        ctx->memory_limit
    );
    ctx->over_limit = true;
    // classify() reports the flag; this is libjpeg's own error for exceeding
    // max_memory_to_use.
    ERREXIT(cinfo, JERR_NO_BACKING_STORE);
}

static void count_allocation(j_common_ptr cinfo, size_t size) {
    idfxx_jpeg_ctx* ctx = ctx_of(cinfo);
    ctx->allocated = add_size(ctx->allocated, size);
}

static void* counted_alloc_small(j_common_ptr cinfo, int pool_id, size_t size) {
    check_limit(cinfo, size);
    void* object = (*ctx_of(cinfo)->libjpeg_mem.alloc_small)(cinfo, pool_id, size);
    count_allocation(cinfo, size);
    return object;
}

static void* counted_alloc_large(j_common_ptr cinfo, int pool_id, size_t size) {
    check_limit(cinfo, size);
    void* object = (*ctx_of(cinfo)->libjpeg_mem.alloc_large)(cinfo, pool_id, size);
    count_allocation(cinfo, size);
    return object;
}

static JSAMPARRAY counted_alloc_sarray(j_common_ptr cinfo, int pool_id, JDIMENSION samplesperrow, JDIMENSION numrows) {
    const size_t size = array_size(mul_size(samplesperrow, sample_size(cinfo)), numrows, sizeof(JSAMPROW));
    check_limit(cinfo, size);
    JSAMPARRAY array = (*ctx_of(cinfo)->libjpeg_mem.alloc_sarray)(cinfo, pool_id, samplesperrow, numrows);
    count_allocation(cinfo, size);
    return array;
}

static JBLOCKARRAY counted_alloc_barray(j_common_ptr cinfo, int pool_id, JDIMENSION blocksperrow, JDIMENSION numrows) {
    const size_t size = array_size(mul_size(blocksperrow, sizeof(JBLOCK)), numrows, sizeof(JBLOCKROW));
    check_limit(cinfo, size);
    JBLOCKARRAY array = (*ctx_of(cinfo)->libjpeg_mem.alloc_barray)(cinfo, pool_id, blocksperrow, numrows);
    count_allocation(cinfo, size);
    return array;
}

static jvirt_sarray_ptr counted_request_virt_sarray(
    j_common_ptr cinfo,
    int pool_id,
    boolean pre_zero,
    JDIMENSION samplesperrow,
    JDIMENSION numrows,
    JDIMENSION maxaccess
) {
    idfxx_jpeg_ctx* ctx = ctx_of(cinfo);
    jvirt_sarray_ptr array =
        (*ctx->libjpeg_mem.request_virt_sarray)(cinfo, pool_id, pre_zero, samplesperrow, numrows, maxaccess);
    const size_t size = array_size(mul_size(samplesperrow, sample_size(cinfo)), numrows, sizeof(JSAMPROW));
    ctx->pending = add_size(ctx->pending, size);
    return array;
}

static jvirt_barray_ptr counted_request_virt_barray(
    j_common_ptr cinfo,
    int pool_id,
    boolean pre_zero,
    JDIMENSION blocksperrow,
    JDIMENSION numrows,
    JDIMENSION maxaccess
) {
    idfxx_jpeg_ctx* ctx = ctx_of(cinfo);
    jvirt_barray_ptr array =
        (*ctx->libjpeg_mem.request_virt_barray)(cinfo, pool_id, pre_zero, blocksperrow, numrows, maxaccess);
    const size_t size = array_size(mul_size(blocksperrow, sizeof(JBLOCK)), numrows, sizeof(JBLOCKROW));
    ctx->pending = add_size(ctx->pending, size);
    return array;
}

// libjpeg allocates the requested virtual arrays here, through functions the
// hooks don't see. Each array is allocated in full: libjpeg would otherwise
// need a backing store, which this build doesn't have.
static void counted_realize_virt_arrays(j_common_ptr cinfo) {
    idfxx_jpeg_ctx* ctx = ctx_of(cinfo);
    check_limit(cinfo, ctx->pending);
    (*ctx->libjpeg_mem.realize_virt_arrays)(cinfo);
    count_allocation(cinfo, ctx->pending);
    ctx->pending = 0;
}

static void install_memory_hooks(idfxx_jpeg_ctx* ctx) {
    struct jpeg_memory_mgr* mem = ctx->cinfo.mem;
    ctx->libjpeg_mem = *mem;
    mem->alloc_small = counted_alloc_small;
    mem->alloc_large = counted_alloc_large;
    mem->alloc_sarray = counted_alloc_sarray;
    mem->alloc_barray = counted_alloc_barray;
    mem->request_virt_sarray = counted_request_virt_sarray;
    mem->request_virt_barray = counted_request_virt_barray;
    mem->realize_virt_arrays = counted_realize_virt_arrays;
}

// --- EXIF orientation --------------------------------------------------------
//
// APP1 segments are read by a marker processor rather than saved: only the
// Orientation tag of the first Exif segment is needed, and other APP1 segments
// (XMP, extended XMP, EXIF thumbnails) can be up to 64 KB each. The processor
// streams the TIFF structure through the data source, which never suspends:
// fill_input_buffer() delivers at least one byte or exits with an error.

#define TIFF_HEADER_SIZE 8
#define TIFF_MAGIC 42
#define IFD_ENTRY_SIZE 12
#define ORIENTATION_TAG 0x0112
#define TYPE_SHORT 3

// The unread part of a marker segment.
typedef struct {
    j_decompress_ptr cinfo;
    size_t remaining;
    bool little_endian;
} segment_reader;

static uint8_t next_byte(j_decompress_ptr cinfo) {
    struct jpeg_source_mgr* src = cinfo->src;
    if (src->bytes_in_buffer == 0) {
        (void)(*src->fill_input_buffer)(cinfo);
    }
    src->bytes_in_buffer--;
    return *src->next_input_byte++;
}

// Reads the next @p count bytes of the segment, or returns false, reading
// nothing, if fewer remain.
static bool segment_read(segment_reader* r, uint8_t* out, size_t count) {
    if (count > r->remaining) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        out[i] = next_byte(r->cinfo);
    }
    r->remaining -= count;
    return true;
}

// Skips the next @p count bytes of the segment, or returns false, skipping
// nothing, if fewer remain.
static bool segment_skip(segment_reader* r, size_t count) {
    if (count > r->remaining) {
        return false;
    }
    if (count > 0) {
        (*r->cinfo->src->skip_input_data)(r->cinfo, (long)count);
    }
    r->remaining -= count;
    return true;
}

static uint16_t tiff_u16(const segment_reader* r, const uint8_t* p) {
    return r->little_endian ? (uint16_t)(p[0] | (p[1] << 8)) : (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t tiff_u32(const segment_reader* r, const uint8_t* p) {
    const uint32_t first = tiff_u16(r, p);
    const uint32_t second = tiff_u16(r, p + 2);
    return r->little_endian ? (first | (second << 16)) : ((first << 16) | second);
}

static uint8_t malformed(const char* reason) {
    ESP_LOGW(TAG, "ignoring malformed EXIF data (%s); assuming upright orientation", reason);
    return 0;
}

// Reads the TIFF structure that follows "Exif\0\0", and returns the
// Orientation tag (1-8) from IFD0, or 0 if there is none or it is invalid.
static uint8_t read_orientation(segment_reader* r) {
    uint8_t header[TIFF_HEADER_SIZE];
    if (!segment_read(r, header, sizeof(header))) {
        return malformed("truncated TIFF header");
    }
    if (header[0] == 'I' && header[1] == 'I') {
        r->little_endian = true;
    } else if (header[0] != 'M' || header[1] != 'M') {
        return malformed("unknown byte order");
    }
    if (tiff_u16(r, header + 2) != TIFF_MAGIC) {
        return malformed("bad TIFF magic number");
    }

    // IFD0's offset is from the start of the TIFF data. It must be past the
    // header, which has already been read.
    const uint32_t ifd = tiff_u32(r, header + 4);
    uint8_t count[2];
    if (ifd < TIFF_HEADER_SIZE || !segment_skip(r, ifd - TIFF_HEADER_SIZE) || !segment_read(r, count, sizeof(count))) {
        return malformed("IFD0 out of range");
    }
    const size_t entry_count = tiff_u16(r, count);
    if (r->remaining / IFD_ENTRY_SIZE < entry_count) {
        return malformed("truncated IFD0");
    }

    for (size_t i = 0; i < entry_count; ++i) {
        uint8_t entry[IFD_ENTRY_SIZE];
        if (!segment_read(r, entry, sizeof(entry))) {
            return malformed("truncated IFD0");
        }
        if (tiff_u16(r, entry) != ORIENTATION_TAG) {
            continue;
        }
        if (tiff_u16(r, entry + 2) != TYPE_SHORT) {
            return malformed("Orientation tag is not of type SHORT");
        }
        if (tiff_u32(r, entry + 4) != 1) {
            return malformed("Orientation tag count is not 1");
        }
        // A single SHORT is stored in the first two bytes of the value field.
        const uint16_t value = tiff_u16(r, entry + 8);
        if (value < 1 || value > 8) {
            ESP_LOGD(TAG, "EXIF Orientation %u is out of range; assuming upright orientation", (unsigned)value);
            return 0;
        }
        return (uint8_t)value;
    }
    return 0;
}

// Reads the segment's first six bytes (none if it is shorter), and returns true
// if they are "Exif\0\0".
static bool read_exif_prefix(segment_reader* r) {
    static const uint8_t exif_prefix[6] = {'E', 'x', 'i', 'f', 0, 0};
    uint8_t prefix[sizeof(exif_prefix)];
    return segment_read(r, prefix, sizeof(prefix)) && memcmp(prefix, exif_prefix, sizeof(prefix)) == 0;
}

// Reads the orientation from the first APP1 segment that starts with
// "Exif\0\0", and skips every other APP1 segment whole. Consumes exactly the
// segment, whatever its contents.
static boolean process_app1(j_decompress_ptr cinfo) {
    idfxx_jpeg_ctx* ctx = ctx_of((j_common_ptr)cinfo);
    const unsigned high = next_byte(cinfo);
    const unsigned length = (high << 8) | next_byte(cinfo);
    // The length counts its own two bytes. Like libjpeg, read nothing more if
    // it is too small to.
    if (length < 2) {
        return TRUE;
    }
    segment_reader r = {.cinfo = cinfo, .remaining = length - 2, .little_endian = false};
    if (!ctx->exif_done && read_exif_prefix(&r)) {
        ctx->exif_done = true;
        ctx->orientation = read_orientation(&r);
    }
    (void)segment_skip(&r, r.remaining);
    return TRUE;
}

idfxx_jpeg_status idfxx_jpeg_create(
    idfxx_jpeg_fill_fn fill,
    void* user,
    bool limited,
    size_t memory_limit,
    idfxx_jpeg_ctx** out
) {
    idfxx_jpeg_ctx* ctx = calloc(1, sizeof(*ctx));
    *out = ctx;
    if (ctx == NULL) {
        return IDFXX_JPEG_NO_MEM;
    }
    ctx->fill = fill;
    ctx->user = user;
    ctx->limited = limited;
    ctx->memory_limit = memory_limit;
    ctx->allocated = sizeof(*ctx);
    ctx->cinfo.err = jpeg_std_error(&ctx->err);
    ctx->err.error_exit = error_exit;
    ctx->err.emit_message = emit_message;
    ctx->cinfo.client_data = ctx;
    if (setjmp(ctx->jump)) {
        return ctx->status;
    }
    jpeg_create_decompress(&ctx->cinfo);
    install_memory_hooks(ctx);
    if (limited) {
        // libjpeg checks its own count against this when it realizes virtual
        // arrays. It treats 0 as no limit, so a limit of 0 becomes 1.
        long max_memory = LONG_MAX;
        if (memory_limit < (size_t)LONG_MAX) {
            max_memory = memory_limit > 0 ? (long)memory_limit : 1;
        }
        ctx->cinfo.mem->max_memory_to_use = max_memory;
    }
    ctx->src.init_source = init_source;
    ctx->src.fill_input_buffer = fill_input_buffer;
    ctx->src.skip_input_data = skip_input_data;
    ctx->src.resync_to_restart = jpeg_resync_to_restart;
    ctx->src.term_source = term_source;
    ctx->src.next_input_byte = NULL;
    ctx->src.bytes_in_buffer = 0;
    ctx->cinfo.src = &ctx->src;
    jpeg_set_marker_processor(&ctx->cinfo, JPEG_APP0 + 1, process_app1);
    return IDFXX_JPEG_OK;
}

idfxx_jpeg_status idfxx_jpeg_read_header(idfxx_jpeg_ctx* ctx, idfxx_jpeg_header* out) {
    if (setjmp(ctx->jump)) {
        return ctx->status;
    }
    struct jpeg_decompress_struct* cinfo = &ctx->cinfo;
    (void)jpeg_read_header(cinfo, TRUE);
    // An Exif segment after the headers comes too late to be reported.
    ctx->exif_done = true;

    if (cinfo->data_precision != 8) {
        ESP_LOGD(TAG, "unsupported data precision %d", cinfo->data_precision);
        return IDFXX_JPEG_NOT_SUPPORTED;
    }
    // Lossless JPEGs have a one-sample "block" instead of an 8x8 DCT block.
    if (cinfo->block_size != DCTSIZE) {
        ESP_LOGD(TAG, "lossless JPEG is not supported");
        return IDFXX_JPEG_NOT_SUPPORTED;
    }
    switch (cinfo->jpeg_color_space) {
    case JCS_GRAYSCALE:
    case JCS_YCbCr:
    case JCS_RGB:
        break;
    default:
        ESP_LOGD(TAG, "unsupported color space %d", (int)cinfo->jpeg_color_space);
        return IDFXX_JPEG_NOT_SUPPORTED;
    }

    cinfo->out_color_space = JCS_EXT_RGB;
    cinfo->scale_num = 8;
    cinfo->scale_denom = 8;
    jpeg_calc_output_dimensions(cinfo);

    out->width = cinfo->image_width;
    out->height = cinfo->image_height;
    out->output_width = cinfo->output_width;
    out->output_height = cinfo->output_height;
    out->progressive = cinfo->progressive_mode != FALSE;
    out->orientation = ctx->orientation;
    return IDFXX_JPEG_OK;
}

idfxx_jpeg_status idfxx_jpeg_calc_output(idfxx_jpeg_ctx* ctx, unsigned eighths, uint32_t* width, uint32_t* height) {
    if (setjmp(ctx->jump)) {
        return ctx->status;
    }
    ctx->cinfo.scale_num = eighths;
    ctx->cinfo.scale_denom = 8;
    jpeg_calc_output_dimensions(&ctx->cinfo);
    *width = ctx->cinfo.output_width;
    *height = ctx->cinfo.output_height;
    return IDFXX_JPEG_OK;
}

idfxx_jpeg_status idfxx_jpeg_start(idfxx_jpeg_ctx* ctx, unsigned eighths, uint32_t* width, uint32_t* height) {
    if (setjmp(ctx->jump)) {
        return ctx->status;
    }
    // Everything counted so far, from the headers, counts towards the limit.
    ctx->enforcing = true;
    check_limit((j_common_ptr)&ctx->cinfo, 0);
    ctx->cinfo.scale_num = eighths;
    ctx->cinfo.scale_denom = 8;
    (void)jpeg_start_decompress(&ctx->cinfo);
    *width = ctx->cinfo.output_width;
    *height = ctx->cinfo.output_height;
    return IDFXX_JPEG_OK;
}

idfxx_jpeg_status idfxx_jpeg_alloc_row(idfxx_jpeg_ctx* ctx, uint8_t** row) {
    if (setjmp(ctx->jump)) {
        return ctx->status;
    }
    struct jpeg_decompress_struct* cinfo = &ctx->cinfo;
    const size_t size = (size_t)cinfo->output_width * (size_t)cinfo->output_components;
    // Through the counting hook, in the pool that finishing or abandoning the
    // decompression frees.
    *row = (*cinfo->mem->alloc_large)((j_common_ptr)cinfo, JPOOL_IMAGE, size);
    return IDFXX_JPEG_OK;
}

idfxx_jpeg_status idfxx_jpeg_read_row(idfxx_jpeg_ctx* ctx, uint8_t* row) {
    ctx->row = row;
    if (setjmp(ctx->jump)) {
        return ctx->status;
    }
    // Without a suspending data source, libjpeg always produces the row.
    if (jpeg_read_scanlines(&ctx->cinfo, &ctx->row, 1) != 1) {
        return IDFXX_JPEG_INVALID_DATA;
    }
    return IDFXX_JPEG_OK;
}

idfxx_jpeg_status idfxx_jpeg_finish(idfxx_jpeg_ctx* ctx) {
    if (setjmp(ctx->jump)) {
        return ctx->status;
    }
    (void)jpeg_finish_decompress(&ctx->cinfo);
    return IDFXX_JPEG_OK;
}

void idfxx_jpeg_abort(idfxx_jpeg_ctx* ctx) {
    if (setjmp(ctx->jump)) {
        return;
    }
    jpeg_abort_decompress(&ctx->cinfo);
}

void idfxx_jpeg_destroy(idfxx_jpeg_ctx* ctx) {
    if (ctx == NULL) {
        return;
    }
    if (setjmp(ctx->jump) == 0) {
        // A no-op if jpeg_create_decompress() never got as far as creating
        // the memory manager.
        jpeg_destroy_decompress(&ctx->cinfo);
    }
    free(ctx);
}
