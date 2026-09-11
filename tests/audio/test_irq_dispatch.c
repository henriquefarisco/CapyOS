#include "arch/x86_64/irq_dispatch.h"
#include <assert.h>
#include <stdio.h>
static unsigned phase;
static void device(void) { assert(phase == 0); phase = 1; }
static void eoi(uint64_t vector) { assert(vector == 32 && phase == 1); phase = 2; }
static void schedule(void) { assert(phase == 2); phase = 3; }
int main(void) {
    x64_irq_dispatch_ordered(32, device, eoi, schedule);
    assert(phase == 3);
    phase = 0;
    x64_irq_dispatch_ordered(32, device, eoi, 0);
    assert(phase == 2);
    phase = 1;
    x64_irq_dispatch_ordered(32, 0, eoi, schedule);
    assert(phase == 3);
    puts("[irq] device -> EOI -> scheduling ok");
    return 0;
}
