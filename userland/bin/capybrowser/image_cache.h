/* userland/bin/capybrowser/image_cache.h
 *
 * Etapa 3 secao a fetch+decode (2026-05-05): cache fixo de imagens
 * para o engine ring 3. Cada slot guarda pixels BGRA decodificados
 * + metadados (URL, nav_id, status, dimensoes) prontos para o raster
 * blittar no lugar do placeholder.
 *
 * Princípios:
 *
 *   - Sem alocador dinamico: cache mora em .bss do engine, fixo em
 *     IMAGE_CACHE_MAX_ENTRIES slots × IMAGE_CACHE_MAX_PIXEL_BYTES
 *     pixels.
 *   - Lookup por URL (sem hash, varredura linear -- 4 entries).
 *   - Invalidacao por nav: novos NAV_STARTED zeram entries com
 *     nav_id antigo para liberar memoria a respostas tardias serem
 *     descartadas pelo handler.
 *   - Estados: FREE -> PENDING (apos REQUEST emitido) -> OK | ERROR.
 *   - Testavel em host: API pura (sem syscalls), so manipula a
 *     struct passada.
 *
 * Tamanho de slot: 240×180 BGRA = 172800 bytes. Total cache = 4 ×
 * 172800 = ~675 KiB. Imagens maiores sao rejeitadas com status
 * OVERSIZED no record_response (placeholder permanece visivel).
 */
#ifndef CAPYBROWSER_IMAGE_CACHE_H
#define CAPYBROWSER_IMAGE_CACHE_H

#include <stdint.h>
#include "apps/browser_ipc.h"

/* Sizing trade-off (2026-05-05):
 *
 *   - 240×180 BGRA per slot = 172800 bytes ~= 169 KiB.
 *   - 4 slots = 691200 bytes ~= 675 KiB total cache footprint.
 *
 * O .bss do engine hoje carrega g_frame_payload (~675 KiB) +
 * g_request_payload (1 MiB) + g_fetch_scratch (1 MiB) =~ 2.7 MiB.
 * Adicionar ~675 KiB do cache mantem o total em ~3.4 MiB, dentro
 * do que o ELF loader aceita por processo. Imagens maiores que
 * 240×180 BGRA caem em status OVERSIZED (placeholder permanece
 * visivel; user feedback degrada de "imagem decodificada" para
 * "rectangulo cinza com alt"). */
#define IMAGE_CACHE_MAX_ENTRIES     4u
#define IMAGE_CACHE_MAX_W           240u
#define IMAGE_CACHE_MAX_H           180u
#define IMAGE_CACHE_MAX_PIXEL_BYTES (IMAGE_CACHE_MAX_W * IMAGE_CACHE_MAX_H * 4u)
#define IMAGE_CACHE_URL_MAX         256u

enum image_cache_status {
    IMAGE_CACHE_FREE     = 0u, /* slot livre */
    IMAGE_CACHE_PENDING  = 1u, /* REQUEST emitido, aguardando RESPONSE */
    IMAGE_CACHE_OK       = 2u, /* pixels validos */
    IMAGE_CACHE_ERROR    = 3u  /* RESPONSE veio com status nao-OK */
};

struct image_cache_entry {
    uint32_t img_id;                              /* engine-assigned, 0 = livre */
    uint32_t nav_id;                              /* nav owning este slot */
    uint8_t  status;                              /* enum image_cache_status */
    uint8_t  reserved;                            /* alinhamento */
    uint16_t width;
    uint16_t height;
    uint16_t url_len;
    uint32_t pixel_bytes;
    char     url[IMAGE_CACHE_URL_MAX];
    uint8_t  pixels[IMAGE_CACHE_MAX_PIXEL_BYTES];
};

struct image_cache {
    struct image_cache_entry entries[IMAGE_CACHE_MAX_ENTRIES];
    uint32_t                 next_img_id; /* monotonico; >0 vira img_id */
    uint32_t                 total_lookups;
    uint32_t                 total_hits;
    uint32_t                 total_misses;
    uint32_t                 total_evictions;
    uint32_t                 total_oversized_drops;
};

/* Inicializa o cache: zera todos os slots e contadores. */
void image_cache_init(struct image_cache *c);

/* Invalida (volta a FREE) todos os slots cujo nav_id != active_nav.
 * Slots livres ficam livres. Util ao receber NAV_STARTED com novo
 * nav_id: respostas tardias sao descartadas porque o slot deixa
 * de existir. Total contado em total_evictions. */
void image_cache_invalidate_other_navs(struct image_cache *c,
                                       uint32_t active_nav);

/* Procura uma entry pelo URL. Retorna o indice [0..MAX-1] ou -1 se
 * miss. Cache hit nao desempata por nav_id por design: imagens com
 * URLs comuns entre navs (ex.: logo do site) reusam pixels. */
int image_cache_lookup(struct image_cache *c,
                       const char *url, uint16_t url_len);

/* Aloca um slot novo para `url` em `nav_id`. Estado fica PENDING e
 * `*out_img_id` recebe o id atribuido (>0). Retorna o indice do
 * slot, ou -1 se cache cheio. Caller deve emitir IMAGE_REQUEST com
 * (img_id, nav_id, url) ao chrome. */
int image_cache_alloc(struct image_cache *c,
                      uint32_t nav_id,
                      const char *url, uint16_t url_len,
                      uint32_t *out_img_id);

/* Atualiza um slot a partir de IMAGE_RESPONSE. Casa o slot por
 * img_id (nao pelo URL, que poderia ter colidido). Retorna 0 em
 * sucesso, -1 se nao achou img_id (tarde demais). Preenche pixels
 * + width/height + status. Imagens > IMAGE_CACHE_MAX_PIXEL_BYTES
 * sao registradas como ERROR + OVERSIZED para nao corromper o
 * slot. */
int image_cache_record_response(struct image_cache *c,
                                const struct browser_ipc_image_response *resp);

/* Acessor read-only por indice ou por URL. NULL se invalido. */
const struct image_cache_entry *image_cache_at(const struct image_cache *c,
                                               int idx);
const struct image_cache_entry *image_cache_find_url(const struct image_cache *c,
                                                    const char *url,
                                                    uint16_t url_len);

#endif /* CAPYBROWSER_IMAGE_CACHE_H */
