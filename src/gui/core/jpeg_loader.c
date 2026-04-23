#include "gui/jpeg_loader.h"
#include "memory/kmem.h"
#include "util/kstring.h"
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Limits                                                               */
/* ------------------------------------------------------------------ */

#define JPEG_MAX_DIM    2048
#define JPEG_MAX_COMPS  3

/* ------------------------------------------------------------------ */
/* JPEG markers                                                         */
/* ------------------------------------------------------------------ */

#define JPEG_SOI  0xD8
#define JPEG_EOI  0xD9
#define JPEG_SOF0 0xC0
#define JPEG_SOF2 0xC2
#define JPEG_DHT  0xC4
#define JPEG_DQT  0xDB
#define JPEG_SOS  0xDA
#define JPEG_DRI  0xDD
#define JPEG_RST0 0xD0
#define JPEG_RST7 0xD7
#define JPEG_APP0 0xE0
#define JPEG_APP15 0xEF
#define JPEG_COM  0xFE

/* ------------------------------------------------------------------ */
/* Zigzag decode order                                                  */
/* ------------------------------------------------------------------ */

static const uint8_t jpeg_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
};

/* ------------------------------------------------------------------ */
/* Cosine table: cos_table[u][i] = round(cos((2i+1)*u*pi/16) * 128)   */
/* ------------------------------------------------------------------ */

static const int16_t jpeg_cos[8][8] = {
    { 128, 128, 128, 128, 128, 128, 128, 128 },
    { 126, 106,  71,  25, -25, -71,-106,-126 },
    { 119,  49, -49,-119,-119, -49,  49, 119 },
    { 106, -25,-126, -71,  71, 126,  25,-106 },
    {  91, -91, -91,  91,  91, -91, -91,  91 },
    {  71,-126,  25, 106,-106, -25, 126, -71 },
    {  49,-119, 119, -49, -49, 119,-119,  49 },
    {  25, -71, 106,-126, 126,-106,  71, -25 },
};

#define JPEG_C0 91   /* round(128/sqrt(2)) for DC component */
#define JPEG_C1 128  /* scale for AC components */

/* ------------------------------------------------------------------ */
/* Huffman table                                                         */
/* ------------------------------------------------------------------ */

struct jpeg_huff {
    uint8_t  bits[17];     /* bits[i] = number of codes of length i (1..16) */
    uint8_t  vals[256];    /* code values in canonical order */
    int      maxcode[18];  /* largest code value for length i; -1 if none */
    int      delta[17];    /* vals[] index offset for length i */
    int      built;
};

/* ------------------------------------------------------------------ */
/* Component descriptor                                                  */
/* ------------------------------------------------------------------ */

struct jpeg_comp {
    uint8_t id;
    uint8_t h_samp;
    uint8_t v_samp;
    uint8_t qtable_idx;
    uint8_t dc_huff_idx;
    uint8_t ac_huff_idx;
    int     dc_pred;
};

/* ------------------------------------------------------------------ */
/* Decoder context                                                       */
/* ------------------------------------------------------------------ */

struct jpeg_ctx {
    const uint8_t *data;
    size_t         len;
    size_t         pos;

    uint32_t width;
    uint32_t height;
    uint8_t  precision;
    uint8_t  ncomp;

    int16_t          qtable[4][64];
    struct jpeg_huff dc_huff[4];
    struct jpeg_huff ac_huff[4];
    struct jpeg_comp comp[JPEG_MAX_COMPS];

    /* Restart interval */
    uint16_t restart_interval;

    /* Bit buffer */
    uint32_t bits;
    int      bits_left;
    int      eof;
};

/* ------------------------------------------------------------------ */
/* Byte-level I/O                                                        */
/* ------------------------------------------------------------------ */

static int jpeg_read_byte(struct jpeg_ctx *ctx) {
    if (ctx->pos >= ctx->len) { ctx->eof = 1; return 0; }
    return ctx->data[ctx->pos++];
}

static uint16_t jpeg_read_u16(struct jpeg_ctx *ctx) {
    uint8_t hi = (uint8_t)jpeg_read_byte(ctx);
    uint8_t lo = (uint8_t)jpeg_read_byte(ctx);
    return (uint16_t)((hi << 8) | lo);
}

