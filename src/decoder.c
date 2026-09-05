#include "decoder.h"

#include <GLFW/glfw3.h>
#include <avif/avif.h>
#include <jxl/decode.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <turbojpeg.h>
#include <unistd.h>
#include <webp/decode.h>

typedef struct {
    PageDecoder *dec;
    int thread_id;
    tjhandle tj;
    zip_t *za;
    unsigned char *cmyk_buf;
    size_t cmyk_buf_cap;
} WorkerContext;

static int decode_jpeg(WorkerContext *ctx, const unsigned char *compressed, size_t size,
                       int *out_w, int *out_h, int *out_channels, unsigned char **out_pixels, size_t *buf_cap)
{
    int width, height, subsamp, colorspace;
    if (tjDecompressHeader3(ctx->tj, compressed, (unsigned long)size,
        &width, &height, &subsamp, &colorspace) != 0)
        return 0;

    if (width <= 0 || height <= 0 || (size_t)width * (size_t)height > SIZE_MAX / 4)
        return 0;

    size_t needed = (size_t)width * (size_t)height * 3;
    if (*buf_cap < needed) {
        free(*out_pixels);
        *out_pixels = malloc(needed);
        if (!*out_pixels) {
            *buf_cap = 0;
            return 0;
        }
        *buf_cap = needed;
    }

    if (colorspace == TJCS_CMYK || colorspace == TJCS_YCCK) {
        size_t cmyk_needed = (size_t)width * (size_t)height * 4;
        if (ctx->cmyk_buf_cap < cmyk_needed) {
            free(ctx->cmyk_buf);
            ctx->cmyk_buf = malloc(cmyk_needed);
            if (!ctx->cmyk_buf) {
                ctx->cmyk_buf_cap = 0;
                return 0;
            }
            ctx->cmyk_buf_cap = cmyk_needed;
        }

        if (tjDecompress2(ctx->tj, compressed, (unsigned long)size,
            ctx->cmyk_buf, width, 0, height, TJPF_CMYK, TJFLAG_FASTDCT) != 0)
            return 0;

        /* Convert CMYK to RGB directly into destination buffer */
        const unsigned char *src = ctx->cmyk_buf;
        unsigned char *dst = *out_pixels;
        size_t total_pixels = (size_t)width * (size_t)height;

        for (size_t i = 0; i < total_pixels; i++) {
            unsigned int c = src[i * 4 + 0];
            unsigned int m = src[i * 4 + 1];
            unsigned int y = src[i * 4 + 2];
            unsigned int k = src[i * 4 + 3];

            dst[i * 3 + 0] = (unsigned char)(((255 - c) * (255 - k)) / 255);
            dst[i * 3 + 1] = (unsigned char)(((255 - m) * (255 - k)) / 255);
            dst[i * 3 + 2] = (unsigned char)(((255 - y) * (255 - k)) / 255);
        }
    } else {
        if (tjDecompress2(ctx->tj, compressed, (unsigned long)size,
            *out_pixels, width, 0, height, TJPF_RGB, TJFLAG_FASTDCT) != 0)
            return 0;
    }

    *out_w = width;
    *out_h = height;
    *out_channels = 3;
    return 1;
}

static int decode_webp(const unsigned char *compressed, size_t size,
                       int *out_w, int *out_h, int *out_channels, unsigned char **out_pixels, size_t *buf_cap)
{
    int width, height;
    if (!WebPGetInfo(compressed, size, &width, &height) || width <= 0 || height <= 0)
        return 0;

    size_t needed = (size_t)width * (size_t)height * 4;
    if (*buf_cap < needed) {
        free(*out_pixels);
        *out_pixels = malloc(needed);
        if (!*out_pixels) {
            *buf_cap = 0;
            return 0;
        }
        *buf_cap = needed;
    }

    if (!WebPDecodeRGBAInto(compressed, size, *out_pixels, needed, width * 4))
        return 0;

    *out_w = width;
    *out_h = height;
    *out_channels = 4;
    return 1;
}

