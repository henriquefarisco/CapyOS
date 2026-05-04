#ifndef APPS_BROWSER_IPC_H
#define APPS_BROWSER_IPC_H

/*
 * Browser IPC Protocol (F3.3a)
 *
 * Contrato binario entre o browser chrome (compositor) e o browser
 * engine (capybrowser, processo ring 3 isolado). Especificacao
 * completa em docs/architecture/browser-ipc.md.
 *
 * Compativel com kernel (ring 0) e userland (ring 3). Usa apenas
 * <stdint.h> e <string.h> via codec.c.
 */

#include <stdint.h>

/* Magic + versao de wire format */
#define BROWSER_IPC_MAGIC          0xCB1Bu
#define BROWSER_IPC_VERSION        0u
#define BROWSER_IPC_MAX_PAYLOAD    (1u * 1024u * 1024u) /* 1 MiB */
#define BROWSER_IPC_HEADER_SIZE    12u

/* Sentinelas de retorno do codec */
#define BROWSER_IPC_OK              0
#define BROWSER_IPC_ERR_SHORT      -1   /* buffer/leitura curta */
#define BROWSER_IPC_ERR_MAGIC      -2   /* magic invalido (versao incompativel) */
#define BROWSER_IPC_ERR_KIND       -3   /* kind nao reconhecido */
#define BROWSER_IPC_ERR_PAYLOAD    -4   /* payload_len > MAX ou inconsistente */
#define BROWSER_IPC_ERR_INVAL      -5   /* parametro NULL ou invalido */

enum browser_ipc_kind {
    /* chrome -> engine (request/comando) */
    BROWSER_IPC_NAVIGATE        = 0x01,
    BROWSER_IPC_CANCEL          = 0x02,
    BROWSER_IPC_BACK            = 0x03,
    BROWSER_IPC_FORWARD         = 0x04,
    BROWSER_IPC_RELOAD          = 0x05,
    BROWSER_IPC_SCROLL          = 0x06,
    BROWSER_IPC_RESIZE          = 0x07,
    BROWSER_IPC_CLICK           = 0x08,
    BROWSER_IPC_KEY             = 0x09,
    BROWSER_IPC_PING            = 0x0A,
    BROWSER_IPC_SHUTDOWN        = 0x0B,
    /* F3.3c slice 5: chrome -> engine FETCH_RESPONSE.
     * Carries the body bytes the engine asked for via
     * EVENT_FETCH_REQUEST. Direction matches the existing
     * "chrome -> engine command" range so the codec direction
     * predicates keep working unchanged. */
    BROWSER_IPC_FETCH_RESPONSE  = 0x0C,

    /* engine -> chrome (event/resposta) */
    BROWSER_IPC_EVENT_TITLE         = 0x81,
    BROWSER_IPC_EVENT_NAV_STARTED   = 0x82,
    BROWSER_IPC_EVENT_NAV_PROGRESS  = 0x83,
    BROWSER_IPC_EVENT_NAV_READY     = 0x84,
    BROWSER_IPC_EVENT_NAV_FAILED    = 0x85,
    BROWSER_IPC_EVENT_NAV_CANCELLED = 0x86,
    BROWSER_IPC_EVENT_FRAME         = 0x87,
    BROWSER_IPC_EVENT_CURSOR        = 0x88,
    BROWSER_IPC_EVENT_PONG          = 0x89,
    BROWSER_IPC_EVENT_LOG           = 0x8A,
    /* F3.3c slice 5: engine -> chrome FETCH_REQUEST. The engine
     * has no direct kernel access; it asks the chrome to fetch
     * `url` and forward the body via FETCH_RESPONSE. The seq is
     * engine-assigned and pairs request/response. */
    BROWSER_IPC_EVENT_FETCH_REQUEST = 0x8B
};

/* Status codes returned in FETCH_RESPONSE.status. The numeric
 * values match common HTTP semantics so a future real backend can
 * map directly without translation. Status 0 means transport-level
 * failure (DNS, refused, timeout). */
enum browser_ipc_fetch_status {
    BROWSER_IPC_FETCH_TRANSPORT_ERR = 0,    /* DNS/refused/timeout/etc. */
    BROWSER_IPC_FETCH_OK            = 200,
    BROWSER_IPC_FETCH_NOT_FOUND     = 404,
    BROWSER_IPC_FETCH_FORBIDDEN     = 403,
    BROWSER_IPC_FETCH_UNAVAILABLE   = 503
};

/* HTTP-ish methods. Only GET is supported in slice 5; POST is
 * reserved for slice 5b (forms). */
enum browser_ipc_fetch_method {
    BROWSER_IPC_FETCH_GET  = 0,
    BROWSER_IPC_FETCH_POST = 1
};

/* Stages emitidos em NAV_PROGRESS */
enum browser_ipc_stage {
    BROWSER_IPC_STAGE_FETCH  = 0,
    BROWSER_IPC_STAGE_PARSE  = 1,
    BROWSER_IPC_STAGE_RENDER = 2
};

/* Cursores emitidos em EVENT_CURSOR */
enum browser_ipc_cursor {
    BROWSER_IPC_CURSOR_DEFAULT = 0,
    BROWSER_IPC_CURSOR_POINTER = 1,
    BROWSER_IPC_CURSOR_TEXT    = 2
};

/* Niveis de log emitidos em EVENT_LOG */
enum browser_ipc_log_level {
    BROWSER_IPC_LOG_DEBUG = 0,
    BROWSER_IPC_LOG_INFO  = 1,
    BROWSER_IPC_LOG_WARN  = 2,
    BROWSER_IPC_LOG_ERROR = 3
};