/* ------------------------------------------------------------------ */
/* Huffman table builder                                                 */
/* ------------------------------------------------------------------ */

static void jpeg_build_huff(struct jpeg_huff *h) {
    int code = 0;
    int val_idx = 0;
    for (int l = 1; l <= 16; l++) {
        if (h->bits[l] == 0) {
            h->maxcode[l] = -1;
            h->delta[l]   = 0;
        } else {
            h->delta[l]   = val_idx - code;
            h->maxcode[l] = code + h->bits[l] - 1;
            val_idx += h->bits[l];
            code    += h->bits[l];
        }
        code <<= 1;
    }
    h->maxcode[17] = 0xFFFFFF;
    h->built = 1;
}

/* ------------------------------------------------------------------ */
/* Bit-level I/O for entropy decoding                                   */
/* ------------------------------------------------------------------ */

static void jpeg_refill_bits(struct jpeg_ctx *ctx) {
    while (ctx->bits_left <= 24) {
        if (ctx->pos >= ctx->len) { ctx->eof = 1; break; }
        uint8_t b = ctx->data[ctx->pos++];
        if (b == 0xFF) {
            /* Peek at next byte */
            if (ctx->pos >= ctx->len) { ctx->eof = 1; break; }
            uint8_t b2 = ctx->data[ctx->pos];
            if (b2 == 0x00) {
                /* Stuffed byte: consume the 0x00 */
                ctx->pos++;
            } else if (b2 >= JPEG_RST0 && b2 <= JPEG_RST7) {
                ctx->pos++; /* skip restart marker, reset nothing here */
                continue;
            } else {
                /* Other marker — back up and stop */
                ctx->pos--;
                ctx->eof = 1;
                break;
            }
        }
        ctx->bits = (ctx->bits << 8) | b;
        ctx->bits_left += 8;
    }
}

static int jpeg_get_bits(struct jpeg_ctx *ctx, int n) {
    if (n == 0) return 0;
    jpeg_refill_bits(ctx);
    if (ctx->bits_left < n) return 0;
    ctx->bits_left -= n;
    return (int)((ctx->bits >> ctx->bits_left) & ((1u << n) - 1u));
}

/* ------------------------------------------------------------------ */
/* Huffman symbol decode                                                 */
/* ------------------------------------------------------------------ */

static int jpeg_huff_decode(struct jpeg_ctx *ctx, struct jpeg_huff *h) {
    if (!h->built) return -1;
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | jpeg_get_bits(ctx, 1);
        if (code <= h->maxcode[l]) {
            return h->vals[code + h->delta[l]];
        }
    }
    return -1; /* invalid code */
}

/* ------------------------------------------------------------------ */
/* Receive + extend (sign-extend n-bit value)                           */
/* ------------------------------------------------------------------ */

static int jpeg_receive_extend(struct jpeg_ctx *ctx, int n) {
    if (n == 0) return 0;
    int v = jpeg_get_bits(ctx, n);
    if (v < (1 << (n - 1)))
        v -= (1 << n) - 1;
    return v;
}

/* ------------------------------------------------------------------ */
/* IDCT: 2D 8x8 integer IDCT                                            */
/* block[] is in natural 8x8 order (after zigzag reorder+dequantize)   */
/* Output is level-shifted: values in [0, 255]                          */
/* ------------------------------------------------------------------ */

static void jpeg_idct(int16_t *block, uint8_t *out, int stride) {
    int32_t tmp[64];

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int64_t sum = 0;
            for (int u = 0; u < 8; u++) {
                int64_t cu_cos = jpeg_cos[u][y];
                int64_t Cu = (u == 0) ? JPEG_C0 : JPEG_C1;
                for (int v = 0; v < 8; v++) {
                    int64_t Cv = (v == 0) ? JPEG_C0 : JPEG_C1;
                    int64_t coeff = block[u * 8 + v];
                    if (coeff == 0) continue;
                    sum += Cu * Cv * coeff * cu_cos * jpeg_cos[v][x];
                }
            }
            /* Divide by 4 * 128^4 = 2^30, level shift by 128 */
            int32_t val = (int32_t)((sum + (1LL << 29)) >> 30) + 128;
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            tmp[y * 8 + x] = val;
        }
    }

    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            out[y * stride + x] = (uint8_t)tmp[y * 8 + x];
}

