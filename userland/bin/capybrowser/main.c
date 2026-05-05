/*
 * userland/bin/capybrowser/main.c -- F3.3b: stub do browser engine
 * em ring 3.
 *
 * Esta primeira versao implementa o loop minimo de IPC do browser:
 *   - Le frames do fd 0 (request pipe do chrome).
 *   - Decodifica o header binario big-endian via codec compartilhado.
 *   - Despacha por kind e responde no fd 1 (response pipe).
 *   - Termina em SHUTDOWN ou erro irrecuperavel de leitura.
 *
 * Cobertura desta fase:
 *   - NAVIGATE  -> NAV_STARTED + NAV_PROGRESS(fetch/parse/render) +
 *                  EVENT_FRAME (16x16 RGBA solido) + NAV_READY.
 *   - PING      -> EVENT_PONG (mesmo nonce).
 *   - SHUTDOWN  -> capy_exit(0).
 *   - CANCEL    -> NAV_CANCELLED imediato (sem render real).
 *   - Outros    -> drena payload e ignora.
 *
 * Renderizacao real, parser HTML/CSS, fetch HTTP e watchdog ficam
 * para F3.3c/F3.3d. Este binario apenas prova que o protocolo
 * funciona end-to-end sobre os pipes do M5.
 *
 * Restricoes (mesma trilha de hello/exectarget/capysh):
 *   - Sem libc.
 *   - Sem alocacoes dinamicas; tudo em stack ou .data/.bss.
 *     (O ELF loader em src/kernel/elf_loader.c zera as paginas
 *     mapeadas para PT_LOAD em paddr_alloc, entao .bss e
 *     seguro neste binario.)
 */

#include "apps/browser_ipc.h"
#include "apps/browser_dimensions.h"
#include "capyhtml/parser.h"
#include "capyhtml/render.h"
#include "capyhtml/raster.h"
#include "capyhtml/font.h"
#include "image_cache.h" /* Etapa 3 secao a fetch+decode (2026-05-05) */
#include <capylibc/capylibc.h>
#include <stdint.h>

/* Slice 4-final: viewport real do engine. As dimensoes vem de
 * `include/apps/browser_dimensions.h` (compartilhado com o chrome
 * ring 0); ENGINE_* sao apenas aliases locais para nao reescrever
 * todo o codigo abaixo. O buffer fica em .bss (zerada na carga).
 * EVENT_FRAME envia 12 B de header + W*H*4 = 675 KiB, dentro do
 * limite de 1 MiB do IPC. 2026-05-02: bump 192x128 -> 480x360 apos
 * feedback UX (janela pequena demais nao mostrava conteudo legivel).
 * O scratch da chrome runtime cresceu de 128 KiB para 768 KiB em
 * paralelo. */
#define ENGINE_FB_W       BROWSER_FRAME_W
#define ENGINE_FB_H       BROWSER_FRAME_H
#define ENGINE_FB_STRIDE  BROWSER_FRAME_STRIDE
#define ENGINE_FB_PIXELS  BROWSER_FRAME_PIXEL_BYTES
#define ENGINE_FRAME_HDR  BROWSER_FRAME_HDR_BYTES
#define ENGINE_FRAME_TOTAL BROWSER_FRAME_TOTAL_BYTES

/* Tamanho do frame stub usado quando o body e vazio ou parse falha. */
#define STUB_FRAME_W 16
#define STUB_FRAME_H 16
#define STUB_FRAME_STRIDE (STUB_FRAME_W * 4u)
#define STUB_FRAME_BYTES  (STUB_FRAME_STRIDE * STUB_FRAME_H)

/* Limite de payload aceito sem alocacao dinamica. Slice 5d
 * aumentou de 1 KiB para 4 KiB para acomodar FETCH_RESPONSE
 * (16 prefix + ctype + body) das paginas embutidas. F3.3g
 * (2026-05-02) bumpou para BROWSER_IPC_MAX_PAYLOAD + 4 KiB
 * para acomodar o body real de HTTP(S) que o chrome comecou a
 * entregar via `net/http.h` (cap do stack = 1 MiB). Como um
 * buffer desse tamanho nao cabe na stack do engine ring 3,
 * as duas areas -- payload de request e scratch de fetch --
 * passaram para `.bss` via `g_request_payload` e
 * `g_fetch_scratch` logo abaixo. */
#define INPUT_PAYLOAD_BUF (1u * 1024u * 1024u + 4096u)

static const char k_log_started[] = "[capybrowser] engine v0 online\n";

/* Etapa 3 seção c (2026-05-03): strlen freestanding (capybrowser
 * compila com -nostdinc; nao queremos depender da libc do host).
 * Tem o mesmo contrato do strlen padrao mas operando byte por byte. */
static size_t cap_strlen_local(const char *s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

/* read_full: le exatamente n bytes do fd ou retorna -1 em EOF/erro.
 * Usa busy-yield em caso de short read para ceder CPU. */
static long read_full(int fd, void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < n) {
        long rd = capy_read(fd, p + got, n - got);
        if (rd < 0) {
            return -1;
        }
        if (rd == 0) {
            /* EOF: pipe write-end fechou. */
            return -1;
        }
        got += (size_t)rd;
    }
    return (long)n;
}

/* write_full: escreve exatamente n bytes ou retorna -1 em broken pipe. */
static long write_full(int fd, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < n) {
        long wr = capy_write(fd, p + sent, n - sent);
        if (wr <= 0) {
            return -1;
        }
        sent += (size_t)wr;
    }
    return (long)n;
}

/* Sequencia local de eventos enviados pelo engine. */
static uint32_t g_resp_seq = 0;

/* 2026-05-02: viewport dinamica controlada pelo BROWSER_IPC_RESIZE
 * que o chrome envia quando o usuario redimensiona a janela.
 * Inicia em DEFAULT (matching a janela inicial criada pelo
 * browser_app), e e clampada em [1..BROWSER_FRAME_MAX_*]. Sem
 * estes globais, o engine sempre rasterizava em DEFAULT mesmo
 * quando o usuario aumentava a janela; o conteudo nao se adaptava
 * e ficava encalhado em 480x360 no canto da janela maior. */
static uint16_t g_vw = (uint16_t)BROWSER_FRAME_DEFAULT_W;
static uint16_t g_vh = (uint16_t)BROWSER_FRAME_DEFAULT_H;

/* Emite um frame IPC (header + payload). Retorna 0 ok, -1 broken pipe. */
static int send_frame(uint16_t kind, const void *payload, uint32_t payload_len) {
    struct browser_ipc_header hdr = {
        .magic       = BROWSER_IPC_MAGIC,
        .kind        = kind,
        .seq         = ++g_resp_seq,
        .payload_len = payload_len
    };
    uint8_t hdr_buf[BROWSER_IPC_HEADER_SIZE];
    if (browser_ipc_header_encode(&hdr, hdr_buf, sizeof(hdr_buf)) != BROWSER_IPC_OK) {
        return -1;
    }
    if (write_full(1, hdr_buf, sizeof(hdr_buf)) < 0) {
        return -1;
    }
    if (payload_len > 0u) {
        if (write_full(1, payload, payload_len) < 0) {
            return -1;
        }
    }
    return 0;
}

/* Helpers para escrever inteiros big-endian em buffer de payload. */
static void be_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void be_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint16_t be_get_u16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t be_get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)
         |  (uint32_t)p[3];
}

/* Sequencia de eventos para uma navegacao. nav_id distingue
 * navegacoes consecutivas; o chrome ignora frames com nav_id antigo. */
static uint32_t g_nav_id = 0;

static int emit_log(uint8_t level, const char *msg) {
    /* level u8 + msg_len u16 + msg utf8 */
    size_t ml = 0;
    while (msg && msg[ml] != 0 && ml < 0xFFFFu) ml++;
    uint8_t buf[3 + 256];
    if (ml > 256u) ml = 256u;
    buf[0] = level;
    be_put_u16(&buf[1], (uint16_t)ml);
    for (size_t i = 0; i < ml; i++) buf[3 + i] = (uint8_t)msg[i];
    return send_frame(BROWSER_IPC_EVENT_LOG, buf, (uint32_t)(3u + ml));
}

static int emit_nav_started(uint32_t nav_id, const uint8_t *url, uint16_t url_len) {
    uint8_t buf[6 + 1024];
    if ((uint32_t)url_len > 1024u) url_len = 1024u;
    be_put_u32(&buf[0], nav_id);
    be_put_u16(&buf[4], url_len);
    for (uint16_t i = 0; i < url_len; i++) buf[6 + i] = url[i];
    return send_frame(BROWSER_IPC_EVENT_NAV_STARTED, buf, (uint32_t)(6u + url_len));
}

/* Etapa 3 seção b-polish (2026-05-03): emite EVENT_TITLE com o
 * `<title>` extraido pelo parser. O chrome chama `compositor_set_title`
 * no browser_app; sem este emit, a janela fica sempre com "CapyBrowser"
 * mesmo apos carregar uma pagina com titulo proprio.
 *
 * Payload: title_len u16 BE + title utf8. Cap em 256 bytes (maior que
 * BROWSER_CHROME_TITLE_MAX=192 para tolerar truncamento no chrome sem
 * perder bytes no IPC, o dispatcher clampa com copy_clamped). */
static int emit_title(const char *title) {
    if (!title) return 0;
    uint16_t tlen = 0;
    while (title[tlen] != '\0' && tlen < 256u) tlen++;
    if (tlen == 0u) return 0; /* titulo vazio: no-op para nao limpar titulo anterior */
    uint8_t buf[2 + 256];
    be_put_u16(&buf[0], tlen);
    for (uint16_t i = 0; i < tlen; ++i) buf[2 + i] = (uint8_t)title[i];
    return send_frame(BROWSER_IPC_EVENT_TITLE, buf, (uint32_t)(2u + tlen));
}

static int emit_nav_progress(uint32_t nav_id, uint8_t stage, uint8_t percent) {
    uint8_t buf[6];
    be_put_u32(&buf[0], nav_id);
    buf[4] = stage;
    buf[5] = percent;
    return send_frame(BROWSER_IPC_EVENT_NAV_PROGRESS, buf, sizeof(buf));
}

static int emit_nav_ready(uint32_t nav_id) {
    uint8_t buf[4];
    be_put_u32(buf, nav_id);
    return send_frame(BROWSER_IPC_EVENT_NAV_READY, buf, sizeof(buf));
}

static int emit_nav_cancelled(uint32_t nav_id) {
    uint8_t buf[4];
    be_put_u32(buf, nav_id);
    return send_frame(BROWSER_IPC_EVENT_NAV_CANCELLED, buf, sizeof(buf));
}

