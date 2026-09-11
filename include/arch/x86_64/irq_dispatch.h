#ifndef X64_IRQ_DISPATCH_H
#define X64_IRQ_DISPATCH_H
#include <stdint.h>
/* A scheduler callback can suspend this stack. Release the interrupt
 * controller before it runs, or the next timer IRQ cannot be delivered. */
static inline void x64_irq_dispatch_ordered(uint64_t vector,
    void (*device)(void), void (*eoi)(uint64_t), void (*after_eoi)(void)) {
    if (device) device();
    eoi(vector);
    if (after_eoi) after_eoi();
}
#endif
