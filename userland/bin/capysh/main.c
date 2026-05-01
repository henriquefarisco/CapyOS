/*
 * userland/bin/capysh/main.c -- M5 phase E.4: minimal interactive
 * capyland shell.
 *
 * Architecture:
 *   1. Print banner + prompt to fd 1.
 *   2. capy_read fd 0 in a loop, accumulating bytes into a 256-byte
 *      line buffer until '\n' OR backspace handling lands.
 *   3. Echo each typed byte back to fd 1 so the user sees input
 *      mirrored on the framebuffer console (the kernel TTY also
 *      echoes; capysh's echo serves the debug-console smoke path
 *      which doesn't see the TTY).
 *   4. When '\n' arrives, NUL-terminate, parse the first whitespace-
 *      separated word, and dispatch to a builtin.
 *
 * Builtins:
 *   help               list the supported commands
 *   echo <args...>     write the rest of the line back to stdout
 *   pid                print the calling process's pid
 *   ppid               print the calling process's parent pid
 *   exectarget         fork+exec("/bin/exectarget") + wait
 *   exit               capy_exit(0)
 *
 * Unknown commands print "[capysh] unknown: <name>".
 *
 * Constraints (same as hello / exectarget):
 *   - No globals beyond .rodata (the loader does not zero .bss).
 *   - No allocations.
 *   - No libc.
 *   - All state lives on the stack inside `main`. */

#include <capylibc/capylibc.h>

#define LINE_MAX_LEN 256

static const char k_banner[]      = "[capysh] CapyOS interactive shell -- type 'help'\n";
static const char k_prompt[]      = "capysh$ ";
static const char k_unknown_pre[] = "[capysh] unknown: ";
static const char k_newline[]     = "\n";
static const char k_help_text[] =
    "help               list builtins\n"
    "echo <args...>     write args back to stdout\n"
    "pid                print my pid\n"
    "ppid               print my parent pid\n"
    "exectarget         fork+exec /bin/exectarget then wait\n"
    "exit               exit the shell\n";
static const char k_exec_path[]      = "/bin/exectarget";
static const char k_fork_fail_marker[] = "[capysh] fork failed\n";
static const char k_exec_fail_marker[] = "[capysh] exec failed\n";
static const char k_pid_prefix[]       = "pid=";
static const char k_ppid_prefix[]      = "ppid=";

/* Small helpers; we cannot link the host libc. */
static size_t cstr_len(const char *s) {
    size_t n = 0;
    while (s && s[n] != 0) n++;
    return n;
}

/* Compare line[start..end) against literal cstr `lit`. */
static int line_word_eq(const char *line, int start, int end, const char *lit) {
    int li = 0;
    for (int i = start; i < end; i++) {
        if (lit[li] == 0) return 0;
        if (line[i] != lit[li]) return 0;
        li++;
    }
    return lit[li] == 0;
}

static void write_cstr(const char *s) {
    capy_write(1, s, cstr_len(s));
}

/* Convert an unsigned long to decimal ASCII; writes into `buf`,
 * returns the number of bytes written. `buf` must be >= 21 bytes. */
static int u64_to_dec(unsigned long v, char *buf) {
    char tmp[24];
    int n = 0;
    if (v == 0) { buf[0] = '0'; return 1; }
    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    }
    /* Reverse into buf. */
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

/* Find the first non-space index >= start. Returns end if none. */
static int skip_spaces(const char *line, int start, int end) {
    while (start < end && (line[start] == ' ' || line[start] == '\t')) {
        start++;
    }
    return start;
}

/* Find the first space-or-end index >= start. */
static int find_word_end(const char *line, int start, int end) {
    while (start < end && line[start] != ' ' && line[start] != '\t') {
        start++;
    }
    return start;
}

/* Returns 1 if the user wants to exit the shell, 0 to continue. */
static int dispatch_line(const char *line, int len) {
    int word_start = skip_spaces(line, 0, len);
    int word_end = find_word_end(line, word_start, len);
    if (word_start == word_end) return 0; /* empty line */

    if (line_word_eq(line, word_start, word_end, "exit")) {
        return 1;
    }
    if (line_word_eq(line, word_start, word_end, "help")) {
        write_cstr(k_help_text);
        return 0;
    }
    if (line_word_eq(line, word_start, word_end, "echo")) {
        int rest = skip_spaces(line, word_end, len);
        if (rest < len) capy_write(1, line + rest, (size_t)(len - rest));
        capy_write(1, k_newline, 1);
        return 0;
    }
    if (line_word_eq(line, word_start, word_end, "pid")) {
        char buf[24];
        int n = u64_to_dec((unsigned long)capy_getpid(), buf);
        write_cstr(k_pid_prefix);
        capy_write(1, buf, (size_t)n);
        capy_write(1, k_newline, 1);
        return 0;
    }
    if (line_word_eq(line, word_start, word_end, "ppid")) {
        char buf[24];
        int n = u64_to_dec((unsigned long)capy_getppid(), buf);
        write_cstr(k_ppid_prefix);
        capy_write(1, buf, (size_t)n);
        capy_write(1, k_newline, 1);
        return 0;
    }
    if (line_word_eq(line, word_start, word_end, "exectarget")) {
        int pid = capy_fork();
        if (pid < 0) {
            write_cstr(k_fork_fail_marker);
            return 0;
        }
        if (pid == 0) {
            /* Child: exec and never return. If exec fails, exit
             * with a marker so the parent's wait sees a non-zero
             * status and the smoke can flag it. */
            int rc = capy_exec(k_exec_path, (const char **)0);
            (void)rc;
            write_cstr(k_exec_fail_marker);
            capy_exit(1);
        }
        /* Parent: block until child finishes. */
        int status = 0;
        capy_wait((unsigned int)pid, &status);
        return 0;
    }

    /* Unknown command: echo back with a prefix. */
    write_cstr(k_unknown_pre);
    capy_write(1, line + word_start, (size_t)(word_end - word_start));
    capy_write(1, k_newline, 1);
    return 0;
}

int main(int rank) {
    (void)rank;

    write_cstr(k_banner);

    char line[LINE_MAX_LEN];
    int len = 0;

    write_cstr(k_prompt);
    for (;;) {
        char buf[32];
        long rd = capy_read(0, buf, sizeof(buf));
        if (rd <= 0) {
            /* sys_read on fd 0 should never return <=0 today
             * (it blocks until at least one byte arrives), but
             * be defensive: yield and retry to avoid spinning. */
            capy_yield();
            continue;
        }
        for (long i = 0; i < rd; i++) {
            char c = buf[i];
            if (c == '\b' || c == 0x7F) {
                /* Handle backspace: erase one char from the line
                 * buffer and emit the canonical BS-SP-BS sequence
                 * to the debug console so any consumer that draws
                 * from it stays consistent with what the user
                 * typed. */
                if (len > 0) {
                    len--;
                    capy_write(1, "\b \b", 3);
                }
                continue;
            }
            if (c == '\n' || c == '\r') {
                /* End of line: echo a newline and dispatch. */
                capy_write(1, k_newline, 1);
                int want_exit = dispatch_line(line, len);
                len = 0;
                if (want_exit) {
                    return 0;
                }
                write_cstr(k_prompt);
                continue;
            }
            /* Printable byte: echo + buffer. Bytes that overflow
             * the fixed buffer are silently dropped (the prompt
             * implies the line is too long; a future revision can
             * print a `[capysh] line too long` warning). */
            if (len < LINE_MAX_LEN - 1) {
                line[len++] = c;
                capy_write(1, &c, 1);
            }
        }
    }
}