static int emit_pong(uint32_t nonce) {
    uint8_t buf[4];
    be_put_u32(buf, nonce);
    return send_frame(BROWSER_IPC_EVENT_PONG, buf, sizeof(buf));
}

/* slice 5d: pede o conteudo da URL ao chrome via fetch IPC. Layout
 * da payload: seq u32 + nav_id u32 + method u8 + url_len u16 + url[].
 * `seq` aqui e do counter do engine (g_resp_seq) e e ecoado pelo
 * chrome no FETCH_RESPONSE para correlacao. */
static int emit_fetch_request(uint32_t seq, uint32_t nav_id,
                              const uint8_t *url, uint16_t url_len) {
    uint8_t buf[11 + 1024];
    if (url_len > 1024u) url_len = 1024u;
    be_put_u32(&buf[0], seq);
    be_put_u32(&buf[4], nav_id);
    buf[8] = (uint8_t)BROWSER_IPC_FETCH_GET;
    be_put_u16(&buf[9], url_len);
    for (uint16_t i = 0; i < url_len; ++i) buf[11 + i] = url[i];
    return send_frame(BROWSER_IPC_EVENT_FETCH_REQUEST,
                      buf, (uint32_t)(11u + url_len));
}

/* Etapa 3 secao a fetch+decode (2026-05-05): emite IMAGE_REQUEST
 * para o chrome. Payload: img_id u32 + nav_id u32 + url_len u16 +
 * url. URL clamped ao limite que cabe no buffer scratch local
 * (1 KiB; URLs maiores sao raras em <img src=> e o engine ja
 * trunca paths longos no parser). */
static int emit_image_request(uint32_t img_id, uint32_t nav_id,
                              const uint8_t *url, uint16_t url_len) {
    uint8_t buf[10 + 1024];
    if (url_len > 1024u) url_len = 1024u;
    struct browser_ipc_image_request req;
    req.img_id = img_id;
    req.nav_id = nav_id;
    req.url_len = url_len;
    req.url = url;
    uint32_t n = 0;
    if (browser_ipc_image_request_encode(&req, buf, sizeof(buf), &n)
        != BROWSER_IPC_OK) {
        return -1;
    }
    return send_frame(BROWSER_IPC_EVENT_IMAGE_REQUEST, buf, n);
}

static int emit_nav_failed(uint32_t nav_id, const char *reason) {
    size_t rl = 0;
    while (reason && reason[rl] != '\0' && rl < 200u) ++rl;
    uint8_t buf[6 + 200];
    be_put_u32(&buf[0], nav_id);
    be_put_u16(&buf[4], (uint16_t)rl);
    for (size_t i = 0; i < rl; ++i) buf[6 + i] = (uint8_t)reason[i];
    return send_frame(BROWSER_IPC_EVENT_NAV_FAILED, buf,
                      (uint32_t)(6u + rl));
}

/* Slice 4-final: framebuffer real do engine. 96 KiB em .bss,
 * preenchido pelo rasterizer libcapyhtml. O layout combina o
 * header de 12 B (nav_id u32 + width u16 + height u16 + stride
 * u32) seguido pelos pixels BGRA, de modo que enviamos o conjunto
 * inteiro como uma unica payload de EVENT_FRAME, sem nenhum buffer
 * intermediario na pilha. */
static uint8_t g_frame_payload[ENGINE_FRAME_TOTAL];

/* Etapa 3 secao a fetch+decode (2026-05-05): cache de imagens
 * decodificadas. ~675 KiB em .bss; ver image_cache.h para o
 * trade-off de tamanho. Inicializado em main(). */
static struct image_cache g_image_cache;

/* F3.3g: scratches em .bss para receber request payload (main loop)
 * e fetch response (run_navigate/wait_for_fetch_response). Ambos
 * precisam caber ate BROWSER_IPC_MAX_PAYLOAD (= 1 MiB) porque o
 * chrome agora entrega bodies HTTP reais. Alocar na stack da tarefa
 * ring 3 faria o kernel abortar no primeiro SYSCALL de read_full.
 * Os dois buffers NAO se sobrepoem em tempo: enquanto run_navigate
 * mantem `g_request_payload` ocupado com a URL, `g_fetch_scratch`
 * recebe o body. Fazer union poderia ter sentido, mas manter
 * separados simplifica o codigo e a pegada (2 MiB + 3 MiB de
 * frame) fica bem abaixo do limite do heap de tarefa ring 3. */
static uint8_t g_request_payload[INPUT_PAYLOAD_BUF];
static uint8_t g_fetch_scratch[INPUT_PAYLOAD_BUF];

/* Etapa 3 (2026-05-02): estado persistente por navegacao. O engine
 * precisa reter o documento parseado entre requests IPC para que
 * CLICK/SCROLL/RELOAD possam operar sem outro round-trip de fetch:
 *  - `g_current_doc`: ultima arvore parseada com sucesso;
 *  - `g_current_doc_valid`: 1 se `g_current_doc` tem conteudo util;
 *  - `g_current_nav`: nav_id da ultima navegacao completada (usado
 *    como nav_id dos frames re-emitidos por SCROLL);
 *  - `g_current_url` / `g_current_url_len`: copia da URL ativa para
 *    permitir RELOAD e para dar ao CLICK um base href (ainda nao
 *    resolvemos URLs relativas nesta slice; link com `/` no href
 *    e tratado como path absoluto e herda scheme/host via browser
 *    chrome fallback resolver);
 *  - `g_scroll_y_px`: deslocamento vertical em pixels (>= 0); o
 *    raster gera frames com os cmds transladados em -scroll_y
 *    (conteudo sobe quando o usuario rola para baixo);
 *  - `g_content_height_px`: altura total do documento layoutado
 *    (usado para clampar scroll e impedir over-scroll no fim da
 *    pagina).
 *
 * Capacidade: 64 nodes * ~520 B = ~33 KiB; perfeitamente cabe em
 * .bss ring 3 sem custo adicional. */
static struct capyhtml_document g_current_doc;
static uint8_t  g_current_doc_valid = 0u;
static uint32_t g_current_nav = 0u;
static char     g_current_url[1024];
static uint16_t g_current_url_len = 0u;
static int32_t  g_scroll_y_px = 0;
static int32_t  g_content_height_px = 0;

/* Etapa 3 seção c (2026-05-03): foco do INPUT atual. -1 = nenhum
 * input focado; valores >= 0 sao indices em `g_current_doc.nodes[]`
 * que apontam para um node CAPYHTML_NODE_TAG_INPUT. KEY events
 * append/remove caracteres em `g_current_doc.nodes[idx].text` quando
 * este indice e valido; CLICK em um input atualiza este valor; cada
 * NAVIGATE bem-sucedido reseta para -1 (os indices antigos
 * referenciam o doc antigo que foi sobrescrito). */
static int32_t g_focused_input_idx = -1;

/* Etapa 3 seção b-polish (2026-05-03): historico de navegacao para
 * BACK/FORWARD. Mantido como um anel estatico em `.bss`:
 *  - `g_history[i]`: URL do slot i (NUL-terminated);
 *  - `g_history_count`: numero total de entradas validas em
 *    [0..g_history_count);
 *  - `g_history_index`: posicao atual; `g_history[g_history_index]`
 *    e a URL ativa quando `g_history_count > 0`;
 *  - `g_nav_from_history`: flag transiente setada pelos handlers
 *    BACK/FORWARD para avisar `run_navigate` a NAO re-push (caso
 *    contrario BACK→FORWARD→BACK viraria spam de duplicatas).
 *
 * Capacidade: 32 entradas * 1024 B = 32 KiB. Acima disso, a pilha
 * descarta a entrada mais antiga (ring buffer), o que e o mesmo
 * comportamento do Chrome/Firefox. FORWARD apos uma nova navegacao
 * manual trunca o "futuro" (histories past current index), igualando
 * o padrao dos browsers reais. */
#define CAPYBROWSER_HISTORY_MAX 32u
#define CAPYBROWSER_HISTORY_URL_MAX 1024u
static char     g_history[CAPYBROWSER_HISTORY_MAX][CAPYBROWSER_HISTORY_URL_MAX];
static uint16_t g_history_len[CAPYBROWSER_HISTORY_MAX];
static uint32_t g_history_count = 0u;
static uint32_t g_history_index = 0u;
static uint8_t  g_nav_from_history = 0u;

/* Paleta default mapeada para os papeis logicos do layout. As cores
 * sao escolhidas para combinar com o tema padrao (`capyos`) do
 * compositor: fundo claro, texto rose-900 escuro, links pink-600
 * sublinhados, etc. Quando o chrome propagar o tema via REQUEST
 * de configuracao (slice futura), esta paleta vira parametro. */
static void engine_default_palette(struct capyhtml_palette *out) {
    out->color_argb[CAPYHTML_COLOR_TEXT]    = 0xFF181818u; /* near-black */
    out->color_argb[CAPYHTML_COLOR_HEADING] = 0xFF7A1F3Eu; /* rose-800 */
    out->color_argb[CAPYHTML_COLOR_LINK]    = 0xFFAD2D5Bu; /* pink-600 */
    out->color_argb[CAPYHTML_COLOR_MUTED]   = 0xFF6E6E6Eu; /* gray-500 */
    out->color_argb[CAPYHTML_COLOR_BULLET]  = 0xFF7A1F3Eu; /* same as heading */
    out->background_argb                    = 0xFFFAF7F7u; /* warm off-white */
}

/* Etapa 3 secao a fetch+decode (2026-05-05): walk no doc parseado
 * e dispara IMAGE_REQUEST para cada `<img src=...>` que ainda nao
 * tem cache hit. O engine emite no maximo IMAGE_CACHE_MAX_ENTRIES
 * requests por nav (cache cheio = restantes ficam com placeholder).
 * Idempotente: chamar 2x no mesmo doc nao duplica requests porque
 * o lookup encontra a entry PENDING/OK do primeiro call.
 *
 * Antes do walk, invalida slots de navs antigas para liberar
 * memoria as URLs novas. Respostas tardias da nav anterior ainda
 * podem chegar; record_response casa por img_id e simplesmente
 * descarta (returns -1) se o slot nao existe mais. */
