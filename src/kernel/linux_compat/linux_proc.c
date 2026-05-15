#include "kernel/linux_compat/linux_proc.h"

#include <stdint.h>
#include <stddef.h>

/* Mini-snprintf accumulator (same shape as in linux_cpuinfo.c).
 * Kept local to avoid pulling a kernel printf dependency into a
 * pure-formatter TU. */
struct writer {
    char *buf;
    size_t cap;
    size_t total;
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

static void w_finalise(struct writer *w) {
    if (w->cap > 0) {
        size_t term = w->total < (w->cap - 1) ? w->total : (w->cap - 1);
        w->buf[term] = '\0';
    }
}

/* ---------- /proc/meminfo ---------- */

/* Linux always shows kB-rounded down. */
static uint64_t bytes_to_kb(uint64_t b) {
    return b / 1024;
}

static void emit_kb_field(struct writer *w, const char *label,
                          uint64_t bytes) {
    w_str(w, label);
    /* Linux pads the label area to 16 chars and right-aligns the
     * number. For parser robustness we keep tabs which Linux also
     * accepts; some parsers want exact spacing. We use the spacing
     * that matches `cat /proc/meminfo` on Linux 6.x: label, ':',
     * spaces to column 16, number, ' kB\n'. */
    /* For simplicity and parser robustness, we use a single space
     * separator; major parsers (procps, glibc, Firefox) tolerate
     * collapsing whitespace. */
    w_byte(w, ' ');
    w_uint(w, bytes_to_kb(bytes));
    w_str(w, " kB\n");
}

size_t linux_proc_format_meminfo(const struct linux_proc_meminfo *m,
                                 char *buf, size_t buf_size) {
    struct writer w = {
        .buf = (buf && buf_size > 0) ? buf : NULL,
        .cap = (buf && buf_size > 0) ? buf_size : 0,
        .total = 0,
    };

    /* Defensive: NULL m -> emit zeros. Lets early boot callers
     * format something even before stats are collected. */
    uint64_t total     = m ? m->mem_total_bytes     : 0;
    uint64_t free_     = m ? m->mem_free_bytes      : 0;
    uint64_t available = m ? m->mem_available_bytes : 0;

    emit_kb_field(&w, "MemTotal:",     total);
    emit_kb_field(&w, "MemFree:",      free_);
    emit_kb_field(&w, "MemAvailable:", available);
    /* Linux always emits these. CapyOS has no buffers / cache /
     * swap, but parsers expect the labels. Zero is fine. */
    emit_kb_field(&w, "Buffers:",  0);
    emit_kb_field(&w, "Cached:",   0);
    emit_kb_field(&w, "SwapTotal:", 0);
    emit_kb_field(&w, "SwapFree:",  0);

    w_finalise(&w);
    return w.total;
}

/* ---------- /proc/<pid>/status ---------- */

static const char *state_letter(enum linux_proc_state s) {
    switch (s) {
        case LINUX_PROC_STATE_RUNNING:    return "R";
        case LINUX_PROC_STATE_SLEEPING:   return "S";
        case LINUX_PROC_STATE_DISK_SLEEP: return "D";
        case LINUX_PROC_STATE_ZOMBIE:     return "Z";
        case LINUX_PROC_STATE_STOPPED:    return "T";
        case LINUX_PROC_STATE_DEAD:       return "X";
    }
    return "?";
}

static const char *state_word(enum linux_proc_state s) {
    switch (s) {
        case LINUX_PROC_STATE_RUNNING:    return "running";
        case LINUX_PROC_STATE_SLEEPING:   return "sleeping";
        case LINUX_PROC_STATE_DISK_SLEEP: return "disk sleep";
        case LINUX_PROC_STATE_ZOMBIE:     return "zombie";
        case LINUX_PROC_STATE_STOPPED:    return "stopped";
        case LINUX_PROC_STATE_DEAD:       return "dead";
    }
    return "unknown";
}

size_t linux_proc_format_pid_status(const struct linux_proc_pid_status *s,
                                    char *buf, size_t buf_size) {
    struct writer w = {
        .buf = (buf && buf_size > 0) ? buf : NULL,
        .cap = (buf && buf_size > 0) ? buf_size : 0,
        .total = 0,
    };

    /* Sentinels for NULL input. */
    const char *name = (s && s->name) ? s->name : "unknown";
    enum linux_proc_state st = s ? s->state : LINUX_PROC_STATE_RUNNING;

    w_str(&w, "Name:\t"); w_str(&w, name); w_byte(&w, '\n');

    w_str(&w, "State:\t"); w_str(&w, state_letter(st));
    w_str(&w, " ("); w_str(&w, state_word(st)); w_str(&w, ")\n");

    /* CapyOS has no thread groups today, so Tgid == Pid. */
    uint32_t pid  = s ? s->pid  : 0;
    uint32_t ppid = s ? s->ppid : 0;
    uint32_t uid  = s ? s->uid  : 0;
    uint32_t gid  = s ? s->gid  : 0;
    uint32_t fds  = s ? s->fd_size : 0;
    uint64_t vsz  = s ? s->vm_size_bytes : 0;
    uint64_t rss  = s ? s->vm_rss_bytes  : 0;
    uint64_t peak = s ? s->vm_peak_bytes : 0;

    w_str(&w, "Tgid:\t"); w_uint(&w, pid);  w_byte(&w, '\n');
    w_str(&w, "Pid:\t");  w_uint(&w, pid);  w_byte(&w, '\n');
    w_str(&w, "PPid:\t"); w_uint(&w, ppid); w_byte(&w, '\n');

    /* Linux emits 4-tuple "real effective saved fs" for both Uid
     * and Gid. CapyOS does not differentiate yet, so we report
     * the same value four times. */
    w_str(&w, "Uid:\t"); w_uint(&w, uid); w_byte(&w, '\t');
    w_uint(&w, uid); w_byte(&w, '\t');
    w_uint(&w, uid); w_byte(&w, '\t');
    w_uint(&w, uid); w_byte(&w, '\n');

    w_str(&w, "Gid:\t"); w_uint(&w, gid); w_byte(&w, '\t');
    w_uint(&w, gid); w_byte(&w, '\t');
    w_uint(&w, gid); w_byte(&w, '\t');
    w_uint(&w, gid); w_byte(&w, '\n');

    w_str(&w, "FDSize:\t"); w_uint(&w, fds); w_byte(&w, '\n');
    w_str(&w, "VmPeak:\t"); w_uint(&w, bytes_to_kb(peak)); w_str(&w, " kB\n");
    w_str(&w, "VmSize:\t"); w_uint(&w, bytes_to_kb(vsz));  w_str(&w, " kB\n");
    w_str(&w, "VmRSS:\t");  w_uint(&w, bytes_to_kb(rss));  w_str(&w, " kB\n");

    w_finalise(&w);
    return w.total;
}

/* ---------- /proc/self/maps ---------- */

/* Hex digits, lowercase Linux. */
static char hex_digit(uint64_t v) {
    return (char)(v < 10 ? '0' + v : 'a' + (v - 10));
}

/* Emit a 0-padded hex number with `width` digits. Linux maps
 * does not pad addresses (it emits the natural width), so we
 * pass width=0 to mean "no padding". For dev fields width=2. */
static void w_hex(struct writer *w, uint64_t v, int width) {
    char tmp[16];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0) {
            tmp[n++] = hex_digit(v & 0xF);
            v >>= 4;
        }
    }
    while (n < width) tmp[n++] = '0';
    while (n > 0) w_byte(w, tmp[--n]);
}

