#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT      0
#define SYS_READ      1
#define SYS_WRITE     2
#define SYS_OPEN      3
#define SYS_CLOSE     4
#define SYS_STAT      5
#define SYS_FSTAT     6
#define SYS_LSEEK     7
#define SYS_MMAP      8
#define SYS_MUNMAP    9
#define SYS_BRK       10
#define SYS_FORK      11
#define SYS_EXEC      12
#define SYS_WAIT      13
#define SYS_GETPID    14
#define SYS_GETPPID   15
#define SYS_KILL      16
#define SYS_YIELD     17
#define SYS_SLEEP     18
#define SYS_DUP       19
#define SYS_DUP2      20
#define SYS_PIPE      21
#define SYS_MKDIR     22
#define SYS_RMDIR     23
#define SYS_UNLINK    24
#define SYS_RENAME    25
#define SYS_GETCWD    26
#define SYS_CHDIR     27
#define SYS_SOCKET    28
#define SYS_BIND      29
#define SYS_LISTEN    30
#define SYS_ACCEPT    31
#define SYS_CONNECT   32
#define SYS_SEND      33
#define SYS_RECV      34
#define SYS_GETUID    35
#define SYS_GETGID    36
#define SYS_SETUID    37
#define SYS_SETGID    38
#define SYS_TIME      39
#define SYS_IOCTL     40
#define SYSCALL_COUNT 41

struct syscall_frame {
  uint64_t rax;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rdx;
  uint64_t r10;
  uint64_t r8;
  uint64_t r9;
  uint64_t rcx;
  uint64_t r11;
  uint64_t rip;
  uint64_t rsp;
  uint64_t rflags;
};

typedef int64_t (*syscall_handler_fn)(struct syscall_frame *frame);

void syscall_init(void);
void syscall_register(uint32_t num, syscall_handler_fn handler);
int64_t syscall_dispatch(struct syscall_frame *frame);

#endif /* KERNEL_SYSCALL_H */
