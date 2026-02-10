/* Keyboard shim for 64-bit kernel.
 * Provides minimal keyboard functions for shell compatibility.
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stddef.h>

typedef void (*keyboard_hotkey_callback)(void);

#ifdef __x86_64__
/* For 64-bit, keyboard functions are stubs */
static inline void keyboard_init(void) {}
static inline void keyboard_set_help_callback(keyboard_hotkey_callback cb) {
  (void)cb;
}
static inline size_t keyboard_layout_count(void) { return 1; }
static inline const char *keyboard_layout_name(size_t idx) {
  (void)idx;
  return "us";
}
static inline const char *keyboard_layout_description(size_t idx) {
  (void)idx;
  return "US English";
}
static inline const char *keyboard_current_layout(void) { return "us"; }
static inline int keyboard_set_layout_by_name(const char *name) {
  (void)name;
  return 0;
}
static inline void keyboard_wait_any(void) {} /* Stub - non-blocking */

#else
/* 32-bit uses full keyboard driver */
void keyboard_init(void);
void keyboard_set_help_callback(keyboard_hotkey_callback cb);
size_t keyboard_layout_count(void);
const char *keyboard_layout_name(size_t index);
const char *keyboard_layout_description(size_t index);
const char *keyboard_current_layout(void);
int keyboard_set_layout_by_name(const char *name);
void keyboard_wait_any(void);
#endif

#endif /* KEYBOARD_H */
