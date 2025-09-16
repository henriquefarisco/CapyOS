#include "gdt.h"

// extern asm (gdt_flush faz lgdt + far jump + carrega DS/ES/SS/FS/GS)
extern void gdt_flush(struct gdt_ptr* gp);

static struct gdt_entry gdt[3];
static struct gdt_ptr   gp;

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran){
    gdt[i].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[i].base_low  = (uint16_t)(base & 0xFFFF);
    gdt[i].base_mid  = (uint8_t)((base >> 16) & 0xFF);
    gdt[i].access    = access;
    gdt[i].gran      = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].base_high = (uint8_t)((base >> 24) & 0xFF);
}

void gdt_init(void){
    gp.limit = (uint16_t)(sizeof(gdt) - 1);
    gp.base  = (uint32_t)&gdt[0];

    // 0: null
    gdt_set(0, 0, 0, 0, 0);

    // 1: code 0x9A, base=0, limit=0xFFFFF, gran=4K|32bit (0xCF)
    gdt_set(1, 0x00000000, 0x000FFFFF, 0x9A, 0xCF);

    // 2: data 0x92, base=0, limit=0xFFFFF, gran=4K|32bit (0xCF)
    gdt_set(2, 0x00000000, 0x000FFFFF, 0x92, 0xCF);

    gdt_flush(&gp);
}
