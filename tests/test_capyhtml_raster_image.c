/* Host coverage for CMD_IMAGE pixel blitting in libcapyhtml raster. */
#include "capyhtml/raster.h"
#include "capyhtml/render.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

#define RX_OK(cond, msg) do {                                             \
    if (cond) { g_passed++; }                                             \
    else { g_failed++; printf("  FAIL %s\n", msg); }                      \
} while (0)

#define FB_W 32
#define FB_H 16
#define FB_STRIDE (FB_W * 4)
static uint8_t g_fb[FB_STRIDE * FB_H];

static void fb_reset(void) { memset(g_fb, 0xAA, sizeof(g_fb)); }

static struct capyhtml_raster_target target(void) {
    struct capyhtml_raster_target t;
    t.pixels = g_fb;
    t.width_px = FB_W;
    t.height_px = FB_H;
    t.stride_b = FB_STRIDE;
    return t;
}

static struct capyhtml_palette default_palette(void) {
    struct capyhtml_palette p;
    p.color_argb[0] = 0xFFFFFFFFu;
    p.color_argb[1] = 0xFFFF0000u;
    p.color_argb[2] = 0xFF0000FFu;
    p.color_argb[3] = 0xFF808080u;
    p.color_argb[4] = 0xFFFFA500u;
    p.background_argb = 0xFF000000u;
    return p;
}

static uint32_t pixel_argb(int x, int y) {
    uint8_t *p = &g_fb[y * FB_STRIDE + x * 4];
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[1] << 8)  | (uint32_t)p[0];
}

static void test_image_cmd_blits_pixels_when_provided(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    static const uint8_t src[4 * 2 * 4] = {
        0x00, 0x00, 0xFF, 0xFF,  0x00, 0xFF, 0x00, 0xFF,
        0xFF, 0x00, 0x00, 0xFF,  0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0x00, 0xFF,  0xFF, 0x00, 0xFF, 0xFF,
        0x00, 0xFF, 0xFF, 0xFF,  0x80, 0x80, 0x80, 0xFF,
    };

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_IMAGE;
    cmd.x = 5; cmd.y = 3;
    cmd.w = 4; cmd.h = 2;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.image_pixels = src;
    cmd.image_w = 4;
    cmd.image_h = 2;
    capyhtml_raster_draw(&t, &cmd, &p);

    RX_OK(pixel_argb(5, 3) == 0xFFFF0000u, "blit: red pixel preserved");
    RX_OK(pixel_argb(6, 3) == 0xFF00FF00u, "blit: green pixel preserved");
    RX_OK(pixel_argb(8, 3) == 0xFFFFFFFFu, "blit: white pixel preserved");
    RX_OK(pixel_argb(5, 4) == 0xFF00FFFFu, "blit: cyan row1 col0");
    RX_OK(pixel_argb(8, 4) == 0xFF808080u, "blit: gray row1 col3");
    RX_OK(pixel_argb(4, 3) == 0xFF000000u, "blit: outside left untouched");
    RX_OK(pixel_argb(9, 3) == 0xFF000000u, "blit: outside right untouched");
    RX_OK(pixel_argb(5, 5) == 0xFF000000u, "blit: outside below untouched");
    RX_OK(pixel_argb(6, 3) != 0xFF808080u,
          "blit: no MUTED fill (placeholder bypassed)");
}

static void test_image_cmd_blits_clipped_to_cmd_dims(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    static uint8_t src[8 * 4 * 4];
    for (int i = 0; i < 8 * 4; ++i) {
        src[i * 4 + 0] = 0x00;
        src[i * 4 + 1] = 0x00;
        src[i * 4 + 2] = 0xFF;
        src[i * 4 + 3] = 0xFF;
    }

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_IMAGE;
    cmd.x = 1; cmd.y = 1;
    cmd.w = 4; cmd.h = 2;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.image_pixels = src;
    cmd.image_w = 8;
    cmd.image_h = 4;
    capyhtml_raster_draw(&t, &cmd, &p);

    RX_OK(pixel_argb(1, 1) == 0xFFFF0000u,
          "blit clipped: TL inside is red");
    RX_OK(pixel_argb(4, 2) == 0xFFFF0000u,
          "blit clipped: BR inside is red");
    RX_OK(pixel_argb(5, 1) == 0xFF000000u,
          "blit clipped: column 5 not written");
    RX_OK(pixel_argb(1, 3) == 0xFF000000u,
          "blit clipped: row 3 not written");
}

static void test_image_cmd_falls_back_to_placeholder_when_no_pixels(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_IMAGE;
    cmd.x = 4; cmd.y = 2; cmd.w = 20; cmd.h = 10;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.image_pixels = (const uint8_t *)0;
    cmd.image_w = 240; cmd.image_h = 180;
    capyhtml_raster_draw(&t, &cmd, &p);

    RX_OK(pixel_argb(4 + 10, 2 + 5) == 0xFF808080u,
          "fallback: interior is MUTED placeholder");
    RX_OK(pixel_argb(4, 2) == 0xFF0000FFu,
          "fallback: TL border is LINK");
}

int test_capyhtml_raster_image_run(void) {
    printf("[test_capyhtml_raster_image]\n");
    g_passed = 0;
    g_failed = 0;
    test_image_cmd_blits_pixels_when_provided();
    test_image_cmd_blits_clipped_to_cmd_dims();
    test_image_cmd_falls_back_to_placeholder_when_no_pixels();
    printf("  -> %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed == 0 ? 0 : 1;
}