static void engine_request_images_for_doc(const struct capyhtml_document *doc,
                                          uint32_t nav_id) {
    if (!doc) return;
    image_cache_invalidate_other_navs(&g_image_cache, nav_id);
    for (int i = 0; i < doc->node_count; ++i) {
        if (doc->nodes[i].type != CAPYHTML_NODE_TAG_IMG) continue;
        const char *src = doc->nodes[i].href; /* IMG src vai em href */
        if (!src || src[0] == '\0') continue;
        size_t url_len_sz = cap_strlen_local(src);
        if (url_len_sz == 0u || url_len_sz > 0xFFFFu) continue;
        uint16_t url_len = (uint16_t)url_len_sz;

        /* Hit (PENDING/OK/ERROR) -> nada a fazer. */
        if (image_cache_lookup(&g_image_cache, src, url_len) >= 0) {
            continue;
        }
        /* Miss: aloca slot + emite REQUEST. */
        uint32_t img_id = 0u;
        int slot = image_cache_alloc(&g_image_cache, nav_id,
                                      src, url_len, &img_id);
        if (slot < 0) {
            /* Cache cheio; resto fica placeholder. */
            break;
        }
        (void)emit_image_request(img_id, nav_id,
                                  (const uint8_t *)src, url_len);
    }
}

/* Forward decl para que engine_handle_image_response possa pedir
 * re-render apos receber pixels novos. */
static int32_t emit_real_frame_scrolled(uint32_t nav_id,
                                         const struct capyhtml_document *doc,
                                         int32_t scroll_y_px);

/* Etapa 3 secao a fetch+decode (2026-05-05): handler para
 * BROWSER_IPC_IMAGE_RESPONSE. Decodifica payload, atualiza o cache,
 * e dispara um re-render do doc atual para que o raster use os
 * pixels recem-recebidos no lugar do placeholder. */
static void engine_handle_image_response(const uint8_t *payload,
                                          uint32_t payload_len) {
    struct browser_ipc_image_response resp;
    if (browser_ipc_image_response_decode(payload, payload_len, &resp)
        != BROWSER_IPC_OK) {
        return;
    }
    /* record_response casa por img_id; nav_id stale gera -1
     * silencioso. */
    (void)image_cache_record_response(&g_image_cache, &resp);
    /* Re-render se temos doc valido E a resposta diz respeito a
     * nav corrente. Re-render para nav antiga seria desperdicio
     * (frame ja invalidado pelo proximo NAV_STARTED). g_current_doc
     * etc. sao file-scope statics ja declarados acima -- referenciar
     * direto sem extern. */
    if (g_current_doc_valid && resp.nav_id == g_current_nav) {
        (void)emit_real_frame_scrolled(g_current_nav, &g_current_doc,
                                        g_scroll_y_px);
    }
}

/* Slice 4-final + Etapa 3 seção e (2026-05-02): layout + raster do
 * documento parseado com suporte a scroll vertical. Quando `doc`
 * produz pelo menos uma cmd, renderizamos o framebuffer inteiro e
 * enviamos via EVENT_FRAME. O scroll e aplicado subtraindo
 * `scroll_y_px` da coordenada Y de cada cmd antes do raster; cmds
 * que caem fora da viewport sao clipados pelo raster (put_pixel ja
 * bound-checka). Em caso de buffer vazio ou cmd_count=0, o caller
 * pode optar por fallback para emit_stub_frame. Retorna o
 * `total_height_px` do layout em sucesso (>= 0) para permitir
 * clamp do scroll no caller, ou -1 em erro de IPC/layout. */
static int32_t emit_real_frame_scrolled(uint32_t nav_id,
                                         const struct capyhtml_document *doc,
                                         int32_t scroll_y_px) {
    struct capyhtml_font_ops font;
    capyhtml_font_ops_default(&font);

    /* 2026-05-02: viewport dinamica. `g_vw`/`g_vh` sao atualizadas
     * pelo handler de BROWSER_IPC_RESIZE. Layout, raster e o header
     * do frame leem dessas globals em vez do MAX, de modo que o
     * conteudo se adapta ao tamanho atual da janela do chrome. */
    uint16_t vw = g_vw;
    uint16_t vh = g_vh;
    uint32_t stride = (uint32_t)vw * 4u;
    uint32_t pixel_bytes = stride * (uint32_t)vh;

    /* Layout: ate 64 cmds no stack (~3 KiB), suficiente para a
     * pagina-modelo (welcome=7 cmds, about=12, demo=18). */
    struct capyhtml_cmd cmds[CAPYHTML_RENDER_RECOMMENDED_CAPACITY];
    struct capyhtml_render_result rr;
    int rc = capyhtml_layout(doc, &font, vw, cmds,
                             (uint16_t)CAPYHTML_RENDER_RECOMMENDED_CAPACITY,
                             &rr);
    if (rc != 0) return -1;

    /* Etapa 3 seção e: translaciona todos os cmds em -scroll_y_px.
     * cmds fora da viewport [0, vh) sao descartados pelo raster via
     * put_pixel bound-check; nao precisamos filtrar aqui. */
    if (scroll_y_px != 0) {
        for (uint16_t i = 0; i < rr.cmd_count; ++i) {
            cmds[i].y -= scroll_y_px;
        }
    }

    /* Etapa 3 seção c polish (2026-05-03): marca o cmd CMD_INPUT
     * cujo node_idx bate com `g_focused_input_idx` setando o bit
     * CAPYHTML_INPUT_FLAG_FOCUSED em reserved[0]. O raster le esse
     * bit para desenhar borda destacada + caret. Mantem o estado de
     * foco isolado no engine; o lib de raster/render fica stateless.
     * O loop e linear sobre cmd_count (<= 64), sem custo perceptivel. */
    if (g_focused_input_idx >= 0) {
        for (uint16_t i = 0; i < rr.cmd_count; ++i) {
            if (cmds[i].kind != CAPYHTML_CMD_INPUT) continue;
            uint16_t ni = CAPYHTML_CMD_NODE_IDX(cmds[i]);
            if ((int32_t)ni == g_focused_input_idx) {
                cmds[i].reserved[0] =
                    (uint8_t)(cmds[i].reserved[0]
                              | CAPYHTML_INPUT_FLAG_FOCUSED);
            }
        }
    }

    /* Etapa 3 secao a fetch+decode (2026-05-05): para cada CMD_IMAGE
     * com cache hit em status OK, popula image_pixels/w/h com os
     * pixels BGRA decodificados. Raster vai blittar em vez de
     * placeholder. CMDs com cache miss/PENDING/ERROR mantem
     * image_pixels=NULL (zero do layout) e raster cai no
     * placeholder. */
    for (uint16_t i = 0; i < rr.cmd_count; ++i) {
        if (cmds[i].kind != CAPYHTML_CMD_IMAGE) continue;
        if (!cmds[i].href || cmds[i].href[0] == '\0') continue;
        size_t url_len = cap_strlen_local(cmds[i].href);
        if (url_len == 0u || url_len > 0xFFFFu) continue;
        const struct image_cache_entry *e =
            image_cache_find_url(&g_image_cache, cmds[i].href,
                                 (uint16_t)url_len);
        if (!e || e->status != IMAGE_CACHE_OK) continue;
        if (e->pixel_bytes == 0u || e->width == 0u || e->height == 0u) {
            continue;
        }
        cmds[i].image_pixels = e->pixels;
        cmds[i].image_w = e->width;
        cmds[i].image_h = e->height;
    }

    /* Raster no tamanho atual da viewport. O buffer estatico
     * `g_frame_payload` e dimensionado pelo MAX, entao cabe qualquer
     * vw*vh <= MAX_W*MAX_H, mas escrevemos somente os primeiros
     * `pixel_bytes` bytes. */
    struct capyhtml_palette pal;
    engine_default_palette(&pal);
    struct capyhtml_raster_target tgt;
    tgt.pixels    = &g_frame_payload[ENGINE_FRAME_HDR];
    tgt.width_px  = (uint32_t)vw;
    tgt.height_px = (uint32_t)vh;
    tgt.stride_b  = (int32_t)stride;
    capyhtml_raster_render(&tgt, cmds, rr.cmd_count, &pal);

    /* Frame header. */
    be_put_u32(&g_frame_payload[0], nav_id);
    be_put_u16(&g_frame_payload[4], vw);
    be_put_u16(&g_frame_payload[6], vh);
    be_put_u32(&g_frame_payload[8], stride);

    if (send_frame(BROWSER_IPC_EVENT_FRAME, g_frame_payload,
                   ENGINE_FRAME_HDR + pixel_bytes) < 0) {
        return -1;
    }
    return rr.total_height_px;
}

/* Pinta um frame solido azul (0xFF1B66CB ARGB) na area da viewport.
 * Layout: BGRA8888 little-endian (compativel com framebuffer Capy). */
static int emit_stub_frame(uint32_t nav_id) {
    /* Header do frame: nav_id u32 + width u16 + height u16 + stride u32. */
    uint8_t hdr[12];
    be_put_u32(&hdr[0], nav_id);
    be_put_u16(&hdr[4], (uint16_t)STUB_FRAME_W);
    be_put_u16(&hdr[6], (uint16_t)STUB_FRAME_H);
    be_put_u32(&hdr[8], STUB_FRAME_STRIDE);

    /* Sem blit real ainda: mando apenas o header + payload zeros para
     * caber no protocolo. F3.3c trara pixels de verdade. */
    static const uint8_t pixel_ab[4] = { 0xCBu, 0x66u, 0x1Bu, 0xFFu }; /* B,G,R,A */
    uint8_t pixels[STUB_FRAME_BYTES];
    for (uint32_t i = 0; i < STUB_FRAME_BYTES; i += 4u) {
        pixels[i + 0] = pixel_ab[0];
        pixels[i + 1] = pixel_ab[1];
        pixels[i + 2] = pixel_ab[2];
        pixels[i + 3] = pixel_ab[3];
    }

    /* Envia hdr + pixels como um unico frame IPC (12 + STUB_FRAME_BYTES). */
    uint8_t payload[12u + STUB_FRAME_BYTES];
    for (uint32_t i = 0; i < 12u; i++) payload[i] = hdr[i];
    for (uint32_t i = 0; i < STUB_FRAME_BYTES; i++) payload[12u + i] = pixels[i];
    return send_frame(BROWSER_IPC_EVENT_FRAME, payload, (uint32_t)sizeof(payload));
}

/* Etapa 3 b-polish++ (2026-05-03): buffer `.bss` para montar o
 * HTML da pagina de erro sem tocar na stack. Cap em 2 KiB e mais
 * que suficiente para h1 + 3 paragrafos com URL (<= 1024 bytes) e
 * reason (<= 64 bytes). */
#define ENGINE_ERROR_HTML_CAP 2048u
static char g_error_html_buf[ENGINE_ERROR_HTML_CAP];

/* Escreve `s` em `buf[pos..cap-1]` truncando se faltar espaco.
 * Retorna `pos` avancado. O terminator NUL nao e escrito aqui:
 * o caller fecha a string uma unica vez apos montar tudo. */
static size_t err_append(char *buf, size_t cap, size_t pos, const char *s) {
    if (!buf || !s) return pos;
    while (*s && pos + 1u < cap) {
        buf[pos++] = *s++;
    }
    return pos;
}

