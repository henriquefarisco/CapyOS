/*
 * userland/lib/capylibc-net/capy_net_error.c (F4 seção c parte 2/2)
 *
 * Single-process error slot for libcapy-net. Every entry point
 * resets it to CAPY_NET_OK before doing real work, then writes a
 * specific code on the failure path. `capy_net_last_error()`
 * exposes it. Threading-safe variant (TLS) lands with the broader
 * pthread integration; today libcapy-net is single-threaded.
 */

#include "capylibc-net/capy_net.h"

static capy_net_err_t g_last_error = CAPY_NET_OK;

void capy_net_internal_set_error(capy_net_err_t err);
void capy_net_internal_reset_error(void);

void capy_net_internal_set_error(capy_net_err_t err) {
  g_last_error = err;
}

void capy_net_internal_reset_error(void) {
  g_last_error = CAPY_NET_OK;
}

capy_net_err_t capy_net_last_error(void) {
  return g_last_error;
}
