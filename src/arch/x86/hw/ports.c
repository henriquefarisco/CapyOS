#include "arch/x86/hw/ports.h"
uint8_t port_byte_in(uint16_t port){
    uint8_t r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(port)); return r;
}
void port_byte_out(uint16_t port,uint8_t data){
    __asm__ volatile("outb %0,%1"::"a"(data),"Nd"(port));
}