/*
 * Header binario sempre transmitido em big-endian. 12 bytes.
 *
 * Layout no wire (cada field e BE):
 *   offset 0: magic       (u16)
 *   offset 2: kind        (u16)
 *   offset 4: seq         (u32)
 *   offset 8: payload_len (u32)
 */
struct browser_ipc_header {
    uint16_t magic;
    uint16_t kind;
    uint32_t seq;
    uint32_t payload_len;
};

/*
 * Encode header em buffer de >= BROWSER_IPC_HEADER_SIZE bytes (BE).
 * Retorna BROWSER_IPC_OK ou BROWSER_IPC_ERR_INVAL.
 */
int browser_ipc_header_encode(const struct browser_ipc_header *hdr,
                              uint8_t *out,
                              uint32_t out_size);

/* === F3.3c slice 5: fetch payload helpers ============================
 *
 * Engine asks the chrome to fetch a URL; chrome answers with the
 * resolved body. The wire shapes are:
 *
 *   EVENT_FETCH_REQUEST (engine -> chrome) payload:
 *     [0..3]   seq u32 BE          -- engine-assigned, paired with response
 *     [4..7]   nav_id u32 BE       -- which navigation owns this fetch
 *     [8]      method u8           -- enum browser_ipc_fetch_method
 *     [9..10]  url_len u16 BE      -- length of url bytes
 *     [11..]   url utf8            -- url_len bytes
 *
 *   FETCH_RESPONSE (chrome -> engine) payload:
 *     [0..3]   seq u32 BE          -- echoes the request
 *     [4..7]   nav_id u32 BE       -- echoes the request
 *     [8..9]   status u16 BE       -- enum browser_ipc_fetch_status
 *     [10..11] ctype_len u16 BE    -- length of content_type bytes
 *     [12..15] body_len u32 BE     -- length of body bytes
 *     [16..]   ctype utf8          -- ctype_len bytes
 *     [..]     body bytes          -- body_len bytes
 */

struct browser_ipc_fetch_request {
    uint32_t seq;
    uint32_t nav_id;
    uint8_t  method;
    uint16_t url_len;
    const uint8_t *url;
};

struct browser_ipc_fetch_response {
    uint32_t seq;
    uint32_t nav_id;
    uint16_t status;
    uint16_t content_type_len;
    uint32_t body_len;
    const uint8_t *content_type;
    const uint8_t *body;
};

/* Encode the FETCH_REQUEST payload into `out`. Writes
 * 11 + url_len bytes; returns BROWSER_IPC_OK or
 * BROWSER_IPC_ERR_SHORT / BROWSER_IPC_ERR_INVAL. */
int browser_ipc_fetch_request_encode(const struct browser_ipc_fetch_request *req,
                                     uint8_t *out, uint32_t out_size,
                                     uint32_t *out_written);

/* Decode FETCH_REQUEST payload from `in`. Populates `*req` with
 * fields and a borrowed url pointer into the input buffer. */
int browser_ipc_fetch_request_decode(const uint8_t *in, uint32_t in_size,
                                     struct browser_ipc_fetch_request *req);

/* Encode FETCH_RESPONSE payload into `out`. Writes
 * 16 + content_type_len + body_len bytes. */
int browser_ipc_fetch_response_encode(const struct browser_ipc_fetch_response *resp,
                                      uint8_t *out, uint32_t out_size,
                                      uint32_t *out_written);

/* Decode FETCH_RESPONSE payload from `in`. */
int browser_ipc_fetch_response_decode(const uint8_t *in, uint32_t in_size,
                                      struct browser_ipc_fetch_response *resp);

/*
 * Decode header a partir de buffer (BE). Valida magic, kind conhecido
 * e payload_len <= BROWSER_IPC_MAX_PAYLOAD.
 *
 * Retorna BROWSER_IPC_OK em sucesso, ou:
 *   BROWSER_IPC_ERR_SHORT   se in_size < BROWSER_IPC_HEADER_SIZE
 *   BROWSER_IPC_ERR_MAGIC   se magic != BROWSER_IPC_MAGIC
 *   BROWSER_IPC_ERR_KIND    se kind nao esta no enum
 *   BROWSER_IPC_ERR_PAYLOAD se payload_len > BROWSER_IPC_MAX_PAYLOAD
 *   BROWSER_IPC_ERR_INVAL   se hdr ou in for NULL
 */
int browser_ipc_header_decode(const uint8_t *in,
                              uint32_t in_size,
                              struct browser_ipc_header *out_hdr);

/*
 * Helpers de validacao de payload por kind. Cada kind tem layout
 * proprio descrito no doc; aqui validamos apenas o tamanho minimo
 * estatico (campos de tamanho variavel sao validados pelo caller).
 *
 * Retorna 1 se valido, 0 se invalido.
 */
int browser_ipc_kind_is_request(uint16_t kind);
int browser_ipc_kind_is_event(uint16_t kind);
int browser_ipc_kind_is_known(uint16_t kind);

/*
 * Tamanho minimo estatico do payload por kind. Retorna 0 para kinds
 * sem payload, ou (uint32_t)-1 se kind desconhecido.
 *
 * Kinds com tamanho variavel (NAVIGATE, EVENT_TITLE, EVENT_NAV_FAILED,
 * EVENT_LOG, EVENT_FRAME) retornam o tamanho dos campos fixos do
 * payload; o caller deve validar o tail.
 */
uint32_t browser_ipc_kind_min_payload(uint16_t kind);

#endif /* APPS_BROWSER_IPC_H */
