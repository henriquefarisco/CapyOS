/*
 * Browser IPC codec (F3.3a).
 *
 * Encode/decode do header binario big-endian definido em
 * include/apps/browser_ipc.h. Implementacao deliberadamente livre de
 * dependencias de kernel/libc do desktop: usa apenas <stdint.h>.
 *
 * O mesmo arquivo e linkado tanto no chrome (kernel/desktop) quanto no
 * engine (userland ring 3, via libcapy-browser-ipc).
 */

#include "apps/browser_ipc.h"

static void be_write_u16(uint8_t *buf, uint16_t value) {
    buf[0] = (uint8_t)((value >> 8) & 0xFFu);
    buf[1] = (uint8_t)(value & 0xFFu);
}

static void be_write_u32(uint8_t *buf, uint32_t value) {
    buf[0] = (uint8_t)((value >> 24) & 0xFFu);
    buf[1] = (uint8_t)((value >> 16) & 0xFFu);
    buf[2] = (uint8_t)((value >> 8) & 0xFFu);
    buf[3] = (uint8_t)(value & 0xFFu);
}

static uint16_t be_read_u16(const uint8_t *buf) {
    return (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
}

static uint32_t be_read_u32(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24)
         | ((uint32_t)buf[1] << 16)
         | ((uint32_t)buf[2] << 8)
         |  (uint32_t)buf[3];
}

int browser_ipc_header_encode(const struct browser_ipc_header *hdr,
                              uint8_t *out,
                              uint32_t out_size) {
    if (!hdr || !out) {
        return BROWSER_IPC_ERR_INVAL;
    }
    if (out_size < BROWSER_IPC_HEADER_SIZE) {
        return BROWSER_IPC_ERR_SHORT;
    }
    be_write_u16(out + 0, hdr->magic);
    be_write_u16(out + 2, hdr->kind);
    be_write_u32(out + 4, hdr->seq);
    be_write_u32(out + 8, hdr->payload_len);
    return BROWSER_IPC_OK;
}

int browser_ipc_header_decode(const uint8_t *in,
                              uint32_t in_size,
                              struct browser_ipc_header *out_hdr) {
    if (!in || !out_hdr) {
        return BROWSER_IPC_ERR_INVAL;
    }
    if (in_size < BROWSER_IPC_HEADER_SIZE) {
        return BROWSER_IPC_ERR_SHORT;
    }
    uint16_t magic = be_read_u16(in + 0);
    uint16_t kind  = be_read_u16(in + 2);
    uint32_t seq   = be_read_u32(in + 4);
    uint32_t plen  = be_read_u32(in + 8);

    if (magic != (uint16_t)BROWSER_IPC_MAGIC) {
        return BROWSER_IPC_ERR_MAGIC;
    }
    if (!browser_ipc_kind_is_known(kind)) {
        return BROWSER_IPC_ERR_KIND;
    }
    if (plen > (uint32_t)BROWSER_IPC_MAX_PAYLOAD) {
        return BROWSER_IPC_ERR_PAYLOAD;
    }

    out_hdr->magic       = magic;
    out_hdr->kind        = kind;
    out_hdr->seq         = seq;
    out_hdr->payload_len = plen;
    return BROWSER_IPC_OK;
}

int browser_ipc_kind_is_request(uint16_t kind) {
    switch (kind) {
        case BROWSER_IPC_NAVIGATE:
        case BROWSER_IPC_CANCEL:
        case BROWSER_IPC_BACK:
        case BROWSER_IPC_FORWARD:
        case BROWSER_IPC_RELOAD:
        case BROWSER_IPC_SCROLL:
        case BROWSER_IPC_RESIZE:
        case BROWSER_IPC_CLICK:
        case BROWSER_IPC_KEY:
        case BROWSER_IPC_PING:
        case BROWSER_IPC_SHUTDOWN:
            return 1;
        default:
            return 0;
    }
}

int browser_ipc_kind_is_event(uint16_t kind) {
    switch (kind) {
        case BROWSER_IPC_EVENT_TITLE:
        case BROWSER_IPC_EVENT_NAV_STARTED:
        case BROWSER_IPC_EVENT_NAV_PROGRESS:
        case BROWSER_IPC_EVENT_NAV_READY:
        case BROWSER_IPC_EVENT_NAV_FAILED:
        case BROWSER_IPC_EVENT_NAV_CANCELLED:
        case BROWSER_IPC_EVENT_FRAME:
        case BROWSER_IPC_EVENT_CURSOR:
        case BROWSER_IPC_EVENT_PONG:
        case BROWSER_IPC_EVENT_LOG:
            return 1;
        default:
            return 0;
    }
}

int browser_ipc_kind_is_known(uint16_t kind) {
    return browser_ipc_kind_is_request(kind)
        || browser_ipc_kind_is_event(kind);
}

uint32_t browser_ipc_kind_min_payload(uint16_t kind) {
    switch (kind) {
        /* Sem payload */
        case BROWSER_IPC_CANCEL:
        case BROWSER_IPC_BACK:
        case BROWSER_IPC_FORWARD:
        case BROWSER_IPC_RELOAD:
        case BROWSER_IPC_SHUTDOWN:
            return 0;

        /* Tamanho fixo */
        case BROWSER_IPC_SCROLL:        return 4;  /* delta_y i32 */
        case BROWSER_IPC_RESIZE:        return 4;  /* w u16 + h u16 */
        case BROWSER_IPC_CLICK:         return 5;  /* x u16 + y u16 + button u8 */
        case BROWSER_IPC_KEY:           return 5;  /* keycode u32 + mods u8 */
        case BROWSER_IPC_PING:          return 4;  /* nonce u32 */
        case BROWSER_IPC_EVENT_NAV_READY:
        case BROWSER_IPC_EVENT_NAV_CANCELLED:
            return 4;                              /* nav_id u32 */
        case BROWSER_IPC_EVENT_NAV_PROGRESS:
            return 6;                              /* nav_id u32 + stage u8 + percent u8 */
        case BROWSER_IPC_EVENT_CURSOR:  return 1;  /* cursor u8 */
        case BROWSER_IPC_EVENT_PONG:    return 4;  /* nonce u32 */

        /* Variavel: minimo estatico apenas */
        case BROWSER_IPC_NAVIGATE:           return 2; /* url_len u16 (depois utf8) */
        case BROWSER_IPC_EVENT_TITLE:        return 2; /* title_len u16 */
        case BROWSER_IPC_EVENT_NAV_STARTED:  return 6; /* nav_id u32 + url_len u16 */
        case BROWSER_IPC_EVENT_NAV_FAILED:   return 6; /* nav_id u32 + reason_len u16 */
        case BROWSER_IPC_EVENT_LOG:          return 3; /* level u8 + msg_len u16 */
        case BROWSER_IPC_EVENT_FRAME:
            return 12; /* nav_id u32 + w u16 + h u16 + stride u32 (depois pixels) */

        default:
            return (uint32_t)-1;
    }
}
