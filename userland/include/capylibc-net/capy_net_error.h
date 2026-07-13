#ifndef CAPYLIBC_NET_CAPY_NET_ERROR_H
#define CAPYLIBC_NET_CAPY_NET_ERROR_H

/* Dependency-free libcapy-net error surface.  Consumers that only need to
 * report a transport failure should include this header instead of the full
 * HTTP/URL API, whose capy_url_parse symbol intentionally coexists with the
 * different parser exported by CapyBrowser core. */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CAPY_NET_OK            = 0,
  CAPY_NET_EINVAL        = -1,
  CAPY_NET_EPARSE        = -2,
  CAPY_NET_ESOCK         = -3,
  CAPY_NET_ECONNECT      = -4,
  CAPY_NET_ESEND         = -5,
  CAPY_NET_ERECV         = -6,
  CAPY_NET_EBUF          = -7,
  CAPY_NET_EDNS          = -8,
  CAPY_NET_EHTTP         = -9,
  CAPY_NET_EUNSUPPORTED  = -10
} capy_net_err_t;

capy_net_err_t capy_net_last_error(void);
const char *capy_net_strerror(capy_net_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* CAPYLIBC_NET_CAPY_NET_ERROR_H */
