#include "internal/vmbus_mouse_protocol.h"

static void mouse_report_zero(struct hyperv_mouse_report *out) {
  uint8_t *p = (uint8_t *)out;
  if (!p) return;
  for (uint32_t i = 0; i < (uint32_t)sizeof(*out); ++i) p[i] = 0;
}

int hyperv_mouse_parse_hid_report(const void *report, uint32_t report_len,
                                  struct hyperv_mouse_report *out) {
  const uint8_t *buf = (const uint8_t *)report;

  if (!buf || !out) return 0;
  mouse_report_zero(out);
  if (report_len >= 6u && buf[0] != 0u && buf[0] <= 0x0Fu) {
    out->absolute = 1u;
    out->buttons = buf[1] & 0x07u;
    out->abs_x = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
    out->abs_y = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
    out->dz = report_len >= 7u ? (int8_t)buf[6] : 0;
    return 1;
  }
  if (report_len >= 3u) {
    out->buttons = buf[0] & 0x07u;
    out->dx = (int16_t)(int8_t)buf[1];
    out->dy = (int16_t)(int8_t)buf[2];
    out->dz = report_len >= 4u ? (int8_t)buf[3] : 0;
    return 1;
  }
  return 0;
}
