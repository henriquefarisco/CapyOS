#ifndef KERNEL_LINUX_COMPAT_LINUX_CPUINFO_H
#define KERNEL_LINUX_COMPAT_LINUX_CPUINFO_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI `/proc/cpuinfo` shim (S2.4).
 *
 * Firefox `gfx/` probes `/proc/cpuinfo` to detect SSE2/AVX/AVX2 at
 * runtime instead of compile-time when the build does not hardcode
 * `-march=...`. SpiderMonkey's `js::jit::CPUInfo` also reads this
 * on Linux before falling back to cpuid.
 *
 * We produce a buffer in the canonical Linux 6.x x86_64 format:
 *
 *   processor    : <cpu>
 *   vendor_id    : <vendor>
 *   cpu family   : <family>
 *   model        : <model>
 *   model name   : <model name>
 *   stepping     : <stepping>
 *   cpu MHz      : <mhz>
 *   cache size   : <cache_kb> KB
 *   physical id  : 0
 *   siblings     : <total_cpus>
 *   core id      : <cpu>
 *   cpu cores    : <total_cpus>
 *   flags        : <space-separated tokens>
 *   bogomips     : <mhz * 2>
 *
 *   (blank line, then next processor block)
 *
 * Consumers iterate through the blocks, so the order above must be
 * preserved. Fields that are genuinely absent (we do not know cache
 * size on firmware-minimal boot) can be zero -- Firefox tolerates
 * zeroes as long as `flags:` has the feature tokens it cares about.
 *
 * Layering: pure string formatter. The caller passes an array of
 * `linux_cpuinfo_entry` and a buffer. When CapyOS actually has MP,
 * the caller iterates real CPU descriptors; for now the kernel
 * wiring passes a single entry derived from cpuid.
 */

/* Flag bits. Ordered to match Linux `/proc/cpuinfo` output priority
 * (fpu, vme, de, ... then SSE family, then AVX family). Only the
 * subset Firefox/SpiderMonkey inspects is enumerated. Consumers
 * call `linux_cpuinfo_format` with a bitmask; the formatter emits
 * the corresponding tokens separated by single spaces. */
enum {
    LINUX_CPUINFO_FLAG_FPU    = 1u << 0,
    LINUX_CPUINFO_FLAG_TSC    = 1u << 1,
    LINUX_CPUINFO_FLAG_CMOV   = 1u << 2,
    LINUX_CPUINFO_FLAG_MMX    = 1u << 3,
    LINUX_CPUINFO_FLAG_SSE    = 1u << 4,
    LINUX_CPUINFO_FLAG_SSE2   = 1u << 5,
    LINUX_CPUINFO_FLAG_SSE3   = 1u << 6,
    LINUX_CPUINFO_FLAG_SSSE3  = 1u << 7,
    LINUX_CPUINFO_FLAG_SSE4_1 = 1u << 8,
    LINUX_CPUINFO_FLAG_SSE4_2 = 1u << 9,
    LINUX_CPUINFO_FLAG_AVX    = 1u << 10,
    LINUX_CPUINFO_FLAG_AVX2   = 1u << 11,
    LINUX_CPUINFO_FLAG_FMA    = 1u << 12,
    LINUX_CPUINFO_FLAG_POPCNT = 1u << 13,
    LINUX_CPUINFO_FLAG_AES    = 1u << 14,
    LINUX_CPUINFO_FLAG_RDRAND = 1u << 15,
    LINUX_CPUINFO_FLAG_RDSEED = 1u << 16,
    LINUX_CPUINFO_FLAG_LM     = 1u << 17,  /* long mode */
};

/* Descriptor for a single logical CPU. */
struct linux_cpuinfo_entry {
    uint32_t processor_index;   /* 0-based */
    const char *vendor_id;      /* "GenuineIntel", "AuthenticAMD", "CapyOS" */
    uint32_t cpu_family;
    uint32_t model;
    const char *model_name;
    uint32_t stepping;
    uint32_t cpu_mhz;
    uint32_t cache_size_kb;
    uint32_t flags;             /* bitwise OR of LINUX_CPUINFO_FLAG_* */
};

/* Format `/proc/cpuinfo` for `n` entries into `buf` (cap `buf_size`).
 * Returns the number of bytes written (excluding the final NUL, which
 * is always appended if `buf_size > 0`). On truncation returns the
 * number of bytes that would have been written if the buffer were
 * large enough (same contract as snprintf), so callers can
 * pre-allocate with a second call.
 *
 *   buf == NULL || buf_size == 0 -> returns required size (no write)
 *   entries == NULL && n > 0     -> returns 0 (caller bug, no write)
 */
size_t linux_cpuinfo_format(const struct linux_cpuinfo_entry *entries,
                            size_t n,
                            char *buf,
                            size_t buf_size);

/* Test-only: no state today, symmetric with other modules. */
void linux_cpuinfo_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_CPUINFO_H */
