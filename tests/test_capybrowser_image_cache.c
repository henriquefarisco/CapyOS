/* tests/test_capybrowser_image_cache.c -- Etapa 3 secao a fetch+decode.
 *
 * Cobre o cache de imagens do engine ring 3
 * (`userland/bin/capybrowser/image_cache.c`):
 *
 *   - init zera todos os campos (slots livres, contadores 0).
 *   - alloc atribui img_id monotonico (>0); falha quando cheio.
 *   - lookup retorna idx em hit, -1 em miss; conta hits/misses.
 *   - record_response com IMAGE_OK + BGRA32 preenche pixels.
 *   - record_response com status erro deixa slot em ERROR sem
 *     pixels (mas slot continua reservado).
 *   - record_response com pixel_bytes > MAX_PIXEL_BYTES degrada
 *     para ERROR + total_oversized_drops++.
 *   - record_response com pixel_bytes != w*h*4 -> ERROR.
 *   - invalidate_other_navs limpa slots de navs antigas e conta
 *     evictions; preserva slots da nav ativa.
 *   - find_url tolera NULL e zero-length.
 */

#include "image_cache.h" /* fica em userland/bin/capybrowser/ */
#include "apps/browser_ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

#define IC_OK(cond, msg) do {                                             \
    if (cond) { g_passed++; }                                             \
    else { g_failed++; printf("  FAIL %s\n", msg); }                      \
} while (0)

/* Cache global em .bss para nao estourar a stack do testrunner.
 * Cada teste chama image_cache_init() para zerar. */
static struct image_cache g_ic;

static void test_init_zero_state(void) {
    memset(&g_ic, 0xCC, sizeof(g_ic));
    image_cache_init(&g_ic);
    int all_free = 1;
    for (uint32_t i = 0; i < IMAGE_CACHE_MAX_ENTRIES; ++i) {
        if (g_ic.entries[i].status != IMAGE_CACHE_FREE) all_free = 0;
        if (g_ic.entries[i].img_id != 0u) all_free = 0;
    }
    IC_OK(all_free, "init: todos os slots FREE com img_id=0");
    IC_OK(g_ic.next_img_id == 1u, "init: next_img_id=1 (skip 0)");
    IC_OK(g_ic.total_lookups == 0u, "init: total_lookups=0");
    IC_OK(g_ic.total_hits == 0u, "init: total_hits=0");
    IC_OK(g_ic.total_misses == 0u, "init: total_misses=0");
    IC_OK(g_ic.total_evictions == 0u, "init: total_evictions=0");
}

static void test_alloc_assigns_monotonic_ids(void) {
    image_cache_init(&g_ic);
    uint32_t id1 = 0, id2 = 0, id3 = 0;
    int s1 = image_cache_alloc(&g_ic, 1u, "a", 1u, &id1);
    int s2 = image_cache_alloc(&g_ic, 1u, "b", 1u, &id2);
    int s3 = image_cache_alloc(&g_ic, 1u, "c", 1u, &id3);
    IC_OK(s1 >= 0 && s2 >= 0 && s3 >= 0, "alloc: 3 slots ok");
    IC_OK(id1 > 0 && id2 > id1 && id3 > id2,
         "alloc: img_ids monotonicamente crescentes e >0");
    IC_OK(g_ic.entries[s1].status == IMAGE_CACHE_PENDING,
         "alloc: status=PENDING apos alloc");
    IC_OK(g_ic.entries[s1].nav_id == 1u, "alloc: nav_id armazenado");
    IC_OK(g_ic.entries[s1].url_len == 1u && g_ic.entries[s1].url[0] == 'a',
         "alloc: url copiado");
}

static void test_alloc_fills_then_fails(void) {
    image_cache_init(&g_ic);
    /* IMAGE_CACHE_MAX_ENTRIES = 4 hoje. */
    for (uint32_t i = 0; i < IMAGE_CACHE_MAX_ENTRIES; ++i) {
        char url[2] = { (char)('a' + (int)i), '\0' };
        uint32_t id = 0;
        int s = image_cache_alloc(&g_ic, 1u, url, 1u, &id);
        IC_OK(s >= 0 && id > 0, "alloc: ate o limite ok");
    }
    uint32_t extra = 0;
    int s = image_cache_alloc(&g_ic, 1u, "z", 1u, &extra);
    IC_OK(s == -1, "alloc: cache cheio retorna -1");
    IC_OK(extra == 0u, "alloc: out_img_id nao toca quando falha");
}

