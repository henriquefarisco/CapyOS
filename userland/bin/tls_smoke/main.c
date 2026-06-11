/*
 * userland/bin/tls_smoke/main.c — Etapa 5 / Slice 5.6 userland TLS
 * handshake smoke program.
 *
 * Runs in ring 3 and exercises the REAL userland TLS path. It is only
 * meaningful in a build with CAPYOS_TLS_USERLAND_HANDSHAKE=1, where
 * capy_tls_is_supported()==1 and capy_http_get() wraps the socket in
 * capy_tls_connect_tcp() (the slice 5.5 transport seam). It validates the
 * two Etapa 5 §8 acceptance criteria that need a live peer:
 *
 *   1. a VALID HTTPS GET returns a 2xx/3xx response
 *        -> "[smoke] tls-handshake ready"
 *      (criterion #2: HTTPS em libcapy-net deixa de retornar unsupported
 *       para caso válido);
 *   2. an INVALID-certificate HTTPS GET fails closed (no body delivered)
 *        -> "[smoke] tls-handshake bad-cert refused"
 *      (criteria #1/#3: erro mantém fail-closed; certificado inválido
 *       falha fechado).
 *
 * Both endpoints are compile-time configurable so the operator can point
 * them at a controlled server (see
 * docs/operations/etapa-5-external-validation-playbook.md §4.5). Override
 * with, e.g.:
 *   EXTRA_USERLAND_CFLAGS='-DCAPYOS_TLS_SMOKE_URL=\"https://lab/\" \
 *                          -DCAPYOS_TLS_SMOKE_BADCERT_URL=\"https://bad.lab/\"'
 *
 * The DHCP lease comes up asynchronously, so the valid GET is retried
 * (yielding + sleeping between attempts) until the network is ready or a
 * bounded number of attempts elapse; the harness --timeout is the outer
 * bound. The bad-cert probe runs only AFTER the valid GET succeeded, so a
 * "refused" marker can never be a false positive caused by the network
 * simply being down.
 *
 * Authoritative success signal is the EXIT CODE: the program exits 0 only
 * when the valid GET succeeded AND the invalid-cert GET failed closed; any
 * other outcome exits non-zero. The kernel (process_exit under
 * CAPYOS_TLS_HANDSHAKE_SMOKE) emits "[smoke] tls-handshake ready" on COM1 for
 * the exit-0 case, which is what the VMware harness reads. The capy_write
 * markers below are extra forensics on the debug console (port 0xE9): the
 * QEMU smokes capture 0xE9, but VMware does NOT, so ring-3 stdout cannot be
 * the VMware signal. Constraints mirror the other user binaries
 * (hello/exectarget/capysh): no host libc, all mutable state on the stack.
 * The ELF loader zeroes .bss (elf_loader.c), so the libcapy-net/-tls globals
 * start zero-init.
 */

#include <capylibc/capylibc.h>
#include <capylibc-net/capy_net.h>

#ifndef CAPYOS_TLS_SMOKE_URL
#define CAPYOS_TLS_SMOKE_URL "https://example.com/"
#endif
#ifndef CAPYOS_TLS_SMOKE_BADCERT_URL
#define CAPYOS_TLS_SMOKE_BADCERT_URL "https://expired.example.com/"
#endif

/* Bounded retry to absorb the async DHCP window: up to N attempts with a
 * yield + short sleep between each. The smoke harness --timeout is the
 * hard outer bound. */
#define TLS_SMOKE_MAX_ATTEMPTS 600u
#define TLS_SMOKE_SLEEP_TICKS  10u

static const char k_ready_marker[]   = "[smoke] tls-handshake ready\n";
static const char k_badcert_marker[] = "[smoke] tls-handshake bad-cert refused\n";

static size_t tls_smoke_cstr_len(const char *s) {
    size_t n = 0u;
    while (s[n]) n++;
    return n;
}

static void tls_smoke_emit(const char *marker) {
    capy_write(1, marker, tls_smoke_cstr_len(marker));
}

int main(int rank) {
    uint8_t body[2048];
    struct capy_http_response resp;
    unsigned attempt;
    int valid_ok = 0;
    int badcert_refused = 0;

    (void)rank;

    /* Criterion #2: a valid HTTPS GET must succeed once the network is up.
     * Retry across the async DHCP window; a transient network error (DNS /
     * connect / recv before the lease lands) is retried. */
    for (attempt = 0u; attempt < TLS_SMOKE_MAX_ATTEMPTS; ++attempt) {
        if (capy_http_get(CAPYOS_TLS_SMOKE_URL, body, sizeof(body), &resp) == 0 &&
            resp.status_code >= 200 && resp.status_code < 400) {
            tls_smoke_emit(k_ready_marker);
            valid_ok = 1;
            break;
        }
        capy_yield();
        capy_sleep(TLS_SMOKE_SLEEP_TICKS);
    }

    /* Criteria #1/#3: only probe the invalid-certificate endpoint once the
     * network is confirmed up (valid GET succeeded). The GET must fail
     * closed — capy_http_get returns != 0 and delivers no body — proving
     * the handshake rejected the untrusted/expired/mismatched chain rather
     * than silently proceeding. */
    if (valid_ok &&
        capy_http_get(CAPYOS_TLS_SMOKE_BADCERT_URL, body, sizeof(body), &resp) != 0) {
        tls_smoke_emit(k_badcert_marker);
        badcert_refused = 1;
    }

    /* Authoritative signal to the kernel smoke latch: exit 0 ONLY when the
     * valid GET succeeded AND the invalid-cert GET failed closed. The kernel
     * (process_exit under CAPYOS_TLS_HANDSHAKE_SMOKE) emits the COM1 marker
     * on this exit-0; any other outcome exits non-zero and no marker fires. */
    capy_exit((valid_ok && badcert_refused) ? 0 : 1);
}
