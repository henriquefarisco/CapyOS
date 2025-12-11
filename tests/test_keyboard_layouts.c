#include <stdio.h>
#include <stdint.h>

#include "drivers/input/keyboard_layout.h"

extern const struct keyboard_layout g_keyboard_layout_br_abnt2;

static int assert_key(const struct keyboard_layout *layout, int sc, char expected_base, char expected_shift) {
    if (!layout) return 1;
    char b = layout->base[sc];
    char s = layout->shift[sc];
    if (b != expected_base || s != expected_shift) {
        printf("[kbd] scancode 0x%02X esperado base=%d shift=%d obtido base=%d shift=%d\n",
               sc, (int)expected_base, (int)expected_shift, (int)b, (int)s);
        return 1;
    }
    return 0;
}

static int test_br_abnt2_numpad_digits(void) {
    int fails = 0;
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x47, '7', '7');
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x48, '8', '8');
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x49, '9', '9');
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x4B, '4', '4');
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x4C, '5', '5');
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x4D, '6', '6');
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x4F, '1', '1');
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x50, '2', '2');
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x51, '3', '3');
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x52, '0', '0');
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x53, '.', '.');
    return fails;
}

int run_keyboard_layout_tests(void) {
    int fails = 0;
    fails += test_br_abnt2_numpad_digits();
    if (fails == 0) {
        printf("[tests] keyboard layouts OK\n");
    }
    return fails;
}