static int decode_png(const unsigned char *compressed, size_t size,
                      int *out_w, int *out_h, int *out_channels, unsigned char **out_pixels, size_t *buf_cap)
{
    png_image image = {0};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, compressed, size))
        return 0;

    image.format = PNG_FORMAT_RGBA;
    size_t needed = PNG_IMAGE_SIZE(image);

    if (*buf_cap < needed) {
        free(*out_pixels);
        *out_pixels = malloc(needed);
        if (!*out_pixels) {
            png_image_free(&image);
            *buf_cap = 0;
            return 0;
        }
        *buf_cap = needed;
    }

    if (!png_image_finish_read(&image, NULL, *out_pixels, 0, NULL)) {
        png_image_free(&image);
        return 0;
    }

    *out_w = (int)image.width;
    *out_h = (int)image.height;
    *out_channels = 4;
    png_image_free(&image);
    return 1;
}

static int decode_avif(const unsigned char *compressed, size_t size,
                       int *out_w, int *out_h, int *out_channels, unsigned char **out_pixels, size_t *buf_cap)
{
    avifDecoder *decoder = avifDecoderCreate();
    if (!decoder)
        return 0;

    if (avifDecoderSetIOMemory(decoder, compressed, size) != AVIF_RESULT_OK ||
        avifDecoderParse(decoder) != AVIF_RESULT_OK ||
        avifDecoderNextImage(decoder) != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        return 0;
    }

    int w = (int)decoder->image->width;
    int h = (int)decoder->image->height;
    size_t needed = (size_t)w * (size_t)h * 4;

    if (*buf_cap < needed) {
        free(*out_pixels);
        *out_pixels = malloc(needed);
        if (!*out_pixels) {
            *buf_cap = 0;
            avifDecoderDestroy(decoder);
            return 0;
        }
        *buf_cap = needed;
    }

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, decoder->image);
    rgb.format = AVIF_RGB_FORMAT_RGBA;
    rgb.depth = 8;
    rgb.pixels = *out_pixels;
    rgb.rowBytes = (uint32_t)(w * 4);

    avifResult res = avifImageYUVToRGB(decoder->image, &rgb);
    avifDecoderDestroy(decoder);
    if (res != AVIF_RESULT_OK)
        return 0;

    *out_w = w;
    *out_h = h;
    *out_channels = 4;
    return 1;
}

static int decode_jxl(const unsigned char *compressed, size_t size,
                      int *out_w, int *out_h, int *out_channels, unsigned char **out_pixels, size_t *buf_cap)
{
    JxlDecoder *dec = JxlDecoderCreate(NULL);
    if (!dec)
        return 0;

    if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
        JxlDecoderDestroy(dec);
        return 0;
    }

    JxlDecoderSetInput(dec, compressed, size);
    JxlDecoderCloseInput(dec);

    JxlBasicInfo info;
    JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    int w = 0, h = 0;

    for (;;) {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec);
        if (status == JXL_DEC_ERROR || status == JXL_DEC_NEED_MORE_INPUT) {
            JxlDecoderDestroy(dec);
            return 0;
        }
        if (status == JXL_DEC_BASIC_INFO) {
            if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) {
                JxlDecoderDestroy(dec);
                return 0;
            }
            w = (int)info.xsize;
            h = (int)info.ysize;
            size_t needed = (size_t)w * (size_t)h * 4;
            if (*buf_cap < needed) {
                free(*out_pixels);
                *out_pixels = malloc(needed);
                if (!*out_pixels) {
                    *buf_cap = 0;
                    JxlDecoderDestroy(dec);
                    return 0;
                }
                *buf_cap = needed;
            }
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            size_t buffer_size = (size_t)w * (size_t)h * 4;
            if (JxlDecoderSetImageOutBuffer(dec, &format, *out_pixels, buffer_size) != JXL_DEC_SUCCESS) {
                JxlDecoderDestroy(dec);
                return 0;
            }
        } else if (status == JXL_DEC_FULL_IMAGE || status == JXL_DEC_SUCCESS) {
            break;
        }
    }

    JxlDecoderDestroy(dec);
    *out_w = w;
    *out_h = h;
    *out_channels = 4;
    return 1;
}

static int decode_image(WorkerContext *ctx, const unsigned char *compressed, size_t size,
                        int *out_w, int *out_h, int *out_channels, unsigned char **out_pixels, size_t *buf_cap)
{
    if (size >= 2 && compressed[0] == 0xff && compressed[1] == 0xd8)
        return decode_jpeg(ctx, compressed, size, out_w, out_h, out_channels, out_pixels, buf_cap);
    if (size >= 12 && memcmp(compressed, "RIFF", 4) == 0 && memcmp(compressed + 8, "WEBP", 4) == 0)
        return decode_webp(compressed, size, out_w, out_h, out_channels, out_pixels, buf_cap);
    if (size >= 8 && png_sig_cmp((png_const_bytep)compressed, 0, 8) == 0)
        return decode_png(compressed, size, out_w, out_h, out_channels, out_pixels, buf_cap);

    avifROData raw = {(const uint8_t *)compressed, size};
    if (avifPeekCompatibleFileType(&raw))
        return decode_avif(compressed, size, out_w, out_h, out_channels, out_pixels, buf_cap);

    if (JxlSignatureCheck(compressed, size) >= JXL_SIG_CODESTREAM)
        return decode_jxl(compressed, size, out_w, out_h, out_channels, out_pixels, buf_cap);

    return 0;
}

