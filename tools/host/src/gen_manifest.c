/* gen_manifest.c: Host-side tool to generate manifest.bin for CapyOS boot.
 *
 * Replaces tools/scripts/gen_manifest.py — no Python dependency.
 *
 * Usage: gen_manifest --kernel <path> --out <path> [--kernel-lba N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define BOOT_MANIFEST_MAGIC   0x5442494E
#define BOOT_MANIFEST_VERSION 1
#define BOOT_ENTRY_NORMAL     1

static uint32_t checksum32(const uint8_t *data, size_t len) {
    uint32_t s = 0;
    for (size_t i = 0; i < len; i++)
        s += data[i];
    return s;
}

static void put_le32(uint8_t *buf, uint32_t v) {
    buf[0] = (uint8_t)(v & 0xFF);
    buf[1] = (uint8_t)((v >> 8) & 0xFF);
    buf[2] = (uint8_t)((v >> 16) & 0xFF);
    buf[3] = (uint8_t)((v >> 24) & 0xFF);
}

int main(int argc, char **argv) {
    const char *kernel_path = NULL;
    const char *out_path = "build/manifest.bin";
    uint32_t kernel_lba = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--kernel") == 0 && i + 1 < argc) {
            kernel_path = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--kernel-lba") == 0 && i + 1 < argc) {
            kernel_lba = (uint32_t)atoi(argv[++i]);
        }
    }

    if (!kernel_path) {
        fprintf(stderr, "Usage: gen_manifest --kernel <path> --out <path> [--kernel-lba N]\n");
        return 1;
    }

    FILE *kf = fopen(kernel_path, "rb");
    if (!kf) {
        fprintf(stderr, "[err] Cannot open kernel: %s\n", kernel_path);
        return 1;
    }
    fseek(kf, 0, SEEK_END);
    long ksize = ftell(kf);
    fseek(kf, 0, SEEK_SET);
    if (ksize <= 0) {
        fprintf(stderr, "[err] Kernel file empty or unreadable: %s\n", kernel_path);
        fclose(kf);
        return 1;
    }
    uint8_t *kdata = (uint8_t *)malloc((size_t)ksize);
    if (!kdata) {
        fprintf(stderr, "[err] Out of memory\n");
        fclose(kf);
        return 1;
    }
    if (fread(kdata, 1, (size_t)ksize, kf) != (size_t)ksize) {
        fprintf(stderr, "[err] Failed to read kernel\n");
        free(kdata);
        fclose(kf);
        return 1;
    }
    fclose(kf);

    uint32_t ksecs = (uint32_t)(((size_t)ksize + 511) / 512);
    uint32_t ksum = checksum32(kdata, (size_t)ksize);
    free(kdata);

    uint8_t buf[512];
    memset(buf, 0, 512);
    put_le32(&buf[0], BOOT_MANIFEST_MAGIC);
    put_le32(&buf[4], BOOT_MANIFEST_VERSION);
    put_le32(&buf[8], 1); /* entry_count */
    /* entry 0 at offset 16 */
    put_le32(&buf[16], BOOT_ENTRY_NORMAL);
    put_le32(&buf[20], kernel_lba);
    put_le32(&buf[24], ksecs);
    put_le32(&buf[28], ksum);

    FILE *of = fopen(out_path, "wb");
    if (!of) {
        fprintf(stderr, "[err] Cannot write: %s\n", out_path);
        return 1;
    }
    fwrite(buf, 1, 512, of);
    fclose(of);

    printf("[ok] manifest gerado em %s (entries=1)\n", out_path);
    printf("     kernel lba=%u sec=%u checksum=0x%08X\n", kernel_lba, ksecs, ksum);
    return 0;
}
