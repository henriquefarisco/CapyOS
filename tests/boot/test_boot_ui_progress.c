#include "boot/boot_ui.h"
#include <assert.h>
#include <stdio.h>

static unsigned calls, icons;
static uint64_t pixels;
static uint32_t last_x, last_w, last_h, last_color;
static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    assert(x + w <= 1280 && y + h <= 800);
    ++calls; pixels += (uint64_t)w * h;
    last_x = x; last_w = w; last_h = h; last_color = color;
}
static void text(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    (void)x; (void)y; (void)c; (void)fg; (void)bg;
}
static void icon(uint32_t x, uint32_t y, uint32_t scale, uint32_t color) {
    (void)x; (void)y; (void)scale; (void)color; ++icons;
}
int main(void) {
    struct boot_ui_io io = {.screen_w=1280, .screen_h=800,
        .splash_bar_bg=2, .splash_bar_fill=3,
        .fill_rect=fill, .putch_at=text, .draw_icon=icon};
    boot_ui_init(&io);
    boot_ui_splash_begin();
    assert(icons == 1);
    calls = 0; pixels = 0;
    boot_ui_splash_advance(0, 100);
    assert(calls == 0);
    for (unsigned p = 1; p <= 100; ++p) boot_ui_splash_advance(p, 100);
    assert(calls == 100 && pixels == 416 * 4);
    assert(last_color == 3 && last_h == 4 && last_w <= 5);
    unsigned before = calls;
    boot_ui_splash_advance(100, 100);
    boot_ui_splash_advance(110, 100);
    boot_ui_splash_advance(1, 0);
    assert(calls == before);
    boot_ui_splash_advance(50, 100);
    assert(last_color == 2 && last_w == 208 && last_x == 640);
    boot_ui_splash_end();
    before = calls;
    boot_ui_splash_advance(100, 100);
    assert(calls == before);
    boot_ui_splash_begin();
    before = calls;
    boot_ui_splash_advance(1, 100);
    assert(calls == before + 1 && last_w == 4);
    puts("[boot-ui] bounded delta writes, regression, restart and inactive passed");
}
