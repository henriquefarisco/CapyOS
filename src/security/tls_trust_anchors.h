#ifndef SECURITY_TLS_TRUST_ANCHORS_H
#define SECURITY_TLS_TRUST_ANCHORS_H

#include "bearssl.h"
#include <stddef.h>

const br_x509_trust_anchor *capyos_tls_trust_anchors(void);
size_t capyos_tls_trust_anchor_count(void);

#endif /* SECURITY_TLS_TRUST_ANCHORS_H */