/* ------------------------------------------------------------------ */
/* Decode one 8×8 block into a planar output buffer                     */
/* ------------------------------------------------------------------ */

static int jpeg_decode_block(struct jpeg_ctx *ctx, int comp_idx,
                             uint8_t *plane, int plane_w, int bx, int by) {
    struct jpeg_comp *c = &ctx->comp[comp_idx];
    struct jpeg_huff *dc_h = &ctx->dc_huff[c->dc_huff_idx];
    struct jpeg_huff *ac_h = &ctx->ac_huff[c->ac_huff_idx];
    int16_t *qt = ctx->qtable[c->qtable_idx];

    int16_t block[64];
    kmemzero(block, sizeof(block));

    /* DC coefficient */
    int dc_sym = jpeg_huff_decode(ctx, dc_h);
    if (dc_sym < 0 || dc_sym > 11) return -1;
    int dc_diff = jpeg_receive_extend(ctx, dc_sym);
    c->dc_pred += dc_diff;
    block[0] = (int16_t)(c->dc_pred * qt[0]);

    /* AC coefficients */
    int k = 1;
    while (k < 64) {
        int ac_sym = jpeg_huff_decode(ctx, ac_h);
        if (ac_sym < 0) return -1;
        if (ac_sym == 0x00) break; /* EOB */
        int run = (ac_sym >> 4) & 0x0F;
        int mag = ac_sym & 0x0F;
        if (mag == 0) {
            if (run == 15) { k += 16; continue; } /* ZRL */
            break;
        }
        k += run;
        if (k >= 64) break;
        int val = jpeg_receive_extend(ctx, mag);
        block[jpeg_zigzag[k]] = (int16_t)(val * qt[jpeg_zigzag[k]]);
        k++;
    }

    /* IDCT and store into plane */
    int px = bx * 8;
    int py = by * 8;
    if (px >= (int)ctx->width || py >= (int)ctx->height) return 0;

    uint8_t tmp[64];
    jpeg_idct(block, tmp, 8);

    for (int row = 0; row < 8; row++) {
        int ry = py + row;
        if (ry >= (int)ctx->height) break;
        for (int col = 0; col < 8; col++) {
            int cx = px + col;
            if (cx >= plane_w) break;
            plane[ry * plane_w + cx] = tmp[row * 8 + col];
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Marker parsers                                                        */
/* ------------------------------------------------------------------ */

static int jpeg_parse_dqt(struct jpeg_ctx *ctx, int seg_len) {
    int remaining = seg_len - 2;
    while (remaining >= 65) {
        int info = jpeg_read_byte(ctx);
        int prec = (info >> 4) & 0x0F;
        int tbl  = info & 0x0F;
        remaining--;
        if (tbl >= 4) return -1;
        if (prec == 0) {
            for (int i = 0; i < 64; i++)
                ctx->qtable[tbl][i] = (int16_t)jpeg_read_byte(ctx);
            remaining -= 64;
        } else {
            /* 16-bit quantization table */
            for (int i = 0; i < 64; i++) {
                uint16_t v = jpeg_read_u16(ctx);
                ctx->qtable[tbl][i] = (int16_t)(v > 32767 ? 32767 : v);
            }
            remaining -= 128;
        }
    }
    return 0;
}

static int jpeg_parse_dht(struct jpeg_ctx *ctx, int seg_len) {
    int remaining = seg_len - 2;
    while (remaining > 0) {
        if (remaining < 17) break;
        int info  = jpeg_read_byte(ctx);
        int cls   = (info >> 4) & 0x01; /* 0=DC, 1=AC */
        int tbl   = info & 0x0F;
        remaining--;
        if (tbl >= 4) return -1;

        struct jpeg_huff *h = cls ? &ctx->ac_huff[tbl] : &ctx->dc_huff[tbl];
        kmemzero(h, sizeof(*h));

        int total = 0;
        for (int i = 1; i <= 16; i++) {
            h->bits[i] = (uint8_t)jpeg_read_byte(ctx);
            total += h->bits[i];
            remaining--;
        }
        if (total > 256 || remaining < total) return -1;
        for (int i = 0; i < total; i++) {
            h->vals[i] = (uint8_t)jpeg_read_byte(ctx);
            remaining--;
        }
        jpeg_build_huff(h);
    }
    return 0;
}

static int jpeg_parse_sof0(struct jpeg_ctx *ctx, int seg_len) {
    (void)seg_len;
    ctx->precision = (uint8_t)jpeg_read_byte(ctx);
    ctx->height    = jpeg_read_u16(ctx);
    ctx->width     = jpeg_read_u16(ctx);
    ctx->ncomp     = (uint8_t)jpeg_read_byte(ctx);

    if (ctx->precision != 8) return -1;
    if (ctx->width == 0 || ctx->height == 0) return -1;
    if (ctx->width > JPEG_MAX_DIM || ctx->height > JPEG_MAX_DIM) return -1;
    if (ctx->ncomp != 1 && ctx->ncomp != 3) return -1;

    for (int i = 0; i < (int)ctx->ncomp; i++) {
        ctx->comp[i].id          = (uint8_t)jpeg_read_byte(ctx);
        uint8_t samp             = (uint8_t)jpeg_read_byte(ctx);
        ctx->comp[i].h_samp      = (samp >> 4) & 0x0F;
        ctx->comp[i].v_samp      = samp & 0x0F;
        ctx->comp[i].qtable_idx  = (uint8_t)jpeg_read_byte(ctx);
        ctx->comp[i].dc_pred     = 0;
    }
    return 0;
}

static int jpeg_parse_sos(struct jpeg_ctx *ctx,
                          uint8_t **planes, int *plane_ws) {
    int seg_len = jpeg_read_u16(ctx);
    (void)seg_len;

    int ns = jpeg_read_byte(ctx);
    if (ns != (int)ctx->ncomp) {
        /* Skip scan (progressive or partial) */
        ctx->pos = ctx->len;
        return 0;
    }

    for (int i = 0; i < ns; i++) {
        int id = jpeg_read_byte(ctx);
        int tbl = jpeg_read_byte(ctx);
        /* Find component by ID */
        for (int c = 0; c < (int)ctx->ncomp; c++) {
            if (ctx->comp[c].id == id) {
                ctx->comp[c].dc_huff_idx = (uint8_t)((tbl >> 4) & 0x0F);
                ctx->comp[c].ac_huff_idx = (uint8_t)(tbl & 0x0F);
                break;
            }
        }
    }
    /* Ss, Se, Ah/Al bytes */
    jpeg_read_byte(ctx);
    jpeg_read_byte(ctx);
    jpeg_read_byte(ctx);

    /* Reset DC predictors */
    for (int c = 0; c < (int)ctx->ncomp; c++)
        ctx->comp[c].dc_pred = 0;

    /* Reset bit buffer */
    ctx->bits      = 0;
    ctx->bits_left = 0;

    /* Determine MCU layout */
    int max_h = 0, max_v = 0;
    for (int c = 0; c < (int)ctx->ncomp; c++) {
        if (ctx->comp[c].h_samp > max_h) max_h = ctx->comp[c].h_samp;
        if (ctx->comp[c].v_samp > max_v) max_v = ctx->comp[c].v_samp;
    }
    if (max_h == 0 || max_v == 0) return -1;

    int mcu_w = max_h * 8;
    int mcu_h = max_v * 8;
    int mcus_x = ((int)ctx->width  + mcu_w - 1) / mcu_w;
    int mcus_y = ((int)ctx->height + mcu_h - 1) / mcu_h;

    int restart_count = 0;

    for (int my = 0; my < mcus_y; my++) {
        for (int mx = 0; mx < mcus_x; mx++) {
            if (ctx->restart_interval > 0 &&
                restart_count == (int)ctx->restart_interval) {
                /* Skip restart marker */
                ctx->bits = 0; ctx->bits_left = 0;
                /* Consume RST marker bytes */
                while (ctx->pos + 1 < ctx->len) {
                    if (ctx->data[ctx->pos] == 0xFF &&
                        ctx->data[ctx->pos+1] >= JPEG_RST0 &&
                        ctx->data[ctx->pos+1] <= JPEG_RST7) {
                        ctx->pos += 2;
                        break;
                    }
                    ctx->pos++;
                }
                for (int c = 0; c < (int)ctx->ncomp; c++)
                    ctx->comp[c].dc_pred = 0;
                restart_count = 0;
            }

            /* Decode all data units in this MCU */
            for (int ci = 0; ci < (int)ctx->ncomp; ci++) {
                struct jpeg_comp *comp = &ctx->comp[ci];
                int hs = comp->h_samp;
                int vs = comp->v_samp;
                int pw = plane_ws[ci];

                for (int dv = 0; dv < vs; dv++) {
                    for (int dh = 0; dh < hs; dh++) {
                        int bx = mx * hs + dh;
                        int by = my * vs + dv;
                        if (jpeg_decode_block(ctx, ci, planes[ci],
                                              pw, bx, by) != 0) {
                            return -1;
                        }
                    }
                }
            }
            restart_count++;

            if (ctx->eof) return 0;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* YCbCr → RGB conversion (integer)                                    */
/* R = Y + 1.402 * Cr                                                  */
/* G = Y - 0.344136 * Cb - 0.714136 * Cr                              */
/* B = Y + 1.772 * Cb                                                  */
/* ------------------------------------------------------------------ */

static uint8_t jpeg_clamp(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static uint32_t jpeg_ycbcr_to_argb(uint8_t Y, uint8_t Cb, uint8_t Cr) {
    int y  = (int)Y;
    int cb = (int)Cb - 128;
    int cr = (int)Cr - 128;

    uint8_t r = jpeg_clamp(y + ((1436 * cr) >> 10));
    uint8_t g = jpeg_clamp(y - ((352 * cb + 731 * cr) >> 10));
    uint8_t b = jpeg_clamp(y + ((1815 * cb) >> 10));
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* ------------------------------------------------------------------ */
/* Public API                                                            */
/* ------------------------------------------------------------------ */

int jpeg_decode(const uint8_t *data, size_t len, struct jpeg_image *out) {
    struct jpeg_ctx ctx;
    uint8_t  *planes[JPEG_MAX_COMPS] = { NULL, NULL, NULL };
    int       plane_ws[JPEG_MAX_COMPS] = { 0, 0, 0 };
    uint32_t *pixels = NULL;
    int       ok = 0;

    if (!data || !out) return -1;
    kmemzero(out, sizeof(*out));
    kmemzero(&ctx, sizeof(ctx));
    ctx.data = data;
    ctx.len  = len;

    /* Check SOI marker */
    if (len < 2) return -1;
    if (data[0] != 0xFF || data[1] != JPEG_SOI) return -1;
    ctx.pos = 2;

    int got_sof = 0;
    int got_sos = 0;

    while (ctx.pos + 1 < ctx.len && !ctx.eof) {
        if (ctx.data[ctx.pos] != 0xFF) { ctx.pos++; continue; }
        ctx.pos++;
        uint8_t marker = ctx.data[ctx.pos++];

        if (marker == 0x00 || (marker >= JPEG_RST0 && marker <= JPEG_RST7))
            continue;
        if (marker == JPEG_EOI) break;
        if (marker == JPEG_SOI) continue;

        if (marker == JPEG_SOF0) {
            int seg_len = (int)jpeg_read_u16(&ctx);
            if (jpeg_parse_sof0(&ctx, seg_len) != 0) goto done;
            got_sof = 1;
            /* Allocate component planes */
            int max_h = 0, max_v = 0;
            for (int c = 0; c < (int)ctx.ncomp; c++) {
                if (ctx.comp[c].h_samp > max_h) max_h = ctx.comp[c].h_samp;
                if (ctx.comp[c].v_samp > max_v) max_v = ctx.comp[c].v_samp;
            }
            for (int c = 0; c < (int)ctx.ncomp; c++) {
                int mcu_w = ctx.comp[c].h_samp * 8;
                int mcu_h_blocks = ctx.comp[c].v_samp * 8;
                int pw = (((int)ctx.width * ctx.comp[c].h_samp / max_h) + mcu_w - 1)
                         / mcu_w * mcu_w;
                int ph = (((int)ctx.height * ctx.comp[c].v_samp / max_v) + mcu_h_blocks - 1)
                         / mcu_h_blocks * mcu_h_blocks;
                plane_ws[c] = pw;
                planes[c] = (uint8_t *)kalloc((size_t)(pw * ph));
                if (!planes[c]) goto done;
                kmemzero(planes[c], (size_t)(pw * ph));
            }
        } else if (marker == JPEG_DQT) {
            int seg_len = (int)jpeg_read_u16(&ctx);
            if (jpeg_parse_dqt(&ctx, seg_len) != 0) goto done;
        } else if (marker == JPEG_DHT) {
            int seg_len = (int)jpeg_read_u16(&ctx);
            if (jpeg_parse_dht(&ctx, seg_len) != 0) goto done;
        } else if (marker == JPEG_DRI) {
            jpeg_read_u16(&ctx); /* segment length = 4, skip */
            ctx.restart_interval = jpeg_read_u16(&ctx);
        } else if (marker == JPEG_SOS) {
            if (!got_sof) goto done;
            if (jpeg_parse_sos(&ctx, planes, plane_ws) != 0) goto done;
            got_sos = 1;
            break; /* Baseline JPEG has one scan */
        } else if ((marker >= JPEG_APP0 && marker <= JPEG_APP15) ||
                   marker == JPEG_COM || marker == JPEG_SOF2 ||
                   (marker >= 0xC0 && marker <= 0xFE)) {
            /* Skip unknown segments */
            int seg_len = (int)jpeg_read_u16(&ctx);
            if (seg_len < 2) goto done;
            ctx.pos += (size_t)(seg_len - 2);
        }
    }

    if (!got_sof || !got_sos || !planes[0]) goto done;

    /* Assemble output pixels */
    pixels = (uint32_t *)kalloc(ctx.width * ctx.height * 4);
    if (!pixels) goto done;

    if (ctx.ncomp == 1) {
        /* Grayscale */
        for (uint32_t py = 0; py < ctx.height; py++) {
            for (uint32_t px = 0; px < ctx.width; px++) {
                uint8_t y = planes[0][py * (uint32_t)plane_ws[0] + px];
                pixels[py * ctx.width + px] =
                    0xFF000000u | ((uint32_t)y << 16) | ((uint32_t)y << 8) | y;
            }
        }
    } else {
        /* YCbCr with potential chroma subsampling */
        int max_h = 0, max_v = 0;
        for (int c = 0; c < 3; c++) {
            if (ctx.comp[c].h_samp > max_h) max_h = ctx.comp[c].h_samp;
            if (ctx.comp[c].v_samp > max_v) max_v = ctx.comp[c].v_samp;
        }
        for (uint32_t py = 0; py < ctx.height; py++) {
            for (uint32_t px = 0; px < ctx.width; px++) {
                uint8_t Y  = planes[0][py * (uint32_t)plane_ws[0] + px];

                /* Map luma pixel to chroma plane coordinates */
                uint32_t cbcr_x1 = px * (uint32_t)ctx.comp[1].h_samp / (uint32_t)max_h;
                uint32_t cbcr_y1 = py * (uint32_t)ctx.comp[1].v_samp / (uint32_t)max_v;
                uint32_t cbcr_x2 = px * (uint32_t)ctx.comp[2].h_samp / (uint32_t)max_h;
                uint32_t cbcr_y2 = py * (uint32_t)ctx.comp[2].v_samp / (uint32_t)max_v;

                uint8_t Cb = planes[1][cbcr_y1 * (uint32_t)plane_ws[1] + cbcr_x1];
                uint8_t Cr = planes[2][cbcr_y2 * (uint32_t)plane_ws[2] + cbcr_x2];

                pixels[py * ctx.width + px] = jpeg_ycbcr_to_argb(Y, Cb, Cr);
            }
        }
    }

    out->width  = ctx.width;
    out->height = ctx.height;
    out->pixels = pixels;
    pixels = NULL;
    ok = 1;

done:
    for (int c = 0; c < JPEG_MAX_COMPS; c++)
        if (planes[c]) kfree(planes[c]);
    if (pixels) kfree(pixels);
    return ok ? 0 : -1;
}

void jpeg_free(struct jpeg_image *img) {
    if (!img) return;
    if (img->pixels) kfree(img->pixels);
    img->pixels = NULL;
    img->width  = 0;
    img->height = 0;
}
