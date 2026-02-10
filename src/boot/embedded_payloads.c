/* embedded_payloads.c: exposes boot payload blobs generated at build time. */
#include "boot/boot_writer.h"
#include "boot_payloads.h"

struct boot_payload_set boot_embedded_payloads(void) {
    struct boot_payload_set set;
    set.stage1.data = stage1_image;
    set.stage1.size = stage1_image_len;
    set.stage2.data = stage2_image;
    set.stage2.size = stage2_image_len;
    set.kernel_main.data = noiros_image;
    set.kernel_main.size = noiros_image_len;
    set.kernel_recovery.data = noiros_image;
    set.kernel_recovery.size = noiros_image_len;
    return set;
}
