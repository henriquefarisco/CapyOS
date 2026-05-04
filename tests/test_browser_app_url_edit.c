/*
 * tests/test_browser_app_url_edit.c
 *
 * Cobertura host do editor da URL bar (browser_app).
 */

#include "apps/browser_app_url_edit.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } else { g_fail++; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static void test_set_and_clear(void) {
    struct url_edit e;
    url_edit_set(&e, "http://x");
    CHECK(e.len == 8u);
    CHECK(e.cursor == 8u);
    CHECK(strcmp(e.buf, "http://x") == 0);

    url_edit_clear(&e);
    CHECK(e.len == 0u);
    CHECK(e.cursor == 0u);
    CHECK(e.buf[0] == '\0');

    url_edit_set(&e, NULL);
    CHECK(e.len == 0u);
    CHECK(e.cursor == 0u);
}

static void test_set_truncates(void) {
    struct url_edit e;
    char src[BROWSER_APP_URL_EDIT_CAP + 32];
    for (size_t i = 0; i < sizeof(src) - 1; ++i) src[i] = 'a';
    src[sizeof(src) - 1] = '\0';

    url_edit_set(&e, src);
    CHECK(e.len == BROWSER_APP_URL_EDIT_CAP - 1u);
    CHECK(e.buf[BROWSER_APP_URL_EDIT_CAP - 1u] == '\0');
}

static void test_insert_at_end(void) {
    struct url_edit e;
    url_edit_clear(&e);
    CHECK(url_edit_insert_char(&e, 'a') == 1);
    CHECK(url_edit_insert_char(&e, 'b') == 1);
    CHECK(url_edit_insert_char(&e, 'c') == 1);
    CHECK(strcmp(e.buf, "abc") == 0);
    CHECK(e.cursor == 3u);
}

static void test_insert_in_middle(void) {
    struct url_edit e;
    url_edit_set(&e, "ac");
    e.cursor = 1u;
    CHECK(url_edit_insert_char(&e, 'b') == 1);
    CHECK(strcmp(e.buf, "abc") == 0);
    CHECK(e.cursor == 2u);
}

static void test_insert_rejects_non_printable(void) {
    struct url_edit e;
    url_edit_clear(&e);
    CHECK(url_edit_insert_char(&e, '\n') == 0);
    CHECK(url_edit_insert_char(&e, '\t') == 0);
    CHECK(url_edit_insert_char(&e, (char)0x1B) == 0);
    CHECK(url_edit_insert_char(&e, (char)0x7F) == 0);
    CHECK(e.len == 0u);
}

static void test_insert_full(void) {
    struct url_edit e;
    url_edit_clear(&e);
    for (size_t i = 0; i + 1u < BROWSER_APP_URL_EDIT_CAP; ++i) {
        CHECK(url_edit_insert_char(&e, 'x') == 1);
    }
    CHECK(e.len == BROWSER_APP_URL_EDIT_CAP - 1u);
    CHECK(url_edit_insert_char(&e, 'x') == 0);
}

static void test_backspace(void) {
    struct url_edit e;
    url_edit_set(&e, "abc");
    CHECK(url_edit_backspace(&e) == 1);
    CHECK(strcmp(e.buf, "ab") == 0);
    CHECK(e.cursor == 2u);

    e.cursor = 1u;
    CHECK(url_edit_backspace(&e) == 1);
    CHECK(strcmp(e.buf, "b") == 0);
    CHECK(e.cursor == 0u);

    /* No-op no inicio. */
    CHECK(url_edit_backspace(&e) == 0);
    CHECK(strcmp(e.buf, "b") == 0);
}

static void test_delete(void) {
    struct url_edit e;
    url_edit_set(&e, "abc");
    e.cursor = 0u;
    CHECK(url_edit_delete(&e) == 1);
    CHECK(strcmp(e.buf, "bc") == 0);
    CHECK(e.cursor == 0u);

    /* No-op no fim. */
    e.cursor = e.len;
    CHECK(url_edit_delete(&e) == 0);
}

static void test_cursor_movement(void) {
    struct url_edit e;
    url_edit_set(&e, "abc");
    /* cursor inicial no fim. */
    CHECK(e.cursor == 3u);
    CHECK(url_edit_move_right(&e) == 0);     /* ja no fim */
    CHECK(url_edit_move_left(&e) == 1);
    CHECK(e.cursor == 2u);
    CHECK(url_edit_move_home(&e) == 1);
    CHECK(e.cursor == 0u);
    CHECK(url_edit_move_home(&e) == 0);      /* idempotente */
    CHECK(url_edit_move_right(&e) == 1);
    CHECK(e.cursor == 1u);
    CHECK(url_edit_move_end(&e) == 1);
    CHECK(e.cursor == 3u);
    CHECK(url_edit_move_end(&e) == 0);       /* idempotente */
}

static void test_null_safe(void) {
    /* Nenhum dos helpers deve crashar com NULL. */
    url_edit_clear(NULL);
    url_edit_set(NULL, "foo");
    CHECK(url_edit_insert_char(NULL, 'x') == 0);
    CHECK(url_edit_backspace(NULL) == 0);
    CHECK(url_edit_delete(NULL) == 0);
    CHECK(url_edit_move_left(NULL) == 0);
    CHECK(url_edit_move_right(NULL) == 0);
    CHECK(url_edit_move_home(NULL) == 0);
    CHECK(url_edit_move_end(NULL) == 0);
}

int test_browser_app_url_edit_run(void) {
    g_pass = 0; g_fail = 0;

    test_set_and_clear();
    test_set_truncates();
    test_insert_at_end();
    test_insert_in_middle();
    test_insert_rejects_non_printable();
    test_insert_full();
    test_backspace();
    test_delete();
    test_cursor_movement();
    test_null_safe();

    printf("[test_browser_app_url_edit]\n  -> %d/%d passed\n",
           g_pass, g_pass + g_fail);
    if (g_fail != 0) {
        fprintf(stderr, "test_browser_app_url_edit FAIL: %d failures\n", g_fail);
    }
    return g_fail;
}