/* Igual ao `err_append` mas para strings com tamanho explicito
 * (URLs nao-NUL-terminated quando vem do payload IPC do chrome).
 * Faz sanitizacao minima: substitui `<`/`>` por `?` para impedir
 * que uma URL com HTML injetado quebre o parser com tags
 * nao-fechadas. */
static size_t err_append_raw(char *buf, size_t cap, size_t pos,
                             const char *s, size_t n) {
    if (!buf || !s) return pos;
    for (size_t i = 0; i < n && pos + 1u < cap; ++i) {
        char c = s[i];
        if (c == '<' || c == '>') c = '?';
        buf[pos++] = c;
    }
    return pos;
}

/* Etapa 3 b-polish++ (2026-05-03): monta + parseia + rasteriza um
 * frame de erro (pagina HTML nativa) em vez do stub azul opaco.
 * Inputs:
 *   nav      = nav_id corrente;
 *   url/u    = URL que falhou (pode ser NULL/0 se indeterminado);
 *   reason   = string curta como "fetch_status=404" ou "empty body".
 * Efeitos:
 *   1. Sobrescreve `g_current_doc` com o doc parseado da pagina de
 *      erro (h1 + 3x p);
 *   2. Emite EVENT_TITLE com `g_current_doc.title` (parser fallback
 *      escolhe a primeira h1 = "Pagina nao carregou");
 *   3. Emite EVENT_FRAME renderizando o doc na viewport atual;
 *   4. Atualiza estado (g_current_doc_valid, g_current_nav,
 *      g_scroll_y_px, g_content_height_px) para que scroll/BACK
 *      funcionem tambem na pagina de erro.
 * Retorna 0 em sucesso, -1 em falha fatal de IPC (caller fecha
 * pipe). Se o parse da propria HTML de erro falhar (bug no parser),
 * faz fallback para emit_stub_frame. */
static int emit_error_frame(uint32_t nav,
                             const uint8_t *url, uint16_t url_len,
                             const char *reason) {
    size_t pos = 0;
    /* Heading vira o title por fallback (primeira h1). */
    pos = err_append(g_error_html_buf, ENGINE_ERROR_HTML_CAP, pos,
                     "<h1>Pagina nao carregou</h1>");
    pos = err_append(g_error_html_buf, ENGINE_ERROR_HTML_CAP, pos,
                     "<p>Endereco: ");
    if (url && url_len > 0u) {
        pos = err_append_raw(g_error_html_buf, ENGINE_ERROR_HTML_CAP,
                             pos, (const char *)url, url_len);
    } else {
        pos = err_append(g_error_html_buf, ENGINE_ERROR_HTML_CAP, pos,
                         "(desconhecido)");
    }
    pos = err_append(g_error_html_buf, ENGINE_ERROR_HTML_CAP, pos,
                     "</p><p>Motivo: ");
    pos = err_append(g_error_html_buf, ENGINE_ERROR_HTML_CAP, pos,
                     reason ? reason : "falha nao especificada");
    pos = err_append(g_error_html_buf, ENGINE_ERROR_HTML_CAP, pos,
                     "</p><p>F5 recarrega, F6 volta, F7 avanca, Esc fecha.</p>");
    if (pos < ENGINE_ERROR_HTML_CAP) g_error_html_buf[pos] = '\0';

    int prc = capyhtml_parse(g_error_html_buf, (uint32_t)pos,
                              &g_current_doc,
                              (capyhtml_yield_fn)0, (void *)0);
    if (prc != 0) {
        /* Parser morreu no proprio HTML controlado: bug do parser.
         * Cai no stub azul para nao deixar o usuario sem feedback. */
        (void)emit_log(BROWSER_IPC_LOG_ERROR,
                       "[capybrowser] error-page parse FAILED");
        return (emit_stub_frame(nav) < 0) ? -1 : 0;
    }

    g_scroll_y_px = 0;
    /* Etapa 3 seção c (2026-05-03): doc novo invalida focus antigo. */
    g_focused_input_idx = -1;
    g_current_nav = nav;
    g_current_doc_valid = 1u;
    g_content_height_px = 0;

    /* Emite titulo antes do frame: handlers do chrome recebem
     * UPDATE_TITLE e UPDATE_STATUS juntos; se o frame chegar
     * primeiro, o usuario ve "Loading" na title bar por 1 tick. */
    (void)emit_title(g_current_doc.title);

    int32_t h = emit_real_frame_scrolled(nav, &g_current_doc, 0);
    if (h < 0) return -1;
    g_content_height_px = h;
    return 0;
}

/* Sequencia local de fetch IDs (independente do IPC header seq).
 * Usado como "seq" no payload do EVENT_FETCH_REQUEST e validado
 * no FETCH_RESPONSE para detectar respostas atrasadas. */
static uint32_t g_fetch_seq = 0;

/* Drena um payload do request pipe sem usa-lo. Forward decl. */
static int drain_payload(uint32_t payload_len);

/* slice 5d: aguarda o FETCH_RESPONSE correspondente a um
 * EVENT_FETCH_REQUEST emitido. Mantem o engine "vivo" durante a
 * espera respondendo a PING e tratando SHUTDOWN/CANCEL inline.
 *
 * Retorno:
 *   0  -> sucesso; *out_status, *out_body, *out_body_len preenchidos
 *         (out_body aponta para dentro de scratch_buf).
 *  -1  -> EOF/erro fatal de protocolo; caller deve abortar a nav.
 *  -2  -> CANCEL recebido para esta nav; caller deve emitir
 *         NAV_CANCELLED.
 *  Em SHUTDOWN, esta funcao chama capy_exit(0) diretamente. */
static int wait_for_fetch_response(uint32_t expected_seq,
                                   uint32_t expected_nav_id,
                                   uint8_t *scratch_buf,
                                   uint32_t scratch_cap,
                                   uint16_t *out_status,
                                   const uint8_t **out_body,
                                   uint32_t *out_body_len) {
    for (;;) {
        uint8_t hdr_buf[BROWSER_IPC_HEADER_SIZE];
        if (read_full(0, hdr_buf, sizeof(hdr_buf)) < 0) return -1;
        struct browser_ipc_header hdr;
        if (browser_ipc_header_decode(hdr_buf, sizeof(hdr_buf), &hdr)
            != BROWSER_IPC_OK) {
            return -1;
        }
        if (hdr.payload_len > scratch_cap) {
            /* Refuses oversized payload; drain and keep waiting --
             * the chrome shouldn't send anything that big, but if
             * it does we shouldn't block the navigation forever. */
            (void)drain_payload(hdr.payload_len);
            continue;
        }
        if (hdr.payload_len > 0u) {
            if (read_full(0, scratch_buf, hdr.payload_len) < 0) return -1;
        }
        switch (hdr.kind) {
            case BROWSER_IPC_FETCH_RESPONSE: {
                struct browser_ipc_fetch_response resp;
                if (browser_ipc_fetch_response_decode(scratch_buf,
                                                      hdr.payload_len,
                                                      &resp)
                    != BROWSER_IPC_OK) {
                    return -1;
                }
                if (resp.seq != expected_seq
                    || resp.nav_id != expected_nav_id) {
                    /* Stale response from a superseded nav; ignore
                     * and keep waiting for the matching one. */
                    continue;
                }
                *out_status = resp.status;
                *out_body = resp.body;
                *out_body_len = resp.body_len;
                return 0;
            }
            case BROWSER_IPC_PING: {
                /* Keep watchdog alive even mid-fetch. */
                if (hdr.payload_len >= 4u) {
                    (void)emit_pong(be_get_u32(scratch_buf));
                }
                continue;
            }
            case BROWSER_IPC_CANCEL:
                return -2;
            case BROWSER_IPC_SHUTDOWN:
                capy_exit(0);
            default:
                /* Other control kinds are out-of-band during fetch
                 * wait; drain (already done) and ignore. */
                continue;
        }
    }
}

/* Slice 5d: appends the title line "[capybrowser] parsed N nodes
 * title=T" to log_buf and emits it. Splits the existing inline
 * itoa from run_navigate into a helper so the new fetch flow can
 * call it on the freshly parsed body. */
static void emit_parsed_log(const struct capyhtml_document *doc) {
    char log_buf[96];
    int p = 0;
    const char *prefix = "[capybrowser] parsed ";
    for (int i = 0; prefix[i] && p < (int)sizeof(log_buf) - 1; i++) {
        log_buf[p++] = prefix[i];
    }
    int n = (int)doc->node_count;
    char digits[8];
    int dp = 0;
    if (n == 0) digits[dp++] = '0';
    while (n > 0 && dp < (int)sizeof(digits)) {
        digits[dp++] = (char)('0' + n % 10);
        n /= 10;
    }
    while (dp > 0 && p < (int)sizeof(log_buf) - 1) {
        log_buf[p++] = digits[--dp];
    }
    const char *suffix = " nodes title=";
    for (int i = 0; suffix[i] && p < (int)sizeof(log_buf) - 1; i++) {
        log_buf[p++] = suffix[i];
    }
    for (int i = 0; doc->title[i] && p < (int)sizeof(log_buf) - 1; i++) {
        log_buf[p++] = doc->title[i];
    }
    log_buf[p] = '\0';
    (void)emit_log(BROWSER_IPC_LOG_INFO, log_buf);
}

/* Sequencia completa de uma navegacao: started -> fetch_request ->
 * fetch_response -> parse -> progress -> frame -> ready.
 *
 * Slice 5d: o engine agora pede o conteudo ao chrome via
 * EVENT_FETCH_REQUEST e parseia o body recebido em FETCH_RESPONSE.
 * Antes da slice 5d, qualquer URL produzia a mesma pagina hardcoded;
 * agora o chrome resolve `file://capyos/<page>` via tabela embutida
 * (3 paginas) e qualquer outra URL resolve para o body 404. */
