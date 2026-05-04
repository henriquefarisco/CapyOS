#ifndef KERNEL_PIPE_H
#define KERNEL_PIPE_H

#include <stdint.h>
#include <stddef.h>

/* 2026-05-02: bump 4 KiB -> 64 KiB. O EVENT_FRAME do browser
 * (480x360 BGRA = 675 KiB) atravessa o pipe como ~170 chunks de
 * 4 KiB, cada um requerendo ida-e-volta cooperativa entre
 * engine e chrome. Em PIPE_BUF_SIZE=64 KiB, sao apenas ~11
 * round-trips, reduzindo ordem de magnitude na latencia. Custo
 * total .bss: 32 pipes * 64 KiB = 2 MiB (era 128 KiB). Aceitavel
 * para kernel image. PIPE_MAX permanece 32 para nao colidir com
 * o offset +256 usado em pipe_create para distinguir read/write
 * fds (PIPE_MAX < 256 obrigatorio). */
#define PIPE_BUF_SIZE (64u * 1024u)
#define PIPE_MAX 32

struct pipe {
  uint8_t buffer[PIPE_BUF_SIZE];
  uint32_t read_pos;
  uint32_t write_pos;
  uint32_t count;
  int read_open;
  int write_open;
  int id;
};

void pipe_system_init(void);
int pipe_create(int fds[2]);
int pipe_read(int pipe_id, void *buf, size_t len);
int pipe_write(int pipe_id, const void *buf, size_t len);
int pipe_close_read(int pipe_id);
int pipe_close_write(int pipe_id);

#endif /* KERNEL_PIPE_H */
