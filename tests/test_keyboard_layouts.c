#include <stdio.h>
#include <stdint.h>

#include "drivers/input/keyboard_compose.h"
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
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x53, ',', ',');
    return fails;
}

static int test_br_abnt2_accents_and_symbols(void) {
    int fails = 0;
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x1A, '\'', '`');  // dead acute/grave
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x28, '~', '^');   // dead tilde/circumflex
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x07, '6', '"');   // diaeresis on shift
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x29, '\'', '"');  // literal quote key
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x35, ';', ':');
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x56, '\\', '|');
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x59, '/', '?');   // ABNT2 extra key variant
    fails += assert_key(&g_keyboard_layout_br_abnt2, 0x73, '/', '?');   // ABNT2 extra key (keycode 89)
    return fails;
}

static int test_dead_key_composition_runtime_style(void) {
    int fails = 0;
    char dead = 0;
    char pending = 0;
    char out = 0;

    if (keyboard_compose_step(&dead, &pending, '\'', 1, &out) != 0 || dead != '\'') {
        printf("[kbd] dead key agudo nao armou corretamente\n");
        fails++;
    }

    out = 0;
    if (!keyboard_compose_step(&dead, &pending, 'a', 0, &out) || out != (char)0xA0 || pending != 0) {
        printf("[kbd] composicao agudo+a falhou (out=%d pending=%d)\n", (int)out, (int)pending);
        fails++;
    }

    dead = 0;
    pending = 0;
    out = 0;
    keyboard_compose_step(&dead, &pending, '\'', 1, &out);
    out = 0;
    if (!keyboard_compose_step(&dead, &pending, ' ', 0, &out) || out != '\'' || pending != 0) {
        printf("[kbd] agudo+espaco deveria emitir apenas o acento\n");
        fails++;
    }

    dead = 0;
    pending = 0;
    out = 0;
    keyboard_compose_step(&dead, &pending, '\'', 1, &out);
    out = 0;
    if (!keyboard_compose_step(&dead, &pending, '\'', 1, &out) || out != '\'' || dead != 0 || pending != 0) {
        printf("[kbd] agudo repetido deveria emitir acento literal\n");
        fails++;
    }

    dead = 0;
    pending = 0;
    out = 0;
    keyboard_compose_step(&dead, &pending, '\'', 1, &out);
    out = 0;
    if (!keyboard_compose_step(&dead, &pending, '~', 1, &out) || out != '\'' || dead != '~' || pending != 0) {
        printf("[kbd] troca de dead key deveria emitir a anterior e armar a nova\n");
        fails++;
    }
    out = 0;
    if (!keyboard_compose_step(&dead, &pending, 'a', 0, &out) || out != (char)0xC6) {
        printf("[kbd] dead key trocada nao compôs para til+a\n");
        fails++;
    }

    dead = 0;
    pending = 0;
    out = 0;
    keyboard_compose_step(&dead, &pending, '\'', 1, &out);
    out = 0;
    if (!keyboard_compose_step(&dead, &pending, 'x', 0, &out) || out != '\'' || pending != 'x') {
        printf("[kbd] fallback de acento deveria emitir acento e guardar base\n");
        fails++;
    }

    return fails;
}

int run_keyboard_layout_tests(void) {
    int fails = 0;
    fails += test_br_abnt2_numpad_digits();
    fails += test_br_abnt2_accents_and_symbols();
    fails += test_dead_key_composition_runtime_style();
    if (fails == 0) {
        printf("[tests] keyboard layouts OK\n");
    }
    return fails;
}
