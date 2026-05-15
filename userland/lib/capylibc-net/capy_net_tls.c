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
  capy_net_internal_set_error(CAPY_NET_EUNSUPPORTED);
  return -1;
}
