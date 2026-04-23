#ifndef DEBUG_H
#define DEBUG_H
#include <stdint.h>

/* escreve 1 byte na porta 0xE9 (DebugCon) */
void dbg_put(char c);

static inline void dbg_hex(uint8_t v)
{
    const char h[]="0123456789ABCDEF";
    dbg_put(h[v>>4]);
    dbg_put(h[v&0xF]);
}

static inline void dbg_str(const char *s)
{
    while (*s) dbg_put(*s++);
}
#endif
