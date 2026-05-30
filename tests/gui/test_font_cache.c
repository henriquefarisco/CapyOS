#include "gui/font.h"

#include <stdio.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[font-cache] FAIL: %s\n", msg);
  g_failures++;
}

static void clear_pixels(uint32_t *pixels, uint32_t count) {
  for (uint32_t i = 0u; i < count; i++) pixels[i] = 0u;
}

static int pixel_count(uint32_t *pixels, uint32_t count, uint32_t color) {
  uint32_t hits = 0u;
  for (uint32_t i = 0u; i < count; i++) {
    if (pixels[i] == color) hits++;
  }
  return (int)hits;
}

static void test_font_init_populates_cache(void) {
  struct font_cache_stats stats;
  font_init();
  font_cache_stats_get(&stats);
  if (stats.glyphs_cached != FONT_GLYPH_COUNT ||
      stats.rows_cached != FONT_GLYPH_COUNT * FONT_GLYPH_HEIGHT ||
      stats.cache_hits != 0u ||
      stats.cache_misses != 0u) {
    fail("font_init must populate cache and reset counters");
  }
}

static void test_default_font_reports_cached_glyphs(void) {
  const struct font *f;
  font_init();
  f = font_default();
  if (!font_glyph_cached(f, 'A')) {
    fail("default glyph A must be cached");
  }
  if (font_glyph_cached(f, '\n')) {
    fail("out-of-range glyph must not report cached");
  }
}

static void test_draw_char_uses_cache_and_draws_pixels(void) {
  uint32_t pixels[16u * 20u];
  struct gui_surface surface = { pixels, 16u, 20u, 16u * 4u };
  struct font_cache_stats stats;
  clear_pixels(pixels, 16u * 20u);
  font_init();
  font_draw_char(&surface, font_default(), 2, 2, 'A', 0x00ABCDEFu);
  font_cache_stats_get(&stats);
  if (stats.cache_hits != 1u || stats.cache_misses != 0u) {
    fail("drawing default glyph must count one cache hit");
  }
  if (pixel_count(pixels, 16u * 20u, 0x00ABCDEFu) <= 0) {
    fail("drawing default glyph must write pixels");
  }
}

static void test_custom_font_falls_back_without_cache(void) {
  uint8_t custom_rows[1] = { 0x80u };
  uint32_t pixels[8u];
  struct gui_surface surface = { pixels, 8u, 1u, 8u * 4u };
  struct font f = { custom_rows, 8u, 1u, (uint32_t)'X', (uint32_t)'X', 1u };
  struct font_cache_stats stats;
  clear_pixels(pixels, 8u);
  font_init();
  font_draw_char(&surface, &f, 0, 0, 'X', 0x00123456u);
  font_cache_stats_get(&stats);
  if (stats.cache_hits != 0u || stats.cache_misses != 1u) {
    fail("custom font must count one cache miss");
  }
  if (pixels[0] != 0x00123456u) {
    fail("custom font fallback must draw its glyph");
  }
}

static void test_font_cache_stats_null_safe(void) {
  font_cache_stats_get(NULL);
}

int test_font_cache_run(void) {
  g_failures = 0;
  test_font_init_populates_cache();
  test_default_font_reports_cached_glyphs();
  test_draw_char_uses_cache_and_draws_pixels();
  test_custom_font_falls_back_without_cache();
  test_font_cache_stats_null_safe();
  if (g_failures == 0) printf("[tests] font_cache OK\n");
  return g_failures;
}
