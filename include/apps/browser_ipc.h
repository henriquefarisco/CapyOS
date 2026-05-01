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
    BROWSER_IPC_EVENT_LOG           = 0x8A
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