static int select_eviction_slot(PageDecoder *dec, int target_page)
{
    int best_slot = -1;
    int max_dist = -1;
    uint64_t oldest_access = UINT64_MAX;

    for (int i = 0; i < CACHE_CAPACITY; i++) {
        if (!dec->slots[i].in_progress) {
            if (dec->slots[i].page_idx == -1)
                return i;

            int dist = abs(dec->slots[i].page_idx - target_page);
            if (dist > max_dist || (dist == max_dist && dec->slots[i].last_access < oldest_access)) {
                max_dist = dist;
                oldest_access = dec->slots[i].last_access;
                best_slot = i;
            }
        }
    }
    return best_slot;
}

static void *worker_thread_func(void *arg)
{
    WorkerContext *ctx = arg;
    PageDecoder *dec = ctx->dec;
    ctx->tj = tjInitDecompress();
    ctx->za = archive_open_worker_handle(dec->arch);

    unsigned char *local_buf = NULL;
    size_t local_buf_cap = 0;

    while (1) {
        pthread_mutex_lock(&dec->mutex);
        while (dec->running && dec->priority_count == 0)
            pthread_cond_wait(&dec->cond, &dec->mutex);

        if (!dec->running) {
            pthread_mutex_unlock(&dec->mutex);
            break;
        }

        int target_page = -1;
        int target_slot = -1;

        for (int i = 0; i < dec->priority_count; i++) {
            int p = dec->priority_queue[i];
            int already = 0;
            for (int s = 0; s < CACHE_CAPACITY; s++) {
                if (dec->slots[s].page_idx == p &&
                    (dec->slots[s].is_ready || dec->slots[s].in_progress || dec->slots[s].is_failed)) {
                    already = 1;
                break;
                    }
            }
            if (!already) {
                target_slot = select_eviction_slot(dec, dec->current_requested_page);
                if (target_slot >= 0) {
                    target_page = p;
                    dec->slots[target_slot].page_idx = p;
                    dec->slots[target_slot].in_progress = 1;
                    dec->slots[target_slot].is_ready = 0;
                    dec->slots[target_slot].is_failed = 0;
                    break;
                }
            }
        }

        if (target_page == -1) {
            dec->priority_count = 0;
            pthread_mutex_unlock(&dec->mutex);
            continue;
        }

        pthread_mutex_unlock(&dec->mutex);

        size_t compressed_size = 0;
        unsigned char *compressed = archive_read_file_worker(dec->arch, ctx->za, target_page, &compressed_size);

        int w = 0, h = 0, ch = 0, success = 0;
        if (compressed) {
            success = decode_image(ctx, compressed, compressed_size,
                                   &w, &h, &ch, &local_buf, &local_buf_cap);
            free(compressed);
        }

        pthread_mutex_lock(&dec->mutex);
        if (target_slot >= 0 && dec->slots[target_slot].page_idx == target_page) {
            if (success) {
                unsigned char *tmp_ptr = dec->slots[target_slot].rgba;
                size_t tmp_cap = dec->slots[target_slot].buffer_capacity;

                dec->slots[target_slot].rgba = local_buf;
                dec->slots[target_slot].buffer_capacity = local_buf_cap;
                dec->slots[target_slot].width = w;
                dec->slots[target_slot].height = h;
                dec->slots[target_slot].channels = ch;
                dec->slots[target_slot].is_ready = 1;
                dec->slots[target_slot].is_failed = 0;
                dec->slots[target_slot].in_progress = 0;
                dec->slots[target_slot].last_access = ++dec->tick_counter;

                local_buf = tmp_ptr;
                local_buf_cap = tmp_cap;
            } else {
                dec->slots[target_slot].is_ready = 0;
                dec->slots[target_slot].is_failed = 1;
                dec->slots[target_slot].in_progress = 0;
            }
        }
        pthread_mutex_unlock(&dec->mutex);

        glfwPostEmptyEvent();
    }

    if (ctx->za)
        archive_close_worker_handle(ctx->za);
    if (ctx->tj)
        tjDestroy(ctx->tj);
    free(ctx->cmyk_buf);
    free(local_buf);
    free(ctx);
    return NULL;
}