static int run_navigate(const uint8_t *payload, uint32_t payload_len) {
    if (payload_len < 2u) {
        return emit_nav_failed(++g_nav_id, "no_url");
    }
    uint16_t url_len = (uint16_t)(((uint16_t)payload[0] << 8)
                                | (uint16_t)payload[1]);
    if (2u + (uint32_t)url_len > payload_len) {
        return emit_nav_failed(++g_nav_id, "url_overflow");
    }
    const uint8_t *url = payload + 2u;
    uint32_t nav = ++g_nav_id;

    if (emit_nav_started(nav, url, url_len) < 0) return -1;
    if (emit_nav_progress(nav, BROWSER_IPC_STAGE_FETCH, 10u) < 0) return -1;

    /* slice 5d: send fetch request, then block on response. */
    uint32_t fseq = ++g_fetch_seq;
    if (emit_fetch_request(fseq, nav, url, url_len) < 0) return -1;

    /* F3.3g: usa o scratch global em .bss para caber bodies HTTP de
     * ate 1 MiB sem estourar a stack da tarefa ring 3. */
    uint16_t status = 0;
    const uint8_t *body = (const uint8_t *)0;
    uint32_t body_len = 0u;
    int wr = wait_for_fetch_response(fseq, nav, g_fetch_scratch,
                                     sizeof(g_fetch_scratch),
                                     &status, &body, &body_len);
    if (wr == -1) return -1;
    if (wr == -2) {
        /* CANCEL chegou durante o wait. */
        (void)emit_nav_cancelled(nav);
        return 0;
    }
    if (status != BROWSER_IPC_FETCH_OK) {
        /* Surface non-200 as NAV_FAILED with status as the reason
         * (single ASCII digit-string). The chrome shows it in the
         * status bar and the smoke harness can grep for it.
         *
         * Etapa 3 b-polish++ (2026-05-03): antes do NAV_FAILED,
         * emitimos EVENT_FRAME com a pagina de erro HTML real (via
         * emit_error_frame). Sem isso, o usuario so via o frame
         * antigo (de antes da nav) ou o plano de fundo azul -- sem
         * nenhuma pista do que deu errado. */
        char reason[32];
        const char *prefix = "fetch_status=";
        int p = 0;
        for (int i = 0; prefix[i] && p < (int)sizeof(reason) - 1; ++i) {
            reason[p++] = prefix[i];
        }
        uint16_t s = status;
        char digits[6]; int dp = 0;
        if (s == 0u) digits[dp++] = '0';
        while (s > 0u && dp < (int)sizeof(digits)) {
            digits[dp++] = (char)('0' + (s % 10u));
            s /= 10u;
        }
        while (dp > 0 && p < (int)sizeof(reason) - 1) {
            reason[p++] = digits[--dp];
        }
        reason[p] = '\0';
        (void)emit_error_frame(nav, url, url_len, reason);
        /* g_nav_from_history fica limpo para que um BACK subsequente
         * nao acidentalmente skip o push de historico do proximo
         * NAVIGATE bem-sucedido. */
        g_nav_from_history = 0u;
        return emit_nav_failed(nav, reason);
    }

    if (emit_nav_progress(nav, BROWSER_IPC_STAGE_PARSE, 60u) < 0) return -1;

    /* Slice 4-final + Etapa 3 (2026-05-02): o body do FETCH_RESPONSE
     * alimenta o parser. Diferentemente da slice 4-final, o doc
     * resultante e copiado para `g_current_doc` (.bss) para que
     * CLICK/SCROLL/RELOAD subsequentes possam ler a arvore sem um
     * novo fetch. Em caso de body vazio ou parse_failure, caimos para
     * o frame stub azul. */
    int have_doc = 0;
    if (body_len > 0u && body) {
        if (capyhtml_parse((const char *)body, body_len, &g_current_doc,
                           (capyhtml_yield_fn)0, (void *)0) == 0) {
            have_doc = 1;
            emit_parsed_log(&g_current_doc);
        } else {
            (void)emit_log(BROWSER_IPC_LOG_ERROR,
                           "[capybrowser] capyhtml_parse FAILED");
        }
    } else {
        (void)emit_log(BROWSER_IPC_LOG_WARN,
                       "[capybrowser] empty body");
    }

    if (emit_nav_progress(nav, BROWSER_IPC_STAGE_RENDER, 90u) < 0) return -1;
    /* Qualquer nova navegacao reseta o scroll para o topo -- o
     * usuario espera que clicar num link faca a pagina subir de
     * volta. Escrevemos zero ANTES do emit_real_frame* para que o
     * frame rasterizado ja saia na posicao certa. */
    g_scroll_y_px = 0;
    /* Etapa 3 seção c (2026-05-03): doc novo invalida focus antigo. */
    g_focused_input_idx = -1;
    if (have_doc) {
        int32_t h = emit_real_frame_scrolled(nav, &g_current_doc, 0);
        if (h < 0) {
            /* Falha de IPC e fatal; -1 propaga e o caller fecha o
             * pipe. Falhas de layout viram fallback abaixo. */
            return -1;
        }
        g_content_height_px = h;
        g_current_doc_valid = 1u;
        g_current_nav = nav;
        /* Grava URL atual para RELOAD e para herdar em CLICK. */
        uint16_t take_url = url_len;
        if ((size_t)take_url >= sizeof(g_current_url)) {
            take_url = (uint16_t)(sizeof(g_current_url) - 1u);
        }
        for (uint16_t i = 0; i < take_url; ++i) g_current_url[i] = (char)url[i];
        g_current_url[take_url] = '\0';
        g_current_url_len = take_url;

        /* Etapa 3 seção b-polish (2026-05-03): emite o titulo da
         * pagina para o chrome atualizar `window->title`. Mesmo com
         * titulo vazio o emit e no-op, preservando o titulo anterior. */
        (void)emit_title(g_current_doc.title);

        /* Etapa 3 secao a fetch+decode (2026-05-05): apos parse +
         * primeiro frame, dispara IMAGE_REQUESTs para cada `<img>`
         * sem cache hit. Cache invalidate_other_navs ja descartou
         * slots de navs antigas; respostas tardias da nav anterior
         * que possam estar no pipe sao descartadas pelo handler
         * (img_id stale). Re-frames vao sair conforme as
         * IMAGE_RESPONSEs chegarem. */
        engine_request_images_for_doc(&g_current_doc, nav);

        /* Historico: se esta navegacao veio de BACK/FORWARD, nao
         * re-push; senao trunca o "futuro" e adiciona ao topo. */
        if (g_nav_from_history) {
            g_nav_from_history = 0u;
        } else {
            uint16_t push_len = take_url;
            if (push_len >= CAPYBROWSER_HISTORY_URL_MAX) {
                push_len = (uint16_t)(CAPYBROWSER_HISTORY_URL_MAX - 1u);
            }
            /* Trunca qualquer "futuro" pos-index atual. */
            if (g_history_count > 0u) {
                g_history_count = g_history_index + 1u;
            }
            uint32_t slot;
            if (g_history_count < CAPYBROWSER_HISTORY_MAX) {
                /* Espaco disponivel: anexa no fim. */
                slot = g_history_count;
                g_history_count++;
                g_history_index = slot;
            } else {
                /* Ring cheio: shift-left para descartar o mais antigo. */
                for (uint32_t i = 1; i < CAPYBROWSER_HISTORY_MAX; ++i) {
                    for (uint32_t c = 0; c < CAPYBROWSER_HISTORY_URL_MAX; ++c) {
                        g_history[i - 1u][c] = g_history[i][c];
                    }
                    g_history_len[i - 1u] = g_history_len[i];
                }
                slot = CAPYBROWSER_HISTORY_MAX - 1u;
                g_history_index = slot;
            }
            for (uint16_t i = 0; i < push_len; ++i) {
                g_history[slot][i] = (char)url[i];
            }
            g_history[slot][push_len] = '\0';
            g_history_len[slot] = push_len;
        }
    } else {
        /* Etapa 3 b-polish++ (2026-05-03): substituimos o stub azul
         * por uma pagina de erro HTML real (emit_error_frame). A
         * diferenca do caso "status != OK" acima e o motivo exato:
         * aqui a resposta chegou com 200 mas o body e vazio ou o
         * parser nao aceitou o HTML. Mostramos isso explicitamente
         * ao usuario. `emit_error_frame` sobrescreve g_current_doc
         * com a pagina de erro parseada, entao scroll/BACK continuam
         * funcionando sem cair em estado stale. */
        const char *reason = (body_len == 0u || !body)
                             ? "resposta vazia do servidor"
                             : "HTML nao pode ser interpretado";
        if (emit_error_frame(nav, url, url_len, reason) < 0) return -1;
        g_nav_from_history = 0u;
    }
    if (emit_nav_ready(nav) < 0) return -1;
    return 0;
}

/* Drena um payload do request pipe sem usa-lo. */
static int drain_payload(uint32_t payload_len) {
    uint8_t scratch[256];
    while (payload_len > 0u) {
        size_t take = payload_len > sizeof(scratch) ? sizeof(scratch) : payload_len;
        if (read_full(0, scratch, take) < 0) return -1;
        payload_len -= (uint32_t)take;
    }
    return 0;
}

/* Etapa 3 seção c (2026-05-03): tipos de hit suportados pelo
 * `hit_test_doc` (substitui o antigo hit_test_doc_link). */
enum doc_hit_kind {
    DOC_HIT_NONE        = 0,
    DOC_HIT_LINK        = 1, /* link com href; out_href = URL */
    DOC_HIT_INPUT_TEXT  = 2, /* input text/password; out_node_idx valido */
    DOC_HIT_INPUT_SUBMIT= 3  /* input submit; out_node_idx + out_form_action */
};

struct doc_hit {
    enum doc_hit_kind kind;
    /* Para DOC_HIT_LINK + DOC_HIT_INPUT_SUBMIT: URL alvo (NUL-term,
     * borrowed pointer no g_current_doc). Para DOC_HIT_INPUT_TEXT,
     * NULL. */
    const char *href;
    uint16_t    href_len;
    /* Para DOC_HIT_INPUT_*: indice em g_current_doc.nodes[]. Para
     * outros, 0xFFFFu. */
    uint16_t    node_idx;
};

/* Etapa 3 seção b (2026-05-02), estendido seção c (2026-05-03):
 * hit-testing de CLICK contra o layout da pagina atual. Re-roda o
 * `capyhtml_layout` na viewport atual (pode ter sido resizada depois
 * da ultima navegacao) e procura por:
 *   - cmd CMD_TEXT com href nao-NULL (link) cuja bbox contem (hit_x,
 *     hit_y) -> DOC_HIT_LINK;
 *   - cmd CMD_INPUT cuja bbox contem o ponto -> DOC_HIT_INPUT_*
 *     conforme subtipo em reserved[0].
 * Coordenadas ja estao em espaco-do-doc depois do caller ter
 * adicionado g_scroll_y_px -- ou seja, mesma origem usada pelo
 * layout. Preenche `*out` com kind/href/node_idx; retorna 0 sempre
 * (miss == DOC_HIT_NONE em out->kind), -1 em erro fatal de layout. */