static void emit_maps_entry(struct writer *w,
                            const struct linux_proc_maps_entry *e) {
    /* address range: <start>-<end> in lowercase hex */
    w_hex(w, e->start, 0); w_byte(w, '-'); w_hex(w, e->end, 0);
    w_byte(w, ' ');

    /* perms: rwxp */
    w_byte(w, e->perm_read   ? 'r' : '-');
    w_byte(w, e->perm_write  ? 'w' : '-');
    w_byte(w, e->perm_exec   ? 'x' : '-');
    w_byte(w, e->perm_shared ? 's' : 'p');
    w_byte(w, ' ');

    /* offset: 8 hex digits 0-padded */
    w_hex(w, e->offset, 8);
    w_byte(w, ' ');

    /* dev major:minor in hex 2-digit */
    w_hex(w, e->dev_major, 2); w_byte(w, ':'); w_hex(w, e->dev_minor, 2);
    w_byte(w, ' ');

    /* inode (decimal). */
    w_uint(w, e->inode);

    /* pathname column: Linux pads with spaces to column ~73 then
     * emits the path. Most parsers just grep for the path; we
     * use a single space separator (procfs parsers tolerate). */
    if (e->pathname && e->pathname[0]) {
        w_byte(w, ' '); w_byte(w, ' '); w_byte(w, ' ');
        w_str(w, e->pathname);
    }
    w_byte(w, '\n');
}

size_t linux_proc_format_maps(const struct linux_proc_maps_entry *entries,
                              size_t n,
                              char *buf, size_t buf_size) {
    if (n > 0 && !entries) return 0;
    struct writer w = {
        .buf = (buf && buf_size > 0) ? buf : NULL,
        .cap = (buf && buf_size > 0) ? buf_size : 0,
        .total = 0,
    };
    for (size_t i = 0; i < n; i++) {
        emit_maps_entry(&w, &entries[i]);
    }
    w_finalise(&w);
    return w.total;
}

/* ---------- /proc/self/cmdline ---------- */

