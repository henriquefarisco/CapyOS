#include "security/tls_hostname.h"
#include "security/tls_hostname_policy.h"

int tls_hostname_valid(const char *hostname) {
  return tls_hostname_policy_valid(hostname);
}