static int hit_test_doc(int32_t hit_x, int32_t hit_y,
                        struct doc_hit *out) {
    if (!out) return -1;
    out->kind = DOC_HIT_NONE;
    out->href = (const char *)0;
    out->href_len = 0u;
    out->node_idx = 0xFFFFu;
    if (!g_current_doc_valid) return 0;

    struct capyhtml_font_ops font;
    capyhtml_font_ops_default(&font);
    struct capyhtml_cmd cmds[CAPYHTML_RENDER_RECOMMENDED_CAPACITY];
    struct capyhtml_render_result rr;
    if (capyhtml_layout(&g_current_doc, &font, (int32_t)g_vw, cmds,
                        (uint16_t)CAPYHTML_RENDER_RECOMMENDED_CAPACITY,
                        &rr) != 0) {
        return -1;
    }

    /* Itera cmds em ordem. Usamos "half-open" [x, x+w) x [y, y+h+pad)
     * para capturar o clique na linha do underline em CMD_TEXT (que
     * fica em y + glyph_h*scale). Para CMD_INPUT, sem padding extra
     * porque a borda ja faz parte do bbox visivel. */
    for (uint16_t i = 0; i < rr.cmd_count; ++i) {
        const struct capyhtml_cmd *c = &cmds[i];
        int32_t x0 = c->x;
        int32_t x1 = c->x + c->w;
        int32_t y0 = c->y;
        int32_t y1 = c->y + c->h;
        if (c->kind == CAPYHTML_CMD_TEXT) {
            y1 += 1; /* absorve underline */
            if (!c->href || c->href[0] == '\0') continue;
            if (hit_x < x0 || hit_x >= x1) continue;
            if (hit_y < y0 || hit_y >= y1) continue;
            out->kind = DOC_HIT_LINK;
            out->href = c->href;
            uint16_t n = 0;
            while (c->href[n] != '\0' && n < 0xFFFFu) ++n;
            out->href_len = n;
            return 0;
        }
        if (c->kind == CAPYHTML_CMD_INPUT) {
            if (hit_x < x0 || hit_x >= x1) continue;
            if (hit_y < y0 || hit_y >= y1) continue;
            /* Etapa 3 seção c polish (2026-05-03): mascarar com
             * SUBTYPE_MASK e defensivo. Hoje hit_test_doc roda um
             * capyhtml_layout fresco (sem flag_focused setado), entao
             * reserved[0] == subtype. Mas se o futuro tiver hit-test
             * sobre os cmds ja pos-processados pela engine, esta
             * mascara mantem a comparacao correta. */
            uint8_t subtype = (uint8_t)(c->reserved[0]
                                          & CAPYHTML_INPUT_SUBTYPE_MASK);
            uint16_t n_idx = CAPYHTML_CMD_NODE_IDX(*c);
            out->node_idx = n_idx;
            if (subtype == CAPYHTML_INPUT_TYPE_SUBMIT) {
                out->kind = DOC_HIT_INPUT_SUBMIT;
                out->href = c->href; /* form action */
                if (c->href) {
                    uint16_t n = 0;
                    while (c->href[n] != '\0' && n < 0xFFFFu) ++n;
                    out->href_len = n;
                }
            } else {
                /* TEXT, PASSWORD: foca. Hidden nao chega aqui (parser
                 * descarta). */
                out->kind = DOC_HIT_INPUT_TEXT;
            }
            return 0;
        }
    }
    return 0;
}

/* Etapa 3 seção b (2026-05-02): procura o comprimento do "scheme +
 * host" em uma URL absoluta do formato `scheme://host[:port]/path`.
 * Retorna o offset do primeiro '/' depois do `scheme://` (ou o
 * comprimento total se nao houver path), ou 0 se a URL nao tiver
 * `://`. Usado para resolver hrefs relativos contra `g_current_url`.
 *
 * Exemplos:
 *   "http://example.com/foo"   -> 18 (aponta para o '/foo')
 *   "https://a.b/"             -> 12
 *   "file://capyos/welcome"    -> 14
 *   "example.com"              -> 0 (sem esquema)
 */
static uint16_t url_origin_len(const char *url, uint16_t url_len) {
    if (!url || url_len < 3u) return 0u;
    /* Acha o "://" */
    uint16_t scheme_end = 0;
    for (uint16_t i = 0; i + 2u < url_len; ++i) {
        if (url[i] == ':' && url[i + 1] == '/' && url[i + 2] == '/') {
            scheme_end = (uint16_t)(i + 3u);
            break;
        }
    }
    if (scheme_end == 0u) return 0u;
    /* Acha o primeiro '/' depois do scheme_end */
    for (uint16_t j = scheme_end; j < url_len; ++j) {
        if (url[j] == '/') return j;
    }
    /* Sem path: origem == URL inteira */
    return url_len;
}

/* Etapa 3 seção b (2026-05-02), refatorada seção c (2026-05-03):
 * resolve href contra g_current_url e escreve a URL final em
 * `g_request_payload` (offset 2..2+final_len). Concatena `extra`
 * (com seu prefixo, e.g. "?...") ao final da URL resolvida. Se
 * `extra` for NULL/extra_len=0, comporta-se como antes da seção c.
 *
 * Resolution:
 *   - href absoluta (contem "://"): passa direto;
 *   - href path-absolute ("/foo"): concat com origin de g_current_url;
 *   - href path-relative ("foo"): passa direto (chrome resolve no
 *     fallback resolver ou retorna 404).
 *
 * Retorna >= 0 com final_len escrito; -1 em erro (logado). */
static int resolve_href_into_payload(const char *href, uint16_t href_len,
                                      const char *extra, uint16_t extra_len,
                                      uint16_t *out_final_len) {
    if (!out_final_len) return -1;
    *out_final_len = 0u;
    if (!href || href_len == 0u) return -1;

    uint32_t body_off = 2u;
    uint16_t base_len = 0u;

    int has_scheme = 0;
    for (uint16_t i = 0; i + 2u < href_len; ++i) {
        if (href[i] == ':' && href[i + 1] == '/' && href[i + 2] == '/') {
            has_scheme = 1; break;
        }
    }

    if (has_scheme) {
        if ((uint32_t)href_len + body_off > (uint32_t)sizeof(g_request_payload)) {
            (void)emit_log(BROWSER_IPC_LOG_WARN,
                           "[capybrowser] href too long; ignored");
            return -1;
        }
        for (uint16_t i = 0; i < href_len; ++i) {
            g_request_payload[body_off + i] = (uint8_t)href[i];
        }
        base_len = href_len;
    } else if (href[0] == '/' && g_current_url_len > 0u) {
        uint16_t origin_len = url_origin_len(g_current_url,
                                              g_current_url_len);
        if (origin_len == 0u) {
            (void)emit_log(BROWSER_IPC_LOG_WARN,
                           "[capybrowser] relative href but no base origin");
            return -1;
        }
        uint32_t total = (uint32_t)origin_len + (uint32_t)href_len;
        if (total + body_off > (uint32_t)sizeof(g_request_payload)
            || total > 0xFFFFu) {
            (void)emit_log(BROWSER_IPC_LOG_WARN,
                           "[capybrowser] resolved href too long; ignored");
            return -1;
        }
        for (uint16_t i = 0; i < origin_len; ++i) {
            g_request_payload[body_off + i] = (uint8_t)g_current_url[i];
        }
        for (uint16_t i = 0; i < href_len; ++i) {
            g_request_payload[body_off + origin_len + i] = (uint8_t)href[i];
        }
        base_len = (uint16_t)total;
    } else {
        if ((uint32_t)href_len + body_off > (uint32_t)sizeof(g_request_payload)) {
            (void)emit_log(BROWSER_IPC_LOG_WARN,
                           "[capybrowser] href too long; ignored");
            return -1;
        }
        for (uint16_t i = 0; i < href_len; ++i) {
            g_request_payload[body_off + i] = (uint8_t)href[i];
        }
        base_len = href_len;
    }

    /* Append extra (e.g. "?k=v&...") se presente. */
    if (extra && extra_len > 0u) {
        uint32_t new_total = (uint32_t)base_len + (uint32_t)extra_len;
        if (new_total + body_off > (uint32_t)sizeof(g_request_payload)
            || new_total > 0xFFFFu) {
            (void)emit_log(BROWSER_IPC_LOG_WARN,
                           "[capybrowser] href+query too long; truncated");
            /* truncate extra to fit */
            uint32_t avail = (uint32_t)sizeof(g_request_payload) -
                             body_off - (uint32_t)base_len;
            if (avail > (uint32_t)extra_len) avail = (uint32_t)extra_len;
            extra_len = (uint16_t)avail;
            new_total = (uint32_t)base_len + (uint32_t)extra_len;
        }
        for (uint16_t i = 0; i < extra_len; ++i) {
            g_request_payload[body_off + base_len + i] = (uint8_t)extra[i];
        }
        base_len = (uint16_t)new_total;
    }

    *out_final_len = base_len;
    return 0;
}

/* Etapa 3 seção c polish (2026-05-03): unreserved chars segundo
 * RFC 3986 ALPHA / DIGIT / "-" / "." / "_" / "~". Helper interno do
 * encoder; isolado para que o teste host possa exercitar via grep
 * ou simulacao chamando build_form_query e checando que so esses
 * sobrevivem literais. */
static int form_query_is_unreserved(char ch) {
    if (ch >= 'a' && ch <= 'z') return 1;
    if (ch >= 'A' && ch <= 'Z') return 1;
    if (ch >= '0' && ch <= '9') return 1;
    if (ch == '-' || ch == '.' || ch == '_' || ch == '~') return 1;
    return 0;
}

/* Etapa 3 seção c polish (2026-05-03): emite "%XX" para um byte,
 * em uppercase (convencao web). Retorna numero de bytes escritos
 * (3 em sucesso, 0 se nao houver espaco para os 3 chars). */
static uint16_t form_query_emit_pct(uint8_t b, char *out, uint16_t pos,
                                     uint16_t cap) {
    static const char k_hex[] = "0123456789ABCDEF";
    if ((uint32_t)pos + 3u > (uint32_t)cap) return 0u;
    out[pos]     = '%';
    out[pos + 1] = k_hex[(b >> 4) & 0x0Fu];
    out[pos + 2] = k_hex[b & 0x0Fu];
    return 3u;
}

/* Etapa 3 seção c (2026-05-03), polish (2026-05-03): walk de
 * g_current_doc construindo query string a partir dos INPUTs do form
 * que contem `submit_node_idx`. Forma "?k1=v1&k2=v2".
 *
 * Encoding (compativel com `application/x-www-form-urlencoded`):
 *   - char unreserved RFC 3986 -> literal (a-z, A-Z, 0-9, '-._~').
 *   - espaco               -> '+'.
 *   - qualquer outro byte  -> '%XX' (uppercase hex). Esta versao
 *     trata bytes Unicode/multi-byte como bytes opacos -- cada byte
 *     do UTF-8 vira seu proprio %XX, o que e o que o servidor espera
 *     (decodifica byte stream antes de virar string). */
