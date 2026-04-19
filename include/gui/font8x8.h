#ifndef GUI_FONT8X8_H
#define GUI_FONT8X8_H

#include <stdint.h>

enum fbcon_accent_style {
  FBCON_ACCENT_NONE = 0,
  FBCON_ACCENT_ACUTE,
  FBCON_ACCENT_GRAVE,
  FBCON_ACCENT_CIRCUMFLEX,
  FBCON_ACCENT_TILDE,
  FBCON_ACCENT_DIAERESIS,
  FBCON_ACCENT_CEDILLA,
};

extern const uint8_t font8x8_basic[128][8];

void fbcon_copy_glyph(uint8_t dst[8], const uint8_t *src);
void fbcon_apply_accent(uint8_t glyph[8], enum fbcon_accent_style accent);
int fbcon_lookup_extended_glyph(uint8_t uc, uint8_t *base,
                                enum fbcon_accent_style *accent);
const uint8_t *font_glyph(uint8_t uc);

#endif /* GUI_FONT8X8_H */
