#ifndef APPS_BROWSER_CHROME_H
#define APPS_BROWSER_CHROME_H

/*
 * Browser chrome (F3.3d).
 *
 * O chrome e o "frame" do navegador no compositor: URL bar, botoes
 * de navegacao, area de viewport, estado do widget e watchdog. Ele
 * NAO faz parse, render, fetch nem decode. Tudo isso vive no engine
 * ring 3 (capybrowser, F3.3b/c).
 *
 * Esta header expoe a API testavel do chrome: estado, dispatcher de
 * eventos vindos do engine e geracao de comandos para o engine. A
 * integracao com o compositor real e a aquisicao de pipes via
 * fork+exec ficam na camada `chrome_runtime.c` (a ser entregue em
 * F3.3d.next).
 */

#include "apps/browser_ipc.h"
#include "apps/browser_watchdog.h"
#include <stddef.h>
#include <stdint.h>

#define BROWSER_CHROME_TITLE_MAX  192u
#define BROWSER_CHROME_URL_MAX    768u
#define BROWSER_CHROME_REASON_MAX 192u

/* Estados visiveis ao usuario na barra de status do chrome. */
enum browser_chrome_status {
    BROWSER_CHROME_STATUS_IDLE   = 0, /* nenhuma navegacao ativa */
    BROWSER_CHROME_STATUS_LOADING = 1, /* NAV_STARTED recebido, sem READY/FAILED ainda */
    BROWSER_CHROME_STATUS_READY  = 2, /* NAV_READY recebido */
    BROWSER_CHROME_STATUS_FAILED = 3, /* NAV_FAILED recebido */
    BROWSER_CHROME_STATUS_CANCELLED = 4 /* NAV_CANCELLED recebido */
};

/* Acoes que o dispatcher pede ao caller apos consumir um evento.
 * Bitmask: o caller checa as flags individuais. */
enum browser_chrome_action {
    BROWSER_CHROME_ACTION_NONE         = 0,
    BROWSER_CHROME_ACTION_REPAINT_FRAME = (1u << 0), /* nova surface chegou; blitar */
    BROWSER_CHROME_ACTION_UPDATE_TITLE  = (1u << 1), /* janela precisa atualizar titulo */
    BROWSER_CHROME_ACTION_UPDATE_STATUS = (1u << 2), /* status bar mudou */
    BROWSER_CHROME_ACTION_LOG_FORWARD   = (1u << 3), /* EVENT_LOG: mandar pro klog */
    BROWSER_CHROME_ACTION_PROTOCOL_ERR  = (1u << 4)  /* engine violou o protocolo */
};

struct browser_chrome_frame_meta {
    uint32_t nav_id;
    uint16_t width;
    uint16_t height;
    uint32_t stride;
    /* Pointer para os pixels dentro do payload recebido (NAO copia).
     * O caller deve blitar antes de descartar o buffer original. */
    const uint8_t *pixels;
    uint32_t pixel_bytes;
};

struct browser_chrome {
    /* Estado da navegacao atual */
    enum browser_chrome_status status;
    uint32_t current_nav_id;
    uint32_t last_progress_stage;
    uint32_t last_progress_percent;

    /* URL e titulo */
    char current_url[BROWSER_CHROME_URL_MAX];
    uint16_t current_url_len;
    char current_title[BROWSER_CHROME_TITLE_MAX];
    uint16_t current_title_len;

    /* Ultimo erro (preenchido em FAILED) */
    char last_error_reason[BROWSER_CHROME_REASON_MAX];
    uint16_t last_error_reason_len;

    /* Frame mais recente */
    struct browser_chrome_frame_meta last_frame;

    /* Sequence numbers */
    uint32_t next_request_seq;

    /* Watchdog instance (chrome possui ownership) */
    struct browser_watchdog watchdog;

    /* Telemetria */
    uint32_t total_events_handled;
    uint32_t total_protocol_errors;
};

/* Inicializa o chrome em estado IDLE. now_ticks alimenta o watchdog. */
void browser_chrome_init(struct browser_chrome *c, uint64_t now_ticks);

/* Despacha um frame IPC vindo do engine. O caller ja decodificou o
 * header via browser_ipc_header_decode() e leu `payload_len` bytes
 * em `payload`. Retorna um bitmask de browser_chrome_action.
 *
 * Eventos com nav_id antigo (`< current_nav_id`) sao silenciosamente
 * descartados (PROGRESS/FRAME/READY/FAILED de navegacoes superadas).
 *
 * EVENT_PONG e roteado para o watchdog automaticamente. */
uint32_t browser_chrome_dispatch_event(struct browser_chrome *c,
                                       const struct browser_ipc_header *hdr,
                                       const uint8_t *payload,
                                       uint64_t now_ticks);

/* Aloca o proximo seq number monotonico para uma request enviada
 * ao engine. */
uint32_t browser_chrome_alloc_request_seq(struct browser_chrome *c);

/* Constroi o payload de NAVIGATE no buffer fornecido. Retorna o
 * numero de bytes escritos em out_buf (ou 0 em erro). out_buf deve
 * caber 2 + url_len bytes; recomenda-se BROWSER_CHROME_URL_MAX + 2. */
uint32_t browser_chrome_build_navigate_payload(const char *url,
                                               size_t url_len,
                                               uint8_t *out_buf,
                                               uint32_t out_size);

/* Marca que o caller iniciou uma navegacao para `url`. Atualiza
 * estado interno (status=LOADING, current_url, reseta titulo).
 * O nav_id real vem no EVENT_NAV_STARTED do engine. */
void browser_chrome_record_navigate_sent(struct browser_chrome *c,
                                         const char *url,
                                         size_t url_len);

#endif /* APPS_BROWSER_CHROME_H */