static void test_alloc_rejects_url_too_long(void) {
    image_cache_init(&g_ic);
    char big[IMAGE_CACHE_URL_MAX + 1];
    memset(big, 'x', sizeof(big));
    uint32_t id = 0;
    int s = image_cache_alloc(&g_ic, 1u, big,
                               (uint16_t)(IMAGE_CACHE_URL_MAX), &id);
    IC_OK(s == -1, "alloc: rejeita URL >= IMAGE_CACHE_URL_MAX");
}

static void test_lookup_hit_and_miss(void) {
    image_cache_init(&g_ic);
    uint32_t id = 0;
    image_cache_alloc(&g_ic, 1u, "foo", 3u, &id);

    int hit = image_cache_lookup(&g_ic, "foo", 3u);
    int miss = image_cache_lookup(&g_ic, "bar", 3u);
    IC_OK(hit >= 0, "lookup: hit retorna idx valido");
    IC_OK(miss == -1, "lookup: miss retorna -1");
    IC_OK(g_ic.total_lookups == 2u, "lookup: total_lookups=2");
    IC_OK(g_ic.total_hits == 1u, "lookup: total_hits=1");
    IC_OK(g_ic.total_misses == 1u, "lookup: total_misses=1");
}

static void test_lookup_with_null_returns_miss(void) {
    image_cache_init(&g_ic);
    int r1 = image_cache_lookup(&g_ic, NULL, 0u);
    int r2 = image_cache_lookup(&g_ic, "x", 0u);
    IC_OK(r1 == -1 && r2 == -1, "lookup: NULL/zero-len -> miss");
    IC_OK(g_ic.total_misses == 2u, "lookup: misses contados");
}

static void test_record_response_ok_fills_pixels(void) {
    image_cache_init(&g_ic);
    uint32_t id = 0;
    int slot = image_cache_alloc(&g_ic, 1u, "img.png", 7u, &id);
    IC_OK(slot >= 0 && id > 0, "record-pre: alloc ok");

    /* 2x2 BGRA = 16 bytes pixels. */
    uint8_t pixels[16];
    for (int i = 0; i < 16; ++i) pixels[i] = (uint8_t)(0x10 + i);
    struct browser_ipc_image_response resp = {
        .img_id = id,
        .nav_id = 1u,
        .status = BROWSER_IPC_IMAGE_OK,
        .format = BROWSER_IPC_IMAGE_FMT_BGRA32,
        .width = 2u,
        .height = 2u,
        .pixel_bytes = 16u,
        .pixels = pixels
    };
    int rc = image_cache_record_response(&g_ic, &resp);
    IC_OK(rc == 0, "record OK: ok");
    const struct image_cache_entry *e = image_cache_at(&g_ic, slot);
    IC_OK(e && e->status == IMAGE_CACHE_OK, "record OK: status=OK");
    IC_OK(e && e->width == 2u && e->height == 2u, "record OK: w/h corretos");
    IC_OK(e && e->pixel_bytes == 16u, "record OK: pixel_bytes=16");
    IC_OK(e && memcmp(e->pixels, pixels, 16) == 0,
         "record OK: pixels copiados byte-a-byte");
}

static void test_record_response_error_status_marks_error(void) {
    image_cache_init(&g_ic);
    uint32_t id = 0;
    int slot = image_cache_alloc(&g_ic, 1u, "x", 1u, &id);
    struct browser_ipc_image_response resp = {
        .img_id = id,
        .nav_id = 1u,
        .status = BROWSER_IPC_IMAGE_TRANSPORT_ERR,
        .format = BROWSER_IPC_IMAGE_FMT_BGRA32,
        .width = 0u, .height = 0u,
        .pixel_bytes = 0u,
        .pixels = NULL
    };
    int rc = image_cache_record_response(&g_ic, &resp);
    IC_OK(rc == 0, "record err: ok");
    const struct image_cache_entry *e = image_cache_at(&g_ic, slot);
    IC_OK(e && e->status == IMAGE_CACHE_ERROR, "record err: status=ERROR");
    IC_OK(e && e->pixel_bytes == 0u, "record err: pixel_bytes=0");
}

static void test_record_response_oversized_degrades(void) {
    image_cache_init(&g_ic);
    uint32_t id = 0;
    int slot = image_cache_alloc(&g_ic, 1u, "huge", 4u, &id);
    /* Forja resposta com pixel_bytes > MAX_PIXEL_BYTES. Pixels=NULL
     * para nao alocar 1+ MiB no test; o cache rejeita ANTES de
     * tentar copiar. */
    struct browser_ipc_image_response resp = {
        .img_id = id,
        .nav_id = 1u,
        .status = BROWSER_IPC_IMAGE_OK,
        .format = BROWSER_IPC_IMAGE_FMT_BGRA32,
        .width = 0u, .height = 0u,
        .pixel_bytes = IMAGE_CACHE_MAX_PIXEL_BYTES + 1u,
        .pixels = NULL
    };
    int rc = image_cache_record_response(&g_ic, &resp);
    IC_OK(rc == 0, "oversized: record retorna 0 (degradado)");
    const struct image_cache_entry *e = image_cache_at(&g_ic, slot);
    IC_OK(e && e->status == IMAGE_CACHE_ERROR,
         "oversized: status=ERROR");
    IC_OK(g_ic.total_oversized_drops == 1u,
         "oversized: total_oversized_drops++");
}

