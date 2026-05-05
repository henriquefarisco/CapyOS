/* userland/bin/capybrowser/image_cache.c
 *
 * Etapa 3 secao a fetch+decode (2026-05-05): impl do cache de
 * imagens descrito em image_cache.h.
 *
 * Sem libc: usa apenas <stdint.h> e a struct browser_ipc_image_response
 * (que tambem e header-free de libc). Linkavel em ring 3 freestanding
 * E em testes host -- a separacao da varredura/lookup/record da
 * pipeline IPC permite cobrir todos os caminhos com testes
 * deterministicos.
 */

#include "image_cache.h"
#include <stdint.h>

/* --- helpers privados -------------------------------------------- */

static int ic_url_eq(const char *a, uint16_t a_len,
                     const char *b, uint16_t b_len) {
    if (a_len != b_len) return 0;
    for (uint16_t i = 0; i < a_len; ++i) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static void ic_clear_entry(struct image_cache_entry *e) {
    e->img_id = 0u;
    e->nav_id = 0u;
    e->status = (uint8_t)IMAGE_CACHE_FREE;
    e->reserved = 0u;
    e->width = 0u;
    e->height = 0u;
    e->url_len = 0u;
    e->pixel_bytes = 0u;
    e->url[0] = '\0';
    /* Pixels NAO sao zerados: economiza ciclos no init e no
     * invalidate. O caller le pixels apenas quando status == OK
     * + url_len > 0, e nesse caso pixels foram reescritos pelo
     * record_response. */
}

/* --- API publica ------------------------------------------------- */

void image_cache_init(struct image_cache *c) {
    if (!c) return;
    for (uint32_t i = 0; i < IMAGE_CACHE_MAX_ENTRIES; ++i) {
        ic_clear_entry(&c->entries[i]);
    }
    c->next_img_id = 1u; /* 0 e reservado para "slot livre" */
    c->total_lookups = 0u;
    c->total_hits = 0u;
    c->total_misses = 0u;
    c->total_evictions = 0u;
    c->total_oversized_drops = 0u;
}

void image_cache_invalidate_other_navs(struct image_cache *c,
                                       uint32_t active_nav) {
    if (!c) return;
    for (uint32_t i = 0; i < IMAGE_CACHE_MAX_ENTRIES; ++i) {
        struct image_cache_entry *e = &c->entries[i];
        if (e->status == (uint8_t)IMAGE_CACHE_FREE) continue;
        if (e->nav_id != active_nav) {
            ic_clear_entry(e);
            c->total_evictions++;
        }
    }
}

int image_cache_lookup(struct image_cache *c,
                       const char *url, uint16_t url_len) {
    if (!c) return -1;
    c->total_lookups++;
    if (!url || url_len == 0u) {
        c->total_misses++;
        return -1;
    }
    for (uint32_t i = 0; i < IMAGE_CACHE_MAX_ENTRIES; ++i) {
        const struct image_cache_entry *e = &c->entries[i];
        if (e->status == (uint8_t)IMAGE_CACHE_FREE) continue;
        if (ic_url_eq(e->url, e->url_len, url, url_len)) {
            c->total_hits++;
            return (int)i;
        }
    }
    c->total_misses++;
    return -1;
}

int image_cache_alloc(struct image_cache *c,
                      uint32_t nav_id,
                      const char *url, uint16_t url_len,
                      uint32_t *out_img_id) {
    if (!c || !url || url_len == 0u) return -1;
    if (url_len > (uint16_t)(IMAGE_CACHE_URL_MAX - 1u)) {
        /* URL nao cabe no slot; defensivo. */
        return -1;
    }
    /* Acha slot livre. */
    int free_idx = -1;
    for (uint32_t i = 0; i < IMAGE_CACHE_MAX_ENTRIES; ++i) {
        if (c->entries[i].status == (uint8_t)IMAGE_CACHE_FREE) {
            free_idx = (int)i;
            break;
        }
    }
    if (free_idx < 0) return -1; /* cache cheio */

    struct image_cache_entry *e = &c->entries[free_idx];
    /* Aloca id monotonico; pula 0 (sentinela de "livre"). */
    if (c->next_img_id == 0u) c->next_img_id = 1u;
    uint32_t id = c->next_img_id++;
    e->img_id = id;
    e->nav_id = nav_id;
    e->status = (uint8_t)IMAGE_CACHE_PENDING;
    e->width = 0u;
    e->height = 0u;
    e->pixel_bytes = 0u;
    e->url_len = url_len;
    for (uint16_t i = 0; i < url_len; ++i) {
        e->url[i] = url[i];
    }
    e->url[url_len] = '\0';
    if (out_img_id) *out_img_id = id;
    return free_idx;
}

int image_cache_record_response(struct image_cache *c,
                                const struct browser_ipc_image_response *resp) {
    if (!c || !resp) return -1;
    /* Casa por img_id (nao por url): a engine pode ter mais de uma
     * URL identica em pending? Nao: alloc ja descartou via lookup
     * antes. Mesmo assim, img_id e o handle canonico. */
    int found = -1;
    for (uint32_t i = 0; i < IMAGE_CACHE_MAX_ENTRIES; ++i) {
        struct image_cache_entry *e = &c->entries[i];
        if (e->status != (uint8_t)IMAGE_CACHE_FREE
            && e->img_id == resp->img_id) {
            found = (int)i;
            break;
        }
    }
    if (found < 0) {
        /* Resposta tardia ou stale; descarta sem falhar. */
        return -1;
    }
    struct image_cache_entry *e = &c->entries[found];

    if (resp->status != (uint8_t)BROWSER_IPC_IMAGE_OK) {
        /* Erro reportado pelo chrome: marca slot ERROR e zera
         * dimensoes para sinalizar que pixels nao sao validos.
         * Slot nao volta a FREE ate uma nova nav invalidate. */
        e->status = (uint8_t)IMAGE_CACHE_ERROR;
        e->width = 0u;
        e->height = 0u;
        e->pixel_bytes = 0u;
        return 0;
    }
    /* Status OK: copia pixels se couber. */
    if (resp->format != (uint8_t)BROWSER_IPC_IMAGE_FMT_BGRA32) {
        /* Formato nao suportado pelo cache (so BGRA32 hoje). */
        e->status = (uint8_t)IMAGE_CACHE_ERROR;
        return 0;
    }
    if (resp->pixel_bytes > IMAGE_CACHE_MAX_PIXEL_BYTES) {
        /* Imagem maior que o slot; degrada para ERROR + telemetria. */
        c->total_oversized_drops++;
        e->status = (uint8_t)IMAGE_CACHE_ERROR;
        e->width = 0u;
        e->height = 0u;
        e->pixel_bytes = 0u;
        return 0;
    }
    /* Defensivo: w/h/pixels consistentes. */
    if ((uint32_t)resp->width * (uint32_t)resp->height * 4u
        != resp->pixel_bytes) {
        e->status = (uint8_t)IMAGE_CACHE_ERROR;
        return 0;
    }
    e->width = resp->width;
    e->height = resp->height;
    e->pixel_bytes = resp->pixel_bytes;
    if (resp->pixels && resp->pixel_bytes > 0u) {
        for (uint32_t i = 0; i < resp->pixel_bytes; ++i) {
            e->pixels[i] = resp->pixels[i];
        }
    }
    e->status = (uint8_t)IMAGE_CACHE_OK;
    return 0;
}

const struct image_cache_entry *image_cache_at(const struct image_cache *c,
                                               int idx) {
    if (!c || idx < 0 || (uint32_t)idx >= IMAGE_CACHE_MAX_ENTRIES) {
        return (const struct image_cache_entry *)0;
    }
    return &c->entries[idx];
}

const struct image_cache_entry *image_cache_find_url(const struct image_cache *c,
                                                    const char *url,
                                                    uint16_t url_len) {
    if (!c || !url || url_len == 0u) return (const struct image_cache_entry *)0;
    for (uint32_t i = 0; i < IMAGE_CACHE_MAX_ENTRIES; ++i) {
        const struct image_cache_entry *e = &c->entries[i];
        if (e->status == (uint8_t)IMAGE_CACHE_FREE) continue;
        if (ic_url_eq(e->url, e->url_len, url, url_len)) {
            return e;
        }
    }
    return (const struct image_cache_entry *)0;
}
