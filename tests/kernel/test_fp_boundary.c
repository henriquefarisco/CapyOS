/* Native oracle for the production syscall/IRQ FP boundaries; no privileged code
 * is executed. Compare only architectural fields, not reserved bytes or the
 * implementation-dependent x87 instruction/data pointer metadata. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kernel/syscall.h"

_Static_assert(offsetof(struct syscall_frame, rip) == 72, "syscall RIP slot");
_Static_assert(offsetof(struct syscall_frame, rsp) == 80, "syscall RSP slot");
_Static_assert(sizeof(struct syscall_frame) == 96, "syscall frame size");

struct fx_image { _Alignas(16) unsigned char bytes[512]; };
struct dispatch_frame { struct fx_image incoming, poison, saved_user; };
typedef uint64_t (*boundary_fn)(void *);
extern uint64_t x64_syscall_dispatch_fp(void *);
extern uint64_t x64_exception_dispatch_fp(void *);
extern uint64_t fp_boundary_probe(const struct fx_image *, struct fx_image *,
                                  struct fx_image *, struct dispatch_frame *,
                                  boundary_fn);
_Static_assert(sizeof(struct fx_image) == 512, "FXSAVE image size");
_Static_assert(_Alignof(struct fx_image) >= 16, "FXSAVE alignment");

static void put16(unsigned char *p, unsigned value) {
    p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8);
}
static void put32(unsigned char *p, unsigned value) {
    put16(p, value); put16(p + 2, value >> 16);
}
static unsigned get16(const unsigned char *p) {
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}
static unsigned get32(const unsigned char *p) {
    return get16(p) | get16(p + 2) << 16;
}
static void seed(struct fx_image *image, unsigned case_id) {
    memset(image, 0, sizeof(*image));
    unsigned control = 0x037fu | (case_id % 4u) << 10;
    unsigned mxcsr = 0x1f80u | (case_id % 4u) << 13;
    if (case_id & 8u) { control &= ~1u; mxcsr &= ~0x80u; }
    put16(image->bytes, control);
    put16(image->bytes + 2, ((case_id % 8u) << 11) | (case_id & 16u ? 0x20u : 0));
    image->bytes[4] = 0xff; /* all eight x87 registers occupied */
    put32(image->bytes + 24, mxcsr | (case_id & 16u ? 0x20u : 0));
    for (unsigned reg = 0; reg < 8; ++reg) {
        unsigned char *st = image->bytes + 32 + reg * 16;
        for (unsigned byte = 0; byte < 7; ++byte)
            st[byte] = (unsigned char)(case_id * 13u + reg * 17u + byte);
        st[7] = 0x80; /* normalized finite 80-bit value */
        put16(st + 8, 0x3fffu);
    }
    for (unsigned byte = 160; byte < 416; ++byte)
        image->bytes[byte] = (unsigned char)(case_id * 71u + byte * 19u);
}
static int equal_architecture(const struct fx_image *a, const struct fx_image *b) {
    if (memcmp(a->bytes, b->bytes, 5) ||
        memcmp(a->bytes + 24, b->bytes + 24, 4) ||
        memcmp(a->bytes + 160, b->bytes + 160, 256)) return 0;
    for (unsigned reg = 0; reg < 8; ++reg)
        if (memcmp(a->bytes + 32 + reg * 16,
                   b->bytes + 32 + reg * 16, 10)) return 0;
    return 1;
}
int main(void) {
    unsigned failures = 0;
    const boundary_fn boundaries[] = {
        x64_syscall_dispatch_fp, x64_exception_dispatch_fp
    };
    for (unsigned boundary = 0; boundary < 2; ++boundary) {
        for (unsigned case_id = 0; case_id < 32; ++case_id) {
            struct fx_image input, before, after;
            struct dispatch_frame frame;
            seed(&input, case_id);
            memset(&frame, 0, sizeof(frame));
            seed(&frame.poison, case_id + 53u);
            uint64_t result = fp_boundary_probe(&input, &before, &after, &frame,
                                                boundaries[boundary]);
            int preserved = equal_architecture(&before, &after);
            if (boundary == 0 && !equal_architecture(&before, &frame.saved_user))
                preserved = 0;
            int neutral = get16(frame.incoming.bytes) == 0x037fu &&
                get16(frame.incoming.bytes + 2) == 0 && frame.incoming.bytes[4] == 0 &&
                get32(frame.incoming.bytes + 24) == 0x1f80u;
            if (!preserved || !neutral || result != UINT64_C(0x465053595343414c)) {
                printf("[fp-boundary] FAIL boundary=%u case=%u preserved=%d neutral=%d result=%llx\n",
                       boundary, case_id, preserved, neutral, (unsigned long long)result);
                ++failures;
            }
        }
    }
    if (failures) return 1;
    puts("[fp-boundary] 64 cases: x87, XMM0-15, MXCSR, neutral controls and return preserved");
    return 0;
}
