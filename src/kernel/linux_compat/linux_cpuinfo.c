#include "kernel/linux_compat/linux_cpuinfo.h"

#include <stdint.h>
#include <stddef.h>

/* A tiny snprintf-style accumulator. We cannot use the kernel
 * `snprintf` because this TU is host-testable; we roll minimal
 * append logic here to keep the module self-contained. */

struct writer {
    char *buf;
    size_t cap;    /* destination capacity, 0 means size-query mode */
    size_t total;  /* total bytes that would be written */
};

static void w_byte(struct writer *w, char c) {
    if (w->cap > 0 && w->total + 1 < w->cap) {
        w->buf[w->total] = c;
    }
    w->total++;
}

static void w_str(struct writer *w, const char *s) {
    if (!s) return;
    while (*s) w_byte(w, *s++);
}

static void w_uint(struct writer *w, uint64_t v) {
    char tmp[24];
    size_t n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0) {
            tmp[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    while (n > 0) w_byte(w, tmp[--n]);
}

/* Flag token table. Order mirrors Linux 6.x output priority. */
struct flag_token {
    uint32_t bit;
    const char *name;
};
static const struct flag_token FLAG_TOKENS[] = {
    { LINUX_CPUINFO_FLAG_FPU,    "fpu"    },
    { LINUX_CPUINFO_FLAG_TSC,    "tsc"    },
    { LINUX_CPUINFO_FLAG_CMOV,   "cmov"   },
    { LINUX_CPUINFO_FLAG_MMX,    "mmx"    },
    { LINUX_CPUINFO_FLAG_SSE,    "sse"    },
    { LINUX_CPUINFO_FLAG_SSE2,   "sse2"   },
    { LINUX_CPUINFO_FLAG_SSE3,   "pni"    }, /* Linux uses "pni" token */
    { LINUX_CPUINFO_FLAG_SSSE3,  "ssse3"  },
    { LINUX_CPUINFO_FLAG_SSE4_1, "sse4_1" },
    { LINUX_CPUINFO_FLAG_SSE4_2, "sse4_2" },
    { LINUX_CPUINFO_FLAG_AVX,    "avx"    },
    { LINUX_CPUINFO_FLAG_AVX2,   "avx2"   },
    { LINUX_CPUINFO_FLAG_FMA,    "fma"    },
    { LINUX_CPUINFO_FLAG_POPCNT, "popcnt" },
    { LINUX_CPUINFO_FLAG_AES,    "aes"    },
    { LINUX_CPUINFO_FLAG_RDRAND, "rdrand" },
    { LINUX_CPUINFO_FLAG_RDSEED, "rdseed" },
    { LINUX_CPUINFO_FLAG_LM,     "lm"     },
};
#define FLAG_TOKENS_N (sizeof(FLAG_TOKENS)/sizeof(FLAG_TOKENS[0]))

static void emit_flags(struct writer *w, uint32_t flags) {
    int first = 1;
    for (size_t i = 0; i < FLAG_TOKENS_N; i++) {
        if ((flags & FLAG_TOKENS[i].bit) == 0) continue;
        if (!first) w_byte(w, ' ');
        w_str(w, FLAG_TOKENS[i].name);
        first = 0;
    }
}

static void emit_entry(struct writer *w,
                       const struct linux_cpuinfo_entry *e,
                       uint32_t total_cpus) {
    w_str(w, "processor\t: "); w_uint(w, e->processor_index); w_byte(w, '\n');
    w_str(w, "vendor_id\t: ");
    w_str(w, e->vendor_id ? e->vendor_id : "CapyOS");
    w_byte(w, '\n');
    w_str(w, "cpu family\t: "); w_uint(w, e->cpu_family); w_byte(w, '\n');
    w_str(w, "model\t\t: ");    w_uint(w, e->model);      w_byte(w, '\n');
    w_str(w, "model name\t: ");
    w_str(w, e->model_name ? e->model_name : "CapyOS generic");
    w_byte(w, '\n');
    w_str(w, "stepping\t: "); w_uint(w, e->stepping); w_byte(w, '\n');
    w_str(w, "cpu MHz\t\t: "); w_uint(w, e->cpu_mhz); w_byte(w, '\n');
    w_str(w, "cache size\t: "); w_uint(w, e->cache_size_kb); w_str(w, " KB\n");
    w_str(w, "physical id\t: 0\n");
    w_str(w, "siblings\t: "); w_uint(w, total_cpus); w_byte(w, '\n');
    w_str(w, "core id\t\t: "); w_uint(w, e->processor_index); w_byte(w, '\n');
    w_str(w, "cpu cores\t: "); w_uint(w, total_cpus); w_byte(w, '\n');
    w_str(w, "flags\t\t: "); emit_flags(w, e->flags); w_byte(w, '\n');
    /* bogomips = mhz * 2, standard Linux approximation. */
    w_str(w, "bogomips\t: "); w_uint(w, (uint64_t)e->cpu_mhz * 2); w_byte(w, '\n');
    w_byte(w, '\n'); /* trailing blank line between blocks */
}

size_t linux_cpuinfo_format(const struct linux_cpuinfo_entry *entries,
                            size_t n,
                            char *buf,
                            size_t buf_size) {
    if (n > 0 && !entries) return 0;
    struct writer w = {
        .buf = (buf && buf_size > 0) ? buf : NULL,
        .cap = (buf && buf_size > 0) ? buf_size : 0,
        .total = 0,
    };
    for (size_t i = 0; i < n; i++) {
        emit_entry(&w, &entries[i], (uint32_t)n);
    }
    /* Always NUL-terminate the written prefix. */
    if (w.cap > 0) {
        size_t term = w.total < (w.cap - 1) ? w.total : (w.cap - 1);
        w.buf[term] = '\0';
    }
    return w.total;
}

void linux_cpuinfo_reset_for_tests(void) {
    /* No state. */
}
