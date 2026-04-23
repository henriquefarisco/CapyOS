#include "gui/png_loader.h"
#include "memory/kmem.h"
#include "util/kstring.h"
#include "tinf.h"
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Constants and limits                                                 */
/* ------------------------------------------------------------------ */

#define PNG_SIG_LEN     8
#define PNG_MAX_DIM     1024        /* max width or height in pixels */
#define PNG_MAX_PIXELS  (PNG_MAX_DIM * PNG_MAX_DIM)
#define PNG_MAX_RAW     (PNG_MAX_PIXELS * 5)  /* raw scanlines + filter bytes */

/* ------------------------------------------------------------------ */
/* Big-endian read helpers                                              */
/* ------------------------------------------------------------------ */

static uint32_t png_u32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* ------------------------------------------------------------------ */
/* PNG signature                                                        */
/* ------------------------------------------------------------------ */

static const uint8_t png_sig[PNG_SIG_LEN] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
};

/* ------------------------------------------------------------------ */
/* Paeth predictor (filter type 4)                                      */
/* ------------------------------------------------------------------ */

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p  = (int)a + (int)b - (int)c;
    int pa = p - (int)a; if (pa < 0) pa = -pa;
    int pb = p - (int)b; if (pb < 0) pb = -pb;
    int pc = p - (int)c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/* ------------------------------------------------------------------ */
/* Main decoder                                                         */
/* ------------------------------------------------------------------ */

