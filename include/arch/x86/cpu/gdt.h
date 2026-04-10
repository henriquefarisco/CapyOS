#ifndef GDT_H
#define GDT_H
#include <stdint.h>

struct __attribute__((packed)) gdt_entry {
    uint16_t limit_low;     // bits 0..15 do limite
    uint16_t base_low;      // base 0..15
    uint8_t  base_mid;      // base 16..23
    uint8_t  access;        // (0x9A código / 0x92 dados)
    uint8_t  gran;          // gran=4K|32bit (0xCF), limite 16..19
    uint8_t  base_high;     // base 24..31
};

struct __attribute__((packed)) gdt_ptr {
    uint16_t limit;         // tamanho - 1
    uint32_t base;          // endereço da tabela
};

void gdt_init(void);        // instala GDT e atualiza segmentos
#endif