size_t linux_proc_format_cmdline(const char *const *argv,
                                 char *buf, size_t buf_size) {
    struct writer w = {
        .buf = (buf && buf_size > 0) ? buf : NULL,
        .cap = (buf && buf_size > 0) ? buf_size : 0,
        .total = 0,
    };
    if (argv) {
        for (size_t i = 0; argv[i] != NULL; i++) {
            for (const char *p = argv[i]; *p; p++) w_byte(&w, *p);
            w_byte(&w, '\0');
        }
    }
    /* NB: Linux /proc/self/cmdline returns the raw byte stream
     * with embedded NULs. The trailing NUL of the last arg is the
     * file's natural EOF. We do NOT call w_finalise to avoid
     * over-writing the last byte; the caller is expected to use
     * `total` as the length. But host parsers that printf the buf
     * benefit from a stable terminator. We finalise so buf is
     * always C-string safe. */
    w_finalise(&w);
    return w.total;
}

/* ---------- /proc/self/exe ---------- */

size_t linux_proc_format_self_exe(const char *path,
                                  char *buf, size_t buf_size) {
    struct writer w = {
        .buf = (buf && buf_size > 0) ? buf : NULL,
        .cap = (buf && buf_size > 0) ? buf_size : 0,
        .total = 0,
    };
    const char *p = path ? path : "/unknown";
    while (*p) w_byte(&w, *p++);
    w_finalise(&w);
    return w.total;
}

/* ---------- /proc/version ---------- */

size_t linux_proc_format_version(const char *release,
                                 char *buf, size_t buf_size) {
    struct writer w = {
        .buf = (buf && buf_size > 0) ? buf : NULL,
        .cap = (buf && buf_size > 0) ? buf_size : 0,
        .total = 0,
    };
    /* Userland (musl, glibc, JS shell init, Chromium) checks for
     * the "Linux version " prefix; emit it verbatim then append
     * the release string and a Linux-style build description. */
    w_str(&w, "Linux version ");
    w_str(&w, release ? release : "6.5.0-capyos");
    w_str(&w, " (capyos@build) (clang) "
              "#1 SMP Mon Jan 1 00:00:00 UTC 2026 x86_64\n");
    w_finalise(&w);
    return w.total;
}

/* ---------- /proc/uptime ---------- */

/* Helper: emit "<seconds>.<two_hundredths>" from nanoseconds. */
static void emit_seconds_hundredths(struct writer *w, uint64_t ns) {
    uint64_t seconds   = ns / 1000000000ull;
    /* hundredths of a second from the remainder. */
    uint64_t remainder = ns % 1000000000ull;
    uint32_t hundredths = (uint32_t)(remainder / 10000000ull);
    w_uint(w, seconds);
    w_byte(w, '.');
    /* Always 2 digits, zero-padded. */
    if (hundredths < 10) w_byte(w, '0');
    w_uint(w, hundredths);
}

size_t linux_proc_format_uptime(const struct linux_proc_uptime *u,
                                char *buf, size_t buf_size) {
    struct writer w = {
        .buf = (buf && buf_size > 0) ? buf : NULL,
        .cap = (buf && buf_size > 0) ? buf_size : 0,
        .total = 0,
    };
    uint64_t up   = u ? u->uptime_ns : 0;
    uint64_t idle = u ? u->idle_ns   : 0;
    emit_seconds_hundredths(&w, up);
    w_byte(&w, ' ');
    emit_seconds_hundredths(&w, idle);
    w_byte(&w, '\n');
    w_finalise(&w);
    return w.total;
}

/* ---------- /proc/loadavg ---------- */

/* Helper: emit "<integer>.<two_hundredths>" from thousandths.
 * 1234 -> "1.23" (truncating; Linux emits floor(thousandths/10)). */
static void emit_load_decimal(struct writer *w, uint32_t thousandths) {
    uint32_t int_part   = thousandths / 1000;
    uint32_t hundredths = (thousandths % 1000) / 10;
    w_uint(w, int_part);
    w_byte(w, '.');
    if (hundredths < 10) w_byte(w, '0');
    w_uint(w, hundredths);
}

size_t linux_proc_format_loadavg(const struct linux_proc_loadavg *l,
                                 char *buf, size_t buf_size) {
    struct writer w = {
        .buf = (buf && buf_size > 0) ? buf : NULL,
        .cap = (buf && buf_size > 0) ? buf_size : 0,
        .total = 0,
    };
    uint32_t l1   = l ? l->load1_thousandths  : 0;
    uint32_t l5   = l ? l->load5_thousandths  : 0;
    uint32_t l15  = l ? l->load15_thousandths : 0;
    uint32_t run  = l ? l->running_tasks      : 0;
    uint32_t tot  = l ? l->total_tasks        : 0;
    uint32_t last = l ? l->last_pid           : 0;
    emit_load_decimal(&w, l1);
    w_byte(&w, ' ');
    emit_load_decimal(&w, l5);
    w_byte(&w, ' ');
    emit_load_decimal(&w, l15);
    w_byte(&w, ' ');
    w_uint(&w, run);
    w_byte(&w, '/');
    w_uint(&w, tot);
    w_byte(&w, ' ');
    w_uint(&w, last);
    w_byte(&w, '\n');
    w_finalise(&w);
    return w.total;
}

void linux_proc_reset_for_tests(void) {
    /* No state. */
}
