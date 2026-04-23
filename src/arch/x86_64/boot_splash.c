/* boot_splash.c — Splash screen, ASCII banner, ACPI RSDP validation.
 *
 * Split from kernel_main.c to keep each TU ≤ 500 lines.
 */
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_main_internal.h"
#include "arch/x86_64/kernel_platform_runtime.h"
#include "branding/capyos_icon_mask.h"
#include "core/version.h"

/* ── theme splash colour globals (owned here) ────────────────────────── */

uint32_t g_theme_splash_bg        = 0x000A1713;
uint32_t g_theme_splash_icon      = 0x0000A651;
uint32_t g_theme_splash_bar_border = 0x00213A31;
uint32_t g_theme_splash_bar_bg    = 0x0012221C;
uint32_t g_theme_splash_bar_fill  = 0x0000C364;

/* ── ACPI RSDP validation ────────────────────────────────────────────── */

struct acpi_rsdp {
  char signature[8]; /* "RSD PTR " */
  uint8_t checksum;
  char oemid[6];
  uint8_t revision;
  uint32_t rsdt;
  uint32_t length;
  uint64_t xsdt;
  uint8_t ext_checksum;
  uint8_t reserved[3];
} __attribute__((packed));

static int range_ok(uint64_t addr, uint64_t size) {
  if (addr == 0 || size == 0)
    return 0;
  if (addr + size < addr)
    return 0;
  return 1;
}

static uint8_t sum8(const uint8_t *p, uint32_t len) {
  uint8_t s = 0;
  for (uint32_t i = 0; i < len; i++)
    s = (uint8_t)(s + p[i]);
  return s;
}

int rsdp_is_valid(uint64_t rsdp_addr) {
  if (!range_ok(rsdp_addr, sizeof(struct acpi_rsdp)))
    return 0;
  const struct acpi_rsdp *r = (const struct acpi_rsdp *)(uintptr_t)rsdp_addr;
  const char sig[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};
  for (int i = 0; i < 8; i++) {
    if (r->signature[i] != sig[i])
      return 0;
  }
  if (!range_ok(rsdp_addr, 20))
    return 0;
  if (sum8((const uint8_t *)r, 20) != 0)
    return 0;
  if (r->revision >= 2) {
    uint32_t len = r->length;
    if (len < 36 || len > 4096) {
      return 1;
    }
    if (!range_ok(rsdp_addr, len))
      return 0;
    return sum8((const uint8_t *)r, len) == 0;
  }
  return 1;
}

/* ── splash helpers ──────────────────────────────────────────────────── */

void ui_draw_bars(void) {
  (void)0;
}

static int capyos_icon_mask_get(uint32_t x, uint32_t y) {
  if (x >= CAPYOS_ICON_W || y >= CAPYOS_ICON_H) {
    return 0;
  }
  uint8_t byte = capyos_icon_mask[y * CAPYOS_ICON_STRIDE + (x / 8u)];
  return (byte & (uint8_t)(1u << (7u - (x & 7u)))) != 0;
}

static void splash_spin_delay(uint32_t loops) {
  for (volatile uint32_t i = 0; i < loops; ++i) {
    cpu_relax();
  }
}

void ui_draw_capyos_icon(uint32_t x0, uint32_t y0, uint32_t scale,
                         uint32_t color) {
  if (scale == 0) {
    return;
  }
  for (uint32_t y = 0; y < CAPYOS_ICON_H; ++y) {
    for (uint32_t x = 0; x < CAPYOS_ICON_W; ++x) {
      if (capyos_icon_mask_get(x, y)) {
        fbcon_fill_rect_px(x0 + (x * scale), y0 + (y * scale), scale, scale,
                           color);
      }
    }
  }
}