static uint16_t build_form_query(uint16_t submit_node_idx,
                                  char *out_buf, uint16_t out_cap) {
    if (!out_buf || out_cap == 0u) return 0u;
    if (!g_current_doc_valid) return 0u;
    if (submit_node_idx >= (uint16_t)g_current_doc.node_count) return 0u;

    /* Acha o FORM ancestral: caminha indices para tras ate o primeiro
     * TAG_FORM. Se nao houver FORM antes (input "free"), nao ha
     * query (caller decide o que fazer). */
    int32_t form_idx = -1;
    for (int32_t i = (int32_t)submit_node_idx; i >= 0; --i) {
        if (g_current_doc.nodes[i].type == CAPYHTML_NODE_TAG_FORM) {
            form_idx = i;
            break;
        }
    }
    if (form_idx < 0) return 0u;

    /* Itera de form_idx+1 ate o proximo FORM ou fim, coletando
     * INPUTs com `name` nao-vazio. Submit e ignorado (nao enviamos
     * o nome do botao no submit MVP -- alguns sites usam mas nao
     * e essencial). */
    if (out_cap < 1u) return 0u;
    uint16_t out_pos = 0u;
    out_buf[out_pos++] = '?';
    int first = 1;
    for (int32_t i = form_idx + 1;
         i < g_current_doc.node_count && out_pos < out_cap; ++i) {
        const struct capyhtml_node *n = &g_current_doc.nodes[i];
        if (n->type == CAPYHTML_NODE_TAG_FORM) break; /* outro form */
        if (n->type != CAPYHTML_NODE_TAG_INPUT) continue;
        if (n->bold == CAPYHTML_INPUT_TYPE_SUBMIT) continue;
        if (n->name[0] == '\0') continue;

        if (!first) {
            if (out_pos < out_cap) out_buf[out_pos++] = '&';
        }
        first = 0;

        /* name: encode segundo regra unreserved/+/%XX. */
        for (uint16_t k = 0;
             n->name[k] != '\0' && out_pos < out_cap; ++k) {
            char ch = n->name[k];
            if (form_query_is_unreserved(ch)) {
                out_buf[out_pos++] = ch;
            } else if (ch == ' ') {
                out_buf[out_pos++] = '+';
            } else {
                uint16_t w = form_query_emit_pct((uint8_t)ch, out_buf,
                                                  out_pos, out_cap);
                if (w == 0u) goto done;
                out_pos = (uint16_t)(out_pos + w);
            }
        }
        if (out_pos < out_cap) out_buf[out_pos++] = '=';
        /* value: idem. */
        for (uint16_t k = 0;
             n->text[k] != '\0' && out_pos < out_cap; ++k) {
            char ch = n->text[k];
            if (form_query_is_unreserved(ch)) {
                out_buf[out_pos++] = ch;
            } else if (ch == ' ') {
                out_buf[out_pos++] = '+';
            } else {
                uint16_t w = form_query_emit_pct((uint8_t)ch, out_buf,
                                                  out_pos, out_cap);
                if (w == 0u) goto done;
                out_pos = (uint16_t)(out_pos + w);
            }
        }
    }
done:

    /* Se nao adicionamos nenhum par, descarta o '?' inicial. */
    if (first) return 0u;
    return out_pos;
}

/* Scratch para query string em submit. 1 KiB cobre forms de ~10
 * campos com valores moderados. */
static char g_submit_query_buf[1024];

/* Etapa 3 seção c (2026-05-03): submit do form que contem `submit_idx`.
 * Constroi query string, resolve form action contra g_current_url, e
 * dispara NAVIGATE. */
static void run_submit(uint16_t submit_idx, const char *form_action,
                       uint16_t form_action_len) {
    if (!form_action || form_action_len == 0u) {
        (void)emit_log(BROWSER_IPC_LOG_WARN,
                       "[capybrowser] submit: no form action; ignored");
        return;
    }
    uint16_t qlen = build_form_query(submit_idx, g_submit_query_buf,
                                      (uint16_t)sizeof(g_submit_query_buf));
    uint16_t final_len = 0u;
    if (resolve_href_into_payload(form_action, form_action_len,
                                   g_submit_query_buf, qlen,
                                   &final_len) != 0) {
        return;
    }
    g_request_payload[0] = (uint8_t)((final_len >> 8) & 0xFFu);
    g_request_payload[1] = (uint8_t)(final_len & 0xFFu);
    (void)run_navigate(g_request_payload, 2u + (uint32_t)final_len);
}

/* Etapa 3 seção b (2026-05-02), estendido seção c (2026-05-03):
 * CLICK handler. Decodifica (x,y,button) do payload, faz hit-test
 * na arvore atual e dispatch:
 *   - DOC_HIT_LINK: NAVIGATE para o href
 *   - DOC_HIT_INPUT_TEXT: foca o input (atualiza g_focused_input_idx)
 *   - DOC_HIT_INPUT_SUBMIT: dispara submit do form
 *   - DOC_HIT_NONE: limpa foco (clique fora de input retira focus)
 * Botao != LMB e silenciosamente ignorado.
 */
static void run_click(const uint8_t *payload, uint32_t payload_len) {
    if (payload_len < 5u) return;
    uint16_t x = be_get_u16(&payload[0]);
    uint16_t y = be_get_u16(&payload[2]);
    uint8_t  button = payload[4];
    if (button != 1u) return; /* only LMB in this slice */

    int32_t doc_x = (int32_t)x;
    int32_t doc_y = (int32_t)y + g_scroll_y_px;

    struct doc_hit hit;
    if (hit_test_doc(doc_x, doc_y, &hit) != 0) return;

    switch (hit.kind) {
        case DOC_HIT_LINK: {
            uint16_t final_len = 0u;
            if (resolve_href_into_payload(hit.href, hit.href_len,
                                           (const char *)0, 0u,
                                           &final_len) != 0) return;
            g_request_payload[0] = (uint8_t)((final_len >> 8) & 0xFFu);
            g_request_payload[1] = (uint8_t)(final_len & 0xFFu);
            (void)run_navigate(g_request_payload,
                               2u + (uint32_t)final_len);
            break;
        }
        case DOC_HIT_INPUT_TEXT: {
            /* Foca o input. Re-emite frame para que o usuario veja
             * a borda destacada + caret (Etapa 3 seção c polish). */
            int32_t prev = g_focused_input_idx;
            g_focused_input_idx = (int32_t)hit.node_idx;
            if (prev != g_focused_input_idx && g_current_doc_valid) {
                int32_t h = emit_real_frame_scrolled(g_current_nav,
                                                      &g_current_doc,
                                                      g_scroll_y_px);
                if (h >= 0) g_content_height_px = h;
            }
            break;
        }
        case DOC_HIT_INPUT_SUBMIT:
            run_submit(hit.node_idx, hit.href, hit.href_len);
            break;
        case DOC_HIT_NONE:
        default: {
            /* Limpa foco em click fora de qualquer input; mantem-se
             * o scroll/url. Etapa 3 seção c polish (2026-05-03):
             * re-emit frame quando foco existia antes para limpar a
             * borda destacada do input que estava focado. Caso ja
             * estivesse sem foco, sem re-emit (nada mudou). */
            int32_t prev = g_focused_input_idx;
            g_focused_input_idx = -1;
            if (prev != -1 && g_current_doc_valid) {
                int32_t h = emit_real_frame_scrolled(g_current_nav,
                                                      &g_current_doc,
                                                      g_scroll_y_px);
                if (h >= 0) g_content_height_px = h;
            }
            break;
        }
    }
}

/* Etapa 3 seção e (2026-05-02): SCROLL handler. Atualiza
 * `g_scroll_y_px` (clampado em [0, max_scroll]) e re-rasteriza o
 * frame usando o doc atual. Sem doc valido, vira no-op (nada pra
 * rolar). `max_scroll` e a diferenca entre altura total do
 * conteudo e altura da viewport; se conteudo couber inteiro na
 * viewport, max_scroll = 0 e SCROLL nao muda nada. */
static void run_scroll(const uint8_t *payload, uint32_t payload_len) {
    if (payload_len < 4u) return;
    if (!g_current_doc_valid) return;
    int32_t delta = (int32_t)be_get_u32(payload);

    int32_t new_scroll = g_scroll_y_px + delta;
    if (new_scroll < 0) new_scroll = 0;
    int32_t max_scroll = g_content_height_px - (int32_t)g_vh;
    if (max_scroll < 0) max_scroll = 0;
    if (new_scroll > max_scroll) new_scroll = max_scroll;

    if (new_scroll == g_scroll_y_px) return; /* no movement */
    g_scroll_y_px = new_scroll;

    int32_t h = emit_real_frame_scrolled(g_current_nav, &g_current_doc,
                                          g_scroll_y_px);
    if (h >= 0) g_content_height_px = h;
}

/* Etapa 3 seção b (2026-05-02): RELOAD handler. Re-emite NAVIGATE
 * com a URL armazenada em `g_current_url`. No-op se nunca
 * navegamos (g_current_url_len == 0). */
static void run_reload(void) {
    if (g_current_url_len == 0u) return;
    uint32_t url_len_u32 = (uint32_t)g_current_url_len;
    if (url_len_u32 + 2u > (uint32_t)sizeof(g_request_payload)) return;
    g_request_payload[0] = (uint8_t)((g_current_url_len >> 8) & 0xFFu);
    g_request_payload[1] = (uint8_t)(g_current_url_len & 0xFFu);
    for (uint16_t i = 0; i < g_current_url_len; ++i) {
        g_request_payload[2u + i] = (uint8_t)g_current_url[i];
    }
    (void)run_navigate(g_request_payload, 2u + url_len_u32);
}

/* Etapa 3 seção b-polish (2026-05-03): re-navega para
 * `g_history[g_history_index - 1]` sem empurrar nova entrada. Seta
 * `g_nav_from_history = 1` para que `run_navigate` saiba que nao
 * deve empurrar a URL de novo no ring. No-op se ja estamos no
 * comeco (`g_history_index == 0`) ou se nunca navegamos
 * (`g_history_count == 0`). */
static void run_back(void) {
    if (g_history_count == 0u) return;
    if (g_history_index == 0u) return;
    uint32_t new_idx = g_history_index - 1u;
    uint16_t url_len = g_history_len[new_idx];
    if (url_len == 0u) return;
    if ((uint32_t)url_len + 2u > (uint32_t)sizeof(g_request_payload)) return;
    g_request_payload[0] = (uint8_t)((url_len >> 8) & 0xFFu);
    g_request_payload[1] = (uint8_t)(url_len & 0xFFu);
    for (uint16_t i = 0; i < url_len; ++i) {
        g_request_payload[2u + i] = (uint8_t)g_history[new_idx][i];
    }
    g_history_index = new_idx;
    g_nav_from_history = 1u;
    (void)run_navigate(g_request_payload, 2u + (uint32_t)url_len);
}

