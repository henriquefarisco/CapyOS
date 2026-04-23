#ifndef DRIVERS_IO_H
#define DRIVERS_IO_H
/* Platform-independent I/O port helpers for x86/x86_64.
 * The inb/outb/inw/outw instructions are identical in 32-bit and 64-bit
 * long mode, so this header works for both architectures. */
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static __attribute__((always_inline)) inline void outw(uint16_t port, uint16_t val) {
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

static __attribute__((always_inline)) inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void cli(void) { __asm__ __volatile__("cli"); }
static inline void sti(void) { __asm__ __volatile__("sti"); }
static inline void hlt(void) { __asm__ __volatile__("hlt"); }

static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif /* DRIVERS_IO_H */
