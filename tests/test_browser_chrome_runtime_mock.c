/*
 * tests/test_browser_chrome_runtime_mock.c (2026-05-03)
 *
 * Implementation of the shared mock pipes; see the matching `.h` for
 * rationale. Was inlined in test_browser_chrome_runtime.c until the
 * ~900-line layout rule demanded a split (2026-05-03 Etapa 3 seção
 * b-polish added enough send_* tests to push it over the limit).
 */
#include "test_browser_chrome_runtime_mock.h"

#include "apps/browser_chrome_runtime.h"
#include "apps/browser_ipc.h"

#include <string.h>

struct mock_pipe g_pipes[MOCK_PIPE_COUNT];

void mock_pipe_reset_all(void) {
    for (int i = 0; i < MOCK_PIPE_COUNT; ++i) {
        memset(&g_pipes[i], 0, sizeof(g_pipes[i]));
        g_pipes[i].read_open = 1;
        g_pipes[i].write_open = 1;
    }
}

int mock_pipe_write(int pipe_id, const void *buf, size_t len) {
    if (pipe_id < 0 || pipe_id >= MOCK_PIPE_COUNT) return -1;
    struct mock_pipe *p = &g_pipes[pipe_id];
    if (!p->write_open) return -1;
    if (!p->read_open) return -1; /* broken pipe */
    uint32_t space = MOCK_PIPE_BUF - p->count;
    if (space == 0u) return -1;
    uint32_t to = (uint32_t)len;
    if (to > space) to = space;
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < to; ++i) {
        p->buf[(p->w + i) % MOCK_PIPE_BUF] = src[i];
    }
    p->w = (p->w + to) % MOCK_PIPE_BUF;
    p->count += to;
    return (int)to;
}

int mock_pipe_read(int pipe_id, void *buf, size_t len) {
    if (pipe_id < 0 || pipe_id >= MOCK_PIPE_COUNT) return -1;
    struct mock_pipe *p = &g_pipes[pipe_id];
    if (!p->read_open) return -1;
    if (p->count == 0u) {
        if (!p->write_open) return 0; /* EOF */
        return -1; /* would-block */
    }
    uint32_t avail = p->count;
    if (avail > len) avail = (uint32_t)len;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < avail; ++i) {
        dst[i] = p->buf[(p->r + i) % MOCK_PIPE_BUF];
    }
    p->r = (p->r + avail) % MOCK_PIPE_BUF;
    p->count -= avail;
    return (int)avail;
}

void mock_pipe_install_ops(void) {
    chrome_runtime_set_pipe_ops(mock_pipe_write, mock_pipe_read);
}

void mock_be_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

void mock_be_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

void mock_inject_event(int response_pipe,
                       uint16_t kind,
                       uint32_t seq,
                       const uint8_t *payload,
                       uint32_t plen) {
    uint8_t hdr[BROWSER_IPC_HEADER_SIZE];
    mock_be_put_u16(&hdr[0], (uint16_t)BROWSER_IPC_MAGIC);
    mock_be_put_u16(&hdr[2], kind);
    mock_be_put_u32(&hdr[4], seq);
    mock_be_put_u32(&hdr[8], plen);
    (void)mock_pipe_write(response_pipe, hdr, sizeof(hdr));
    if (plen > 0u) {
        (void)mock_pipe_write(response_pipe, payload, plen);
    }
}