int png_decode(const uint8_t *data, size_t len, struct png_image *out) {
    uint32_t width = 0, height = 0;
    uint8_t  bit_depth = 0, color_type = 0;
    uint8_t  interlace = 0;
    int      channels = 0;
    uint8_t *idat_buf = NULL;
    size_t   idat_len = 0, idat_cap = 0;
    uint8_t *raw = NULL;
    uint32_t *pixels = NULL;
    size_t   pos = 0;
    int      ok = 0;

    if (!data || !out) return -1;
    kmemzero(out, sizeof(*out));

    /* Verify PNG signature */
    if (len < PNG_SIG_LEN) return -1;
    if (kmemcmp(data, png_sig, PNG_SIG_LEN) != 0) return -1;
    pos = PNG_SIG_LEN;

    /* Allocate IDAT accumulation buffer */
    idat_cap = 65536;
    idat_buf = (uint8_t *)kalloc(idat_cap);
    if (!idat_buf) return -1;

    /* Parse chunks */
    while (pos + 8 <= len) {
        uint32_t chunk_len  = png_u32be(data + pos);     pos += 4;
        uint32_t chunk_type = png_u32be(data + pos);     pos += 4;
        const uint8_t *chunk_data = data + pos;

        if (pos + chunk_len + 4 > len) break;  /* truncated */

        /* IHDR */
        if (chunk_type == 0x49484452UL && chunk_len >= 13) {
            width      = png_u32be(chunk_data);
            height     = png_u32be(chunk_data + 4);
            bit_depth  = chunk_data[8];
            color_type = chunk_data[9];
            interlace  = chunk_data[12];
        }
        /* IDAT */
        else if (chunk_type == 0x49444154UL) {
            if (idat_len + chunk_len > idat_cap) {
                size_t new_cap = idat_cap + chunk_len + 65536;
                uint8_t *nb = (uint8_t *)kalloc(new_cap);
                if (!nb) goto done;
                kmemcpy(nb, idat_buf, idat_len);
                kfree(idat_buf);
                idat_buf = nb;
                idat_cap = new_cap;
            }
            kmemcpy(idat_buf + idat_len, chunk_data, chunk_len);
            idat_len += chunk_len;
        }
        /* IEND */
        else if (chunk_type == 0x49454E44UL) {
            break;
        }

        pos += chunk_len + 4;  /* skip data + CRC */
    }

    /* Validate */
    if (width == 0 || height == 0 || width > PNG_MAX_DIM || height > PNG_MAX_DIM)
        goto done;
    if (bit_depth != 8) goto done;  /* only 8-bit supported */
    if (interlace != 0) goto done;  /* no interlace support */

    switch (color_type) {
        case 0: channels = 1; break;  /* grayscale */
        case 2: channels = 3; break;  /* RGB */
        case 4: channels = 2; break;  /* grayscale + alpha */
        case 6: channels = 4; break;  /* RGBA */
        default: goto done;
    }

    /* Decompress IDAT (zlib) */
    {
        size_t   raw_size = (size_t)(width * channels + 1) * height;
        unsigned raw_u    = (unsigned)raw_size;
        int      rc;

        if (raw_size > PNG_MAX_RAW) goto done;
        raw = (uint8_t *)kalloc(raw_size);
        if (!raw) goto done;

        rc = tinf_zlib_uncompress(raw, &raw_u, idat_buf, (unsigned)idat_len);
        if (rc != TINF_OK || (size_t)raw_u < raw_size) goto done;
    }

    /* Allocate output pixel buffer */
    pixels = (uint32_t *)kalloc(width * height * 4);
    if (!pixels) goto done;

    /* Reconstruct filtered scanlines and convert to ARGB32 */
    {
        size_t stride = (size_t)width * (size_t)channels + 1; /* +1 for filter byte */
        for (uint32_t y = 0; y < height; y++) {
            const uint8_t *row  = raw + y * stride;
            const uint8_t *prev = y > 0 ? raw + (y - 1) * stride : NULL;
            uint8_t ftype = row[0];
            const uint8_t *in   = row + 1;
            size_t row_bytes    = (size_t)width * (size_t)channels;
            uint8_t *recon      = (uint8_t *)kalloc(row_bytes);
            if (!recon) goto done;

            for (size_t i = 0; i < row_bytes; i++) {
                uint8_t a = i >= (size_t)channels ? recon[i - channels] : 0;
                uint8_t b = prev ? prev[1 + i] : 0;
                uint8_t c = (prev && i >= (size_t)channels)
                            ? prev[1 + i - channels] : 0;
                switch (ftype) {
                    case 0: recon[i] = in[i]; break;
                    case 1: recon[i] = (uint8_t)(in[i] + a); break;
                    case 2: recon[i] = (uint8_t)(in[i] + b); break;
                    case 3: recon[i] = (uint8_t)(in[i] + ((a + b) >> 1)); break;
                    case 4: recon[i] = (uint8_t)(in[i] + paeth(a, b, c)); break;
                    default: recon[i] = in[i]; break;
                }
            }

            /* Convert to ARGB32 */
            for (uint32_t x = 0; x < width; x++) {
                uint8_t r = 0, g = 0, b = 0, a_ch = 0xFF;
                switch (channels) {
                    case 1:
                        r = g = b = recon[x];
                        break;
                    case 2:
                        r = g = b = recon[x * 2];
                        a_ch = recon[x * 2 + 1];
                        break;
                    case 3:
                        r = recon[x * 3];
                        g = recon[x * 3 + 1];
                        b = recon[x * 3 + 2];
                        break;
                    case 4:
                        r = recon[x * 4];
                        g = recon[x * 4 + 1];
                        b = recon[x * 4 + 2];
                        a_ch = recon[x * 4 + 3];
                        break;
                }
                pixels[y * width + x] = ((uint32_t)a_ch << 24) |
                                        ((uint32_t)r    << 16) |
                                        ((uint32_t)g    <<  8) |
                                         (uint32_t)b;
            }
            kfree(recon);
        }
    }

    out->width  = width;
    out->height = height;
    out->pixels = pixels;
    pixels = NULL;  /* ownership transferred */
    ok = 1;

done:
    if (idat_buf) kfree(idat_buf);
    if (raw)      kfree(raw);
    if (pixels)   kfree(pixels);
    return ok ? 0 : -1;
}

void png_free(struct png_image *img) {
    if (!img) return;
    if (img->pixels) kfree(img->pixels);
    kmemzero(img, sizeof(*img));
}