/* Etapa 3 seção c (2026-05-03): KEY handler. Decode keycode + mods
 * do payload (5 B BE: keycode u32 + mods u8) e roteia ao input
 * focado, se houver. Comportamento por keycode:
 *   - 0x08 / 0x7F (BS / DEL): remove o ultimo caractere do value
 *   - 0x09 (TAB): avanca foco para o proximo INPUT (sem wrap)
 *   - 0x0D / 0x0A (Enter): submit do form atual (se houver)
 *   - 32..126 (printable ASCII): append ao value (clamp em buffer)
 * Outros keycodes (setas, F-keys, etc.) sao ignorados aqui
 * (browser_app trata-los para hotkeys). Sem foco, KEY e no-op.
 *
 * Apos qualquer mutacao no value, re-emit do frame e re-emit do
 * doc atual para que o usuario veja o caractere aparecer/sumir.
 * O nav_id mantido (g_current_nav) -- frame e re-render do mesmo
 * doc, nao uma nova navegacao. */
static void run_key(const uint8_t *payload, uint32_t payload_len) {
    if (payload_len < 5u) return;
    if (g_focused_input_idx < 0) return;
    if (!g_current_doc_valid) return;
    if (g_focused_input_idx >= g_current_doc.node_count) {
        g_focused_input_idx = -1;
        return;
    }

    uint32_t keycode = be_get_u32(payload);
    /* uint8_t mods = payload[4]; -- nao usamos ainda */
    (void)payload[4];

    struct capyhtml_node *n = &g_current_doc.nodes[g_focused_input_idx];
    if (n->type != CAPYHTML_NODE_TAG_INPUT) {
        g_focused_input_idx = -1;
        return;
    }

    /* Submit nao recebe input de texto: redireciona Enter para
     * triggar submit; outros keys ignorados quando submit focado. */
    if (n->bold == CAPYHTML_INPUT_TYPE_SUBMIT) {
        if (keycode == 0x0Du || keycode == 0x0Au) {
            run_submit((uint16_t)g_focused_input_idx,
                       n->href[0] ? n->href : (const char *)0,
                       n->href[0] ? (uint16_t)cap_strlen_local(n->href) : 0u);
        }
        return;
    }

    int mutated = 0;

    if (keycode == 0x0Du || keycode == 0x0Au) {
        /* Enter: tenta submit do form ancestral. O hit_test_doc nao
         * passou pelo SUBMIT, entao precisamos achar o form e seu
         * action manualmente. Simplificamos: usamos n->href ja
         * preenchido pelo layout? Nao, n->href de um INPUT nao tem
         * action -- so o cmd carrega isso. Re-walk: do INPUT focado
         * pra tras ate FORM, le href dele. */
        const char *action = (const char *)0;
        uint16_t action_len = 0u;
        for (int32_t i = g_focused_input_idx; i >= 0; --i) {
            if (g_current_doc.nodes[i].type == CAPYHTML_NODE_TAG_FORM) {
                if (g_current_doc.nodes[i].href[0]) {
                    action = g_current_doc.nodes[i].href;
                    action_len = (uint16_t)cap_strlen_local(action);
                }
                break;
            }
        }
        if (action && action_len > 0u) {
            run_submit((uint16_t)g_focused_input_idx, action, action_len);
        }
        return;
    } else if (keycode == 0x08u || keycode == 0x7Fu) {
        /* Backspace: remove ultimo char. */
        size_t len = 0;
        while (n->text[len]) ++len;
        if (len > 0) {
            n->text[len - 1] = '\0';
            mutated = 1;
        }
    } else if (keycode == 0x09u) {
        /* Tab: proximo INPUT; foco perdido se nao houver. */
        int32_t next = -1;
        for (int32_t i = g_focused_input_idx + 1;
             i < g_current_doc.node_count; ++i) {
            if (g_current_doc.nodes[i].type == CAPYHTML_NODE_TAG_INPUT &&
                g_current_doc.nodes[i].bold !=
                    CAPYHTML_INPUT_TYPE_SUBMIT) {
                next = i; break;
            }
        }
        if (next >= 0) {
            g_focused_input_idx = next;
            mutated = 1; /* re-emit para refletir mudanca de foco */
        }
    } else if (keycode >= 0x20u && keycode < 0x7Fu) {
        /* Printable ASCII: append. Garante NUL ao final. */
        size_t len = 0;
        while (n->text[len] && len + 1 < sizeof(n->text)) ++len;
        if (len + 1 < sizeof(n->text)) {
            n->text[len] = (char)keycode;
            n->text[len + 1] = '\0';
            mutated = 1;
        }
    }
    /* Outros keycodes: silencioso. */

    if (mutated) {
        int32_t h = emit_real_frame_scrolled(g_current_nav,
                                              &g_current_doc,
                                              g_scroll_y_px);
        if (h >= 0) g_content_height_px = h;
    }
}

/* Etapa 3 seção b-polish (2026-05-03): simetrico a run_back. */
static void run_forward(void) {
    if (g_history_count == 0u) return;
    if (g_history_index + 1u >= g_history_count) return;
    uint32_t new_idx = g_history_index + 1u;
    uint16_t url_len = g_history_len[new_idx];
    if (url_len == 0u) return;
    if ((uint32_t)url_len + 2u > (uint32_t)sizeof(g_request_payload)) return;
    g_request_payload[0] = (uint8_t)((url_len >> 8) & 0xFFu);
    g_request_payload[1] = (uint8_t)(url_len & 0xFFu);
    for (uint16_t i = 0; i < url_len; ++i) {
        g_request_payload[2u + i] = (uint8_t)g_history[new_idx][i];
    }
    g_history_index = new_idx;
    g_nav_from_history = 1u;
    (void)run_navigate(g_request_payload, 2u + (uint32_t)url_len);
}

int main(int rank) {
    (void)rank;

    /* Etapa 3 secao a fetch+decode (2026-05-05): inicializa cache de
     * imagens. .bss ja zerou os bytes mas o init seta next_img_id=1
     * e zera contadores explicitamente para deixar o estado claro. */
    image_cache_init(&g_image_cache);

    /* Anuncia versao via EVENT_LOG (chrome encaminha ao klog). */
    (void)emit_log(BROWSER_IPC_LOG_INFO, "capybrowser engine v=0 online");
    (void)capy_write(2, k_log_started, sizeof(k_log_started) - 1u);

    for (;;) {
        uint8_t hdr_buf[BROWSER_IPC_HEADER_SIZE];
        if (read_full(0, hdr_buf, sizeof(hdr_buf)) < 0) {
            /* Pipe de request fechado: chrome saiu. Encerra limpo. */
            capy_exit(0);
        }
        struct browser_ipc_header hdr;
        int rc = browser_ipc_header_decode(hdr_buf, sizeof(hdr_buf), &hdr);
        if (rc != BROWSER_IPC_OK) {
            /* Frame invalido: drena e ignora. Em producao seria erro
             * fatal, mas estamos em stub e queremos resiliencia. */
            (void)emit_log(BROWSER_IPC_LOG_WARN, "bad ipc header");
            capy_exit(2);
        }
        if (!browser_ipc_kind_is_request(hdr.kind)) {
            /* Engine recebendo evento? protocolo violado. */
            (void)emit_log(BROWSER_IPC_LOG_ERROR, "non-request kind from chrome");
            (void)drain_payload(hdr.payload_len);
            continue;
        }
        if (hdr.payload_len > INPUT_PAYLOAD_BUF) {
            /* Recusa, drena. */
            (void)emit_log(BROWSER_IPC_LOG_WARN, "request payload too large");
            (void)drain_payload(hdr.payload_len);
            continue;
        }
        /* F3.3g: payload fica em `.bss` (g_request_payload) para caber
         * bodies HTTP reais de ate 1 MiB sem estourar stack ring 3. */
        uint8_t *payload = g_request_payload;
        if (hdr.payload_len > 0u) {
            if (read_full(0, payload, hdr.payload_len) < 0) {
                capy_exit(0);
            }
        }
        switch (hdr.kind) {
            case BROWSER_IPC_NAVIGATE:
                (void)run_navigate(payload, hdr.payload_len);
                break;
            case BROWSER_IPC_PING: {
                if (hdr.payload_len < 4u) break;
                uint32_t nonce = be_get_u32(payload);
                (void)emit_pong(nonce);
                break;
            }
            case BROWSER_IPC_CANCEL:
                (void)emit_nav_cancelled(g_nav_id);
                break;
            case BROWSER_IPC_SHUTDOWN:
                capy_exit(0);
            case BROWSER_IPC_RESIZE: {
                /* 2026-05-02: payload BE = width:u16 + height:u16.
                 * Clampamos a [1..BROWSER_FRAME_MAX_*] para nunca
                 * estourar o framebuffer estatico. O proximo
                 * NAVIGATE / re-render usara as novas dims; nao
                 * re-rasterizamos automaticamente aqui porque o
                 * engine nao guarda a doc parseada (a chrome envia
                 * RESIZE antes de uma navegacao para que o primeiro
                 * frame ja venha no tamanho correto). */
                if (hdr.payload_len < 4u) break;
                uint16_t w = be_get_u16(&payload[0]);
                uint16_t h = be_get_u16(&payload[2]);
                if (w == 0u) w = 1u;
                if (h == 0u) h = 1u;
                if (w > (uint16_t)BROWSER_FRAME_MAX_W) {
                    w = (uint16_t)BROWSER_FRAME_MAX_W;
                }
                if (h > (uint16_t)BROWSER_FRAME_MAX_H) {
                    h = (uint16_t)BROWSER_FRAME_MAX_H;
                }
                g_vw = w;
                g_vh = h;
                break;
            }
            case BROWSER_IPC_CLICK:
                run_click(payload, hdr.payload_len);
                break;
            case BROWSER_IPC_SCROLL:
                run_scroll(payload, hdr.payload_len);
                break;
            case BROWSER_IPC_RELOAD:
                run_reload();
                break;
            case BROWSER_IPC_BACK:
                run_back();
                break;
            case BROWSER_IPC_FORWARD:
                run_forward();
                break;
            case BROWSER_IPC_KEY:
                run_key(payload, hdr.payload_len);
                break;
            case BROWSER_IPC_IMAGE_RESPONSE:
                /* Etapa 3 secao a fetch+decode (2026-05-05): chrome
                 * decodificou a imagem e mandou os pixels. Atualiza
                 * cache + re-render para o usuario ver. */
                engine_handle_image_response(payload, hdr.payload_len);
                break;
            default:
                /* Stub: ignora silenciosamente. */
                break;
        }
    }
}
