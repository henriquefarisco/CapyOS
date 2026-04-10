#include "kernel/pipe.h"
#include <stddef.h>

static struct pipe g_pipes[PIPE_MAX];
static int g_pipe_initialized = 0;

static void pipe_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d;
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}

void pipe_system_init(void) {
  pipe_memset(g_pipes, 0, sizeof(g_pipes));
  g_pipe_initialized = 1;
}

int pipe_create(int fds[2]) {
  if (!g_pipe_initialized || !fds) return -1;
  for (int i = 0; i < PIPE_MAX; i++) {
    if (!g_pipes[i].read_open && !g_pipes[i].write_open) {
      pipe_memset(&g_pipes[i], 0, sizeof(struct pipe));
      g_pipes[i].id = i;
      g_pipes[i].read_open = 1;
      g_pipes[i].write_open = 1;
      fds[0] = i;       /* read end */
      fds[1] = i + 256;  /* write end (offset to distinguish) */
      return 0;
    }
  }
  return -1;
}

int pipe_read(int pipe_id, void *buf, size_t len) {
  if (pipe_id < 0 || pipe_id >= PIPE_MAX || !buf) return -1;
  struct pipe *p = &g_pipes[pipe_id];
  if (!p->read_open) return -1;
  if (p->count == 0) {
    if (!p->write_open) return 0; /* EOF */
    return -1; /* would block */
  }
  uint32_t avail = p->count;
  if (avail > len) avail = (uint32_t)len;
  uint8_t *dst = (uint8_t *)buf;
  for (uint32_t i = 0; i < avail; i++) {
    dst[i] = p->buffer[(p->read_pos + i) % PIPE_BUF_SIZE];
  }
  p->read_pos = (p->read_pos + avail) % PIPE_BUF_SIZE;
  p->count -= avail;
  return (int)avail;
}

int pipe_write(int pipe_id, const void *buf, size_t len) {
  if (pipe_id < 0 || pipe_id >= PIPE_MAX || !buf) return -1;
  struct pipe *p = &g_pipes[pipe_id];
  if (!p->write_open) return -1;
  if (!p->read_open) return -1; /* broken pipe */
  uint32_t space = PIPE_BUF_SIZE - p->count;
  if (space == 0) return -1; /* would block */
  uint32_t to_write = (uint32_t)len;
  if (to_write > space) to_write = space;
  const uint8_t *src = (const uint8_t *)buf;
  for (uint32_t i = 0; i < to_write; i++) {
    p->buffer[(p->write_pos + i) % PIPE_BUF_SIZE] = src[i];
  }
  p->write_pos = (p->write_pos + to_write) % PIPE_BUF_SIZE;
  p->count += to_write;
  return (int)to_write;
}

int pipe_close_read(int pipe_id) {
  if (pipe_id < 0 || pipe_id >= PIPE_MAX) return -1;
  g_pipes[pipe_id].read_open = 0;
  return 0;
}

int pipe_close_write(int pipe_id) {
  if (pipe_id < 0 || pipe_id >= PIPE_MAX) return -1;
  g_pipes[pipe_id].write_open = 0;
  return 0;
}