void ui_boot_splash(void) {
  if (!g_con.fb || g_con.width < 160 || g_con.height < 120) {
    return;
  }

  uint32_t scale = (g_con.height / 4u) / CAPYOS_ICON_H;
  if (scale == 0) {
    scale = 1;
  }
  if (scale > 4) {
    scale = 4;
  }
  uint32_t icon_w = CAPYOS_ICON_W * scale;
  uint32_t icon_h = CAPYOS_ICON_H * scale;

  uint32_t bar_w = g_con.width / 3u;
  if (bar_w < 140) {
    bar_w = 140;
  } else if (bar_w > 420) {
    bar_w = 420;
  }
  uint32_t bar_h = g_con.height / 96u;
  if (bar_h < 8) {
    bar_h = 8;
  } else if (bar_h > 14) {
    bar_h = 14;
  }

  uint32_t total_h = icon_h + (scale * 10u) + bar_h;
  uint32_t icon_x = (g_con.width > icon_w) ? (g_con.width - icon_w) / 2u : 0;
  uint32_t icon_y =
      (g_con.height > total_h) ? (g_con.height - total_h) / 2u : 0;
  uint32_t bar_x = (g_con.width > bar_w) ? (g_con.width - bar_w) / 2u : 0;
  uint32_t bar_y = icon_y + icon_h + (scale * 10u);

  fbcon_fill_rect_px(0, 0, g_con.width, g_con.height, g_theme_splash_bg);
  ui_draw_capyos_icon(icon_x, icon_y, scale, g_theme_splash_icon);
  fbcon_fill_rect_px(bar_x, bar_y, bar_w, bar_h, g_theme_splash_bar_border);

  uint32_t inner_x = bar_x;
  uint32_t inner_y = bar_y;
  uint32_t inner_w = bar_w;
  uint32_t inner_h = bar_h;
  if (bar_w > 4u) {
    inner_x += 2u;
    inner_w -= 4u;
  }
  if (bar_h > 4u) {
    inner_y += 2u;
    inner_h -= 4u;
  }

  for (uint32_t step = 0; step <= 14; ++step) {
    fbcon_fill_rect_px(inner_x, inner_y, inner_w, inner_h,
                       g_theme_splash_bar_bg);
    uint32_t fill_w = (inner_w * step) / 14u;
    if (fill_w > 0u) {
      fbcon_fill_rect_px(inner_x, inner_y, fill_w, inner_h,
                         g_theme_splash_bar_fill);
    }
    splash_spin_delay(400000u);
  }

  splash_spin_delay(600000u);
}

/* ── ASCII banner ────────────────────────────────────────────────────── */

static uint32_t ui_banner_strlen(const char *s) {
  uint32_t len = 0;
  if (!s) {
    return 0;
  }
  while (s[len]) {
    ++len;
  }
  return len;
}

static void ui_banner_append(char *dst, uint32_t cap, const char *src) {
  uint32_t len = ui_banner_strlen(dst);
  uint32_t pos = 0;

  if (!dst || cap == 0 || !src || len >= cap - 1u) {
    return;
  }
  while (src[pos] && len < cap - 1u) {
    dst[len++] = src[pos++];
  }
  dst[len] = '\0';
}

static void ui_banner_rule(uint32_t inner_cols) {
  fbcon_putc('+');
  for (uint32_t i = 0; i < inner_cols + 2u; ++i) {
    fbcon_putc('-');
  }
  fbcon_print("+\n");
}

static void ui_banner_line(uint32_t inner_cols, const char *text) {
  uint32_t len = ui_banner_strlen(text);
  fbcon_print("| ");
  for (uint32_t i = 0; i < inner_cols; ++i) {
    fbcon_putc(i < len ? text[i] : ' ');
  }
  fbcon_print(" |\n");
}

void ui_banner(void) {
  char version_line[64];
  uint32_t inner_cols =
      (g_con.cols > 6u) ? (g_con.cols - 4u) : 0u;

  if (inner_cols > 58u) {
    inner_cols = 58u;
  }

  if (inner_cols < 38u) {
    fbcon_print("CAPYOS\n");
    fbcon_print(CAPYOS_VERSION_EXTENDED);
    fbcon_print("  x86_64\n");
    return;
  }

  version_line[0] = '\0';
  ui_banner_append(version_line, sizeof(version_line), " Version: ");
  ui_banner_append(version_line, sizeof(version_line), CAPYOS_VERSION_EXTENDED);
  ui_banner_append(version_line, sizeof(version_line), "   Arch: x86_64");

  ui_banner_rule(inner_cols);
  ui_banner_line(inner_cols, "  ###  ###  ###  # #  ###  ### ");
  ui_banner_line(inner_cols, " #     # #  # #  # #  # #  #    ");
  ui_banner_line(inner_cols, " #     ###  ###   #   # #   ##  ");
  ui_banner_line(inner_cols, " #     # #  #     #   # #     # ");
  ui_banner_line(inner_cols, "  ###  # #  #     #   ###  ###  ");
  ui_banner_line(inner_cols, "");
  ui_banner_line(inner_cols, "");
  ui_banner_line(inner_cols, "  CapyOS");
  ui_banner_line(inner_cols, "  Shell");

  ui_banner_line(inner_cols, "");
  ui_banner_line(inner_cols, version_line);
  ui_banner_rule(inner_cols);
}

/* ── cmd_info (routes through kernel_platform_runtime) ───────────────── */

void cmd_info(void) {
  struct x64_platform_diag_io io;
  kernel_platform_diag_io_init(&io);
  x64_kernel_print_cmd_info(g_h, rsdp_is_valid(g_h ? g_h->rsdp : 0),
                            &g_input_runtime, &io);
}
