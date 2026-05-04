/*
 * tests/test_browser_chrome_runtime_mock.h (2026-05-03)
 *
 * Mock pipe infrastructure shared by the `test_browser_chrome_runtime`
 * suite. Was inlined in the main test TU until the file crossed the
 * 900-line layout limit; extracting the mock here brings it back under
 * the limit without duplicating the pipe emulation in every follow-up
 * test file.
 *
 * The mock implements `chrome_runtime_pipe_write_fn` / `_read_fn` on
 * top of an 8-slot ring of fixed 8 KiB buffers, with an open/close
 * flag per end so tests can simulate EOF and broken pipes. It is
 * internal-only: only tests link against it.
 */
#ifndef TEST_BROWSER_CHROME_RUNTIME_MOCK_H
#define TEST_BROWSER_CHROME_RUNTIME_MOCK_H

#include <stddef.h>
#include <stdint.h>

#define MOCK_PIPE_BUF 8192u
#define MOCK_PIPE_COUNT 8

struct mock_pipe {
    uint8_t  buf[MOCK_PIPE_BUF];
    uint32_t r;
    uint32_t w;
    uint32_t count;
    int      read_open;
    int      write_open;
};

extern struct mock_pipe g_pipes[MOCK_PIPE_COUNT];

void mock_pipe_reset_all(void);
int  mock_pipe_write(int pipe_id, const void *buf, size_t len);
int  mock_pipe_read(int pipe_id, void *buf, size_t len);
void mock_pipe_install_ops(void);

void mock_be_put_u16(uint8_t *p, uint16_t v);
void mock_be_put_u32(uint8_t *p, uint32_t v);

/* Writes a full IPC frame (header + optional payload) to the given
 * mock pipe so that a poll from the chrome can decode it as if the
 * engine had emitted it. `seq` is the header seq field; `kind` is
 * one of BROWSER_IPC_EVENT_*. */
void mock_inject_event(int response_pipe,
                       uint16_t kind,
                       uint32_t seq,
                       const uint8_t *payload,
                       uint32_t plen);

#endif /* TEST_BROWSER_CHROME_RUNTIME_MOCK_H */
