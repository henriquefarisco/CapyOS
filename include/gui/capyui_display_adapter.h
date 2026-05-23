#ifndef GUI_CAPYUI_DISPLAY_ADAPTER_H
#define GUI_CAPYUI_DISPLAY_ADAPTER_H

#include <stdint.h>
#include "gui/compositor.h"

#define CAPYUI_DISPLAY_ADAPTER_CLIP_STACK_MAX 16u
#define CAPYUI_DISPLAY_ADAPTER_DIRTY_RECT_MAX 16u

enum capyui_display_adapter_status {
  CAPYUI_DISPLAY_ADAPTER_OK = 0,
  CAPYUI_DISPLAY_ADAPTER_ERR_UNAVAILABLE = -1,
  CAPYUI_DISPLAY_ADAPTER_ERR_INVALID = -2,
  CAPYUI_DISPLAY_ADAPTER_ERR_UNSUPPORTED_SCHEMA = -3,
  CAPYUI_DISPLAY_ADAPTER_ERR_CLIP_STACK = -4,
  CAPYUI_DISPLAY_ADAPTER_ERR_TEXT_RANGE = -5,
  CAPYUI_DISPLAY_ADAPTER_ERR_DIRTY_OVERFLOW = -6,
  CAPYUI_DISPLAY_ADAPTER_ERR_EMIT = -7
};

struct capy_display_list;
typedef int (*capyui_display_adapter_emit_fn)(void *producer,
                                              struct capy_display_list *out);

struct capyui_display_adapter_stats {
  uint32_t commands_seen;
  uint32_t commands_rendered;
  uint32_t commands_skipped;
  uint32_t unsupported_ops;
  uint32_t clip_depth_max;
};

int capyui_display_adapter_available(void);
uint32_t capyui_display_adapter_schema_version(void);
int capyui_display_adapter_render(const struct capy_display_list *dl,
                                  struct gui_surface *surface,
                                  const struct gui_rect *damage_clip,
                                  struct capyui_display_adapter_stats *stats);
int capyui_display_adapter_diff_damage(const struct capy_display_list *prev,
                                       const struct capy_display_list *next,
                                       struct gui_rect *out_dirty,
                                       uint32_t out_cap);
int capyui_display_adapter_render_window(struct gui_window *window,
                                         const struct capy_display_list *prev,
                                         const struct capy_display_list *next,
                                         struct capyui_display_adapter_stats *stats);
int capyui_display_adapter_render_producer_window(
    struct gui_window *window,
    const struct capy_display_list *prev,
    struct capy_display_list *next,
    capyui_display_adapter_emit_fn emit,
    void *producer,
    struct capyui_display_adapter_stats *stats);

#endif
