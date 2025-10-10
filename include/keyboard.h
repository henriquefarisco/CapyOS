#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stddef.h>

void keyboard_init(void);

size_t keyboard_layout_count(void);
const char *keyboard_layout_name(size_t index);
const char *keyboard_layout_description(size_t index);
const char *keyboard_current_layout(void);
int keyboard_set_layout_by_name(const char *name);
#endif
