/*
 * src/apps/browser_chrome/fetch_resolver.c (F3.3c slice 5c)
 *
 * Built-in page table that lets the chrome respond to
 * EVENT_FETCH_REQUEST without networking. Pages live as static
 * const byte arrays so the resolver is allocation-free and
 * usable from both kernel-side chrome and host tests.
 *
 * URL scheme accepted: `file://capyos/<page>`. Anything else
 * returns the 404 fallback. Match is case-sensitive and exact
 * on the path (no trailing slash, no query string in this
 * slice -- those land in slice 5d once URL parsing matures).
 */

#include "apps/browser_chrome_fetch_resolver.h"
#include "apps/browser_ipc.h"

/* ---------- Built-in pages (UTF-8) ---------- */

static const uint8_t k_ctype_html[] = "text/html; charset=utf-8";
static const uint16_t k_ctype_html_len =
    (uint16_t)(sizeof(k_ctype_html) - 1u);

static const uint8_t k_welcome_body[] =
    "<h1>Bem-vindo ao CapyOS</h1>"
    "<p>Esta pagina foi servida pelo resolver embutido do "
    "chrome (file://capyos/welcome).</p>";

static const uint8_t k_about_body[] =
    "<h1>Sobre o CapyOS</h1>"
    "<p>Sistema operacional de bolso com browser isolado em "
    "ring 3.</p>";

static const uint8_t k_demo_body[] =
    "<h1>Demo</h1>"
    "<p>Uma pagina simples para validar parser + render do "
    "capybrowser.</p>";

/* Etapa F4 homepage (2026-05-03): pagina embarcada no estilo
 * Wikipedia que serve como homepage default offline. Usada em duas
 * situacoes:
 *   1. Default explicito (browser_homepage="file://capyos/wikipedia"
 *      no /system/config.ini).
 *   2. Fallback automatico do browser_app quando a navegacao para
 *      a homepage configurada (ex.: https://wikipedia.org) falha
 *      por falta de rede / DNS / TLS. Desta forma o usuario
 *      sempre ve conteudo ao abrir o browser, mesmo sem internet.
 *
 * O conteudo e tematico ao CapyOS (capivaras) e usa h1/h2/p/ul/li
 * que o capyhtml parser ja suporta. Tudo em ASCII para nao
 * depender do tratamento UTF-8 do raster. */
static const uint8_t k_wikipedia_body[] =
    "<title>Capivara - CapyPedia</title>"
    "<h1>Capivara</h1>"
    "<p>A <strong>capivara</strong> (Hydrochoerus hydrochaeris) e o "
    "maior roedor vivo do mundo. Nativa da America do Sul, vive em "
    "grupos proximos a corpos d'agua como rios, lagos e brejos.</p>"
    "<h2>Caracteristicas</h2>"
    "<p>Adultos pesam entre 35 e 66 kg e medem cerca de 130 cm de "
    "comprimento. Possuem pelagem castanho-avermelhada, patas "
    "parcialmente palmadas e podem permanecer submersas por ate "
    "cinco minutos.</p>"
    "<h2>Habitat e distribuicao</h2>"
    "<p>Encontradas em quase toda a America do Sul a leste dos Andes, "
    "as capivaras preferem savanas e florestas densas perto de agua. "
    "Sao excelentes nadadoras e usam a agua para escapar de "
    "predadores como oncas, pumas e jacares.</p>"
    "<h2>Comportamento</h2>"
    "<ul>"
    "<li>Vivem em grupos de 10 a 20 individuos liderados por um macho dominante.</li>"
    "<li>Sao herbivoras: comem gramineas, plantas aquaticas, cascas e frutos.</li>"
    "<li>Comunicam-se com assobios, latidos e estalos.</li>"
    "<li>Sao crepusculares: ativas ao amanhecer e ao entardecer.</li>"
    "</ul>"
    "<h2>Curiosidade: por que CapyOS?</h2>"
    "<p>O nome CapyOS homenageia a capivara, simbolo de tranquilidade "
    "e adaptabilidade. Assim como o roedor convive com diversas "
    "especies em um mesmo habitat, este sistema operacional busca "
    "harmonizar drivers, aplicativos e usuarios em um ambiente "
    "compacto.</p>"
    "<h2>Navegando no CapyBrowser</h2>"
    "<p>Voce esta vendo a pagina <em>file://capyos/wikipedia</em> "
    "embarcada no resolver do chrome. Para acessar a Wikipedia real "
    "digite <strong>https://wikipedia.org</strong> na barra de "
    "endereco e pressione Enter (requer rede ativa).</p>"
    "<p>Outras paginas internas: file://capyos/welcome, "
    "file://capyos/about, file://capyos/demo.</p>"
    "<hr>"
    "<p><em>Conteudo offline servido pelo CapyOS - sem dependencia "
    "de rede.</em></p>";

static const uint8_t k_404_body[] =
    "<h1>404 Not Found</h1>"
    "<p>A URL solicitada nao existe no resolver embutido.</p>";

struct page_entry {
    const char    *url;
    const uint8_t *body;
    uint32_t       body_len;
};

#define PAGE_ENTRY(url_str, body_arr) \
    { (url_str), (body_arr), (uint32_t)(sizeof(body_arr) - 1u) }

static const struct page_entry k_pages[] = {
    PAGE_ENTRY("file://capyos/welcome",   k_welcome_body),
    PAGE_ENTRY("file://capyos/about",     k_about_body),
    PAGE_ENTRY("file://capyos/demo",      k_demo_body),
    /* Etapa F4 homepage (2026-05-03): pagina rica usada como
     * homepage default offline + fallback automatico. */
    PAGE_ENTRY("file://capyos/wikipedia", k_wikipedia_body),
};

#define PAGE_COUNT ((uint32_t)(sizeof(k_pages) / sizeof(k_pages[0])))

/* ---------- Helpers (no libc) ---------- */

static int eq_n(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return 0;
        if (a[i] == '\0') return 0; /* premature NUL */
    }
    return 1;
}

static uint32_t cstrlen(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') ++n;
    return n;
}

/* ---------- Public API ---------- */

void browser_chrome_resolve_local(const char *url, uint16_t url_len,
                                  struct browser_chrome_fetch_result *out) {
    if (!out) return;
    /* Default: 404 fallback. We always set the result before any
     * early return so callers never see uninitialized fields. */
    out->status = BROWSER_IPC_FETCH_NOT_FOUND;
    out->content_type = k_ctype_html;
    out->content_type_len = k_ctype_html_len;
    out->body = k_404_body;
    out->body_len = (uint32_t)(sizeof(k_404_body) - 1u);

    if (!url || url_len == 0u) return;

    for (uint32_t i = 0; i < PAGE_COUNT; ++i) {
        const struct page_entry *e = &k_pages[i];
        uint32_t elen = cstrlen(e->url);
        if (elen != (uint32_t)url_len) continue;
        if (!eq_n(url, e->url, elen)) continue;
        out->status = BROWSER_IPC_FETCH_OK;
        out->content_type = k_ctype_html;
        out->content_type_len = k_ctype_html_len;
        out->body = e->body;
        out->body_len = e->body_len;
        return;
    }
    /* No match -> keep 404 default. */
}

uint32_t browser_chrome_resolver_page_count(void) {
    return PAGE_COUNT;
}

const char *browser_chrome_resolver_page_url(uint32_t i) {
    if (i >= PAGE_COUNT) return (const char *)0;
    return k_pages[i].url;
}