static void test_record_response_inconsistent_size_marks_error(void) {
    image_cache_init(&g_ic);
    uint32_t id = 0;
    int slot = image_cache_alloc(&g_ic, 1u, "z", 1u, &id);
    /* 2x2 BGRA precisa 16 bytes; passar 8 -> ERROR. */
    uint8_t pixels[8] = {0};
    struct browser_ipc_image_response resp = {
        .img_id = id,
        .nav_id = 1u,
        .status = BROWSER_IPC_IMAGE_OK,
        .format = BROWSER_IPC_IMAGE_FMT_BGRA32,
        .width = 2u, .height = 2u,
        .pixel_bytes = 8u,
        .pixels = pixels
    };
    int rc = image_cache_record_response(&g_ic, &resp);
    IC_OK(rc == 0, "inconsistent: rc=0");
    const struct image_cache_entry *e = image_cache_at(&g_ic, slot);
    IC_OK(e && e->status == IMAGE_CACHE_ERROR,
         "inconsistent: status=ERROR");
}

static void test_record_response_unknown_id_drops(void) {
    image_cache_init(&g_ic);
    /* Sem alloc: img_id 999 nao existe no cache. */
    struct browser_ipc_image_response resp = {
        .img_id = 999u,
        .status = BROWSER_IPC_IMAGE_OK,
        .format = BROWSER_IPC_IMAGE_FMT_BGRA32,
        .width = 0u, .height = 0u, .pixel_bytes = 0u,
        .pixels = NULL
    };
    int rc = image_cache_record_response(&g_ic, &resp);
    IC_OK(rc == -1, "unknown id: -1 (descartado)");
}

static void test_invalidate_other_navs_clears_old(void) {
    image_cache_init(&g_ic);
    uint32_t id1 = 0, id2 = 0, id3 = 0;
    image_cache_alloc(&g_ic, 1u, "a", 1u, &id1);
    image_cache_alloc(&g_ic, 1u, "b", 1u, &id2);
    image_cache_alloc(&g_ic, 2u, "c", 1u, &id3);

    image_cache_invalidate_other_navs(&g_ic, 2u);

    IC_OK(g_ic.total_evictions == 2u,
         "invalidate: 2 slots de nav 1 evicted");
    /* Lookups dos URLs antigos devem ser miss agora. */
    IC_OK(image_cache_lookup(&g_ic, "a", 1u) == -1,
         "invalidate: 'a' nao mais encontrado");
    IC_OK(image_cache_lookup(&g_ic, "b", 1u) == -1,
         "invalidate: 'b' nao mais encontrado");
    /* O slot da nav ativa ainda esta la. */
    IC_OK(image_cache_lookup(&g_ic, "c", 1u) >= 0,
         "invalidate: 'c' (nav ativa) preservado");
}

static void test_find_url_helper(void) {
    image_cache_init(&g_ic);
    uint32_t id = 0;
    image_cache_alloc(&g_ic, 1u, "logo", 4u, &id);
    const struct image_cache_entry *e = image_cache_find_url(&g_ic, "logo", 4u);
    IC_OK(e != NULL, "find_url: hit retorna entry");
    IC_OK(image_cache_find_url(&g_ic, "no", 2u) == NULL,
         "find_url: miss retorna NULL");
    IC_OK(image_cache_find_url(&g_ic, NULL, 0u) == NULL,
         "find_url: NULL retorna NULL");
}

int test_capybrowser_image_cache_run(void) {
    printf("[test_capybrowser_image_cache]\n");
    g_passed = 0;
    g_failed = 0;
    test_init_zero_state();
    test_alloc_assigns_monotonic_ids();
    test_alloc_fills_then_fails();
    test_alloc_rejects_url_too_long();
    test_lookup_hit_and_miss();
    test_lookup_with_null_returns_miss();
    test_record_response_ok_fills_pixels();
    test_record_response_error_status_marks_error();
    test_record_response_oversized_degrades();
    test_record_response_inconsistent_size_marks_error();
    test_record_response_unknown_id_drops();
    test_invalidate_other_navs_clears_old();
    test_find_url_helper();
    printf("  -> %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed;
}
