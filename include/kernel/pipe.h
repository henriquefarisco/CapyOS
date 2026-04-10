#ifndef KERNEL_PIPE_H
#define KERNEL_PIPE_H

#include <stdint.h>
#include <stddef.h>

#define PIPE_BUF_SIZE 4096
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
