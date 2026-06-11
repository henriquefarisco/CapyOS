#include "capylibc-net/capy_net.h"
#include "capylibc-tls/capy_tls.h"

extern void capy_net_internal_set_error(capy_net_err_t err);

capy_net_err_t capy_net_internal_tls_error_to_net(capy_tls_err_t err) {
  switch (err) {
    case CAPY_TLS_OK:
      return CAPY_NET_OK;
    case CAPY_TLS_EINVAL:
      return CAPY_NET_EINVAL;
    case CAPY_TLS_EUNSUPPORTED:
    case CAPY_TLS_ESTATE:
    default:
      return CAPY_NET_EUNSUPPORTED;
  }
}

int capy_net_internal_https_fail_closed(const struct capy_url_parts *url) {
  if (!url || !url->is_https || !url->host[0]) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }
  if (!capy_tls_is_supported()) {
    capy_net_internal_set_error(
        capy_net_internal_tls_error_to_net(CAPY_TLS_EUNSUPPORTED));
    return -1;
  }
  /* TLS is available (built with CAPYOS_TLS_USERLAND_HANDSHAKE): HTTPS may
   * proceed. capy_http_get wraps the connected socket via
   * capy_tls_connect_tcp from here. Returning 0 (instead of the historical
   * unconditional EUNSUPPORTED) is what finally satisfies Etapa 5 §8 #2 —
   * "HTTPS em libcapy-net deixa de retornar unsupported para caso válido".
   * When the flag is off, the branch above already failed closed.
   * (No error reset here: capy_http_get already reset at entry and owns
   * the error state from this point.) */
  return 0;
}
