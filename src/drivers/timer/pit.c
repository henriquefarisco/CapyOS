#include <stdint.h>
#include "arch/x86/cpu/isr.h"     // registro de IRQ handler
#include "drivers/timer/pit.h"
#include "security/csprng.h"
#include "arch/x86/hw/io.h"      // garante protótipos de outb/inb (I/O ports)

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_INPUT_HZ 1193182u

static volatile uint64_t _ticks = 0;

static void pit_irq0_handler(/* seus params: int_no, err_code ou regs* */) {
    _ticks++;
    csprng_feed_entropy((uint32_t)_ticks);
    /* nada além disso por enquanto; EOI já deve ser enviado no dispatcher (PIC) */
}

uint64_t pit_ticks(void){ return _ticks; }

void pit_init(uint32_t hz){
    if (hz == 0) hz = 100;
    uint16_t div = (uint16_t)(PIT_INPUT_HZ / hz);

    outb(PIT_CMD, 0x34);            // ch0 | lobyte/hibyte | mode 2 | bin (0x34)
    outb(PIT_CH0, (uint8_t)(div & 0xFF));
    outb(PIT_CH0, (uint8_t)(div >> 8));

    // registre o handler em IRQ0:
    irq_install_handler(0, pit_irq0_handler);  // (função do seu ISR/IRQ)
}