PageDecoder *decoder_init(CBZArchive *arch)
{
    PageDecoder *dec = calloc(1, sizeof(*dec));
    if (!dec)
        return NULL;

    dec->arch = arch;
    dec->running = 1;
    dec->current_requested_page = -1;

    for (int i = 0; i < CACHE_CAPACITY; i++)
        dec->slots[i].page_idx = -1;

    pthread_mutex_init(&dec->mutex, NULL);
    pthread_cond_init(&dec->cond, NULL);

    int nprocs = get_nprocs();
    dec->num_threads = nprocs > 1 ? nprocs - 1 : 1;
    if (dec->num_threads > 8)
        dec->num_threads = 8;

    dec->threads = calloc((size_t)dec->num_threads, sizeof(pthread_t));
    for (int i = 0; i < dec->num_threads; i++) {
        WorkerContext *ctx = calloc(1, sizeof(*ctx));
        ctx->dec = dec;
        ctx->thread_id = i;
        if (pthread_create(&dec->threads[i], NULL, worker_thread_func, ctx) != 0) {
            free(ctx);
            break;
        }
    }

    return dec;
}

void decoder_request_page(PageDecoder *dec, int page_idx, int direction)
{
    pthread_mutex_lock(&dec->mutex);
    dec->current_requested_page = page_idx;
    dec->priority_count = 0;

    int dir = (direction >= 0) ? 1 : -1;

    int candidates[CACHE_CAPACITY];
    int count = 0;

    candidates[count++] = page_idx;
    candidates[count++] = page_idx + 1 * dir;
    candidates[count++] = page_idx + 2 * dir;
    candidates[count++] = page_idx - 1 * dir;
    candidates[count++] = page_idx + 3 * dir;
    candidates[count++] = page_idx + 4 * dir;
    candidates[count++] = page_idx - 2 * dir;
    candidates[count++] = page_idx + 5 * dir;

    for (int i = 0; i < count && dec->priority_count < CACHE_CAPACITY; i++) {
        int p = candidates[i];
        if (p >= 0 && p < dec->arch->total_pages) {
            dec->priority_queue[dec->priority_count++] = p;
        }
    }

    pthread_cond_broadcast(&dec->cond);
    pthread_mutex_unlock(&dec->mutex);
}

int decoder_get_slot_index_locked(PageDecoder *dec, int page_idx)
{
    for (int i = 0; i < CACHE_CAPACITY; i++) {
        if (dec->slots[i].page_idx == page_idx && dec->slots[i].is_ready) {
            dec->slots[i].last_access = ++dec->tick_counter;
            return i;
        }
    }
    return -1;
}

DecodedSlot *decoder_get_slot_locked(PageDecoder *dec, int page_idx)
{
    int idx = decoder_get_slot_index_locked(dec, page_idx);
    return (idx >= 0) ? &dec->slots[idx] : NULL;
}

DecodedSlot *decoder_get_slot(PageDecoder *dec, int page_idx)
{
    pthread_mutex_lock(&dec->mutex);
    DecodedSlot *slot = decoder_get_slot_locked(dec, page_idx);
    pthread_mutex_unlock(&dec->mutex);
    return slot;
}

void decoder_lock(PageDecoder *dec)
{
    pthread_mutex_lock(&dec->mutex);
}

void decoder_unlock(PageDecoder *dec)
{
    pthread_mutex_unlock(&dec->mutex);
}

void decoder_cleanup(PageDecoder *dec)
{
    if (!dec)
        return;

    pthread_mutex_lock(&dec->mutex);
    dec->running = 0;
    pthread_cond_broadcast(&dec->cond);
    pthread_mutex_unlock(&dec->mutex);

    for (int i = 0; i < dec->num_threads; i++) {
        if (dec->threads[i])
            pthread_join(dec->threads[i], NULL);
    }
    free(dec->threads);

    for (int i = 0; i < CACHE_CAPACITY; i++)
        free(dec->slots[i].rgba);

    pthread_mutex_destroy(&dec->mutex);
    pthread_cond_destroy(&dec->cond);
    free(dec);
}
