#include "util/debug.h"

void dbg_put(char c)
{
    __asm__ volatile("outb %0, $0xE9" :: "a"(c));
}