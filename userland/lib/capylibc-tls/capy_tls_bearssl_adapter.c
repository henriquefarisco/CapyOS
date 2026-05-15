#include "capy_tls_internal.h"

static uint32_t capy_tls_bearssl_adapter_fnv1a_byte(uint32_t hash,
    uint8_t byte) {
  hash ^= byte;
  hash *= 16777619u;
  return hash;
}

static uint32_t capy_tls_bearssl_adapter_fnv1a_cstr(uint32_t hash,
    const char *s) {
  while (*s) {
    hash = capy_tls_bearssl_adapter_fnv1a_byte(hash, (uint8_t)*s);
    s++;
  }
  return hash;
}

static uint32_t capy_tls_bearssl_adapter_fnv1a_uint(uint32_t hash,
    uint32_t value) {
  char digits[10];
  size_t i = 0;
  if (value == 0)
    return capy_tls_bearssl_adapter_fnv1a_byte(hash, (uint8_t)'0');
  while (value > 0 && i < sizeof(digits)) {
    digits[i] = (char)('0' + (value % 10u));
    value /= 10u;
    i++;
  }
  while (i > 0) {
    i--;
    hash = capy_tls_bearssl_adapter_fnv1a_byte(hash, (uint8_t)digits[i]);
  }
  return hash;
}

static uint32_t capy_tls_bearssl_adapter_fingerprint_compute(void) {
  uint32_t hash = 2166136261u;
  hash = capy_tls_bearssl_adapter_fnv1a_cstr(hash,
      "capy-tls-bearssl-adapter:metadata-only:fail-closed:v1:");
  hash = capy_tls_bearssl_adapter_fnv1a_uint(hash,
      CAPY_TLS_BEARSSL_ADAPTER_SCHEMA_VERSION);
  hash = capy_tls_bearssl_adapter_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_adapter_fnv1a_uint(hash,
      CAPY_TLS_BEARSSL_ADAPTER_ENGINE_BEARSSL_USERLAND);
  hash = capy_tls_bearssl_adapter_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_adapter_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FLAGS);
  hash = capy_tls_bearssl_adapter_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_adapter_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT);
  hash = capy_tls_bearssl_adapter_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_adapter_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT);
  hash = capy_tls_bearssl_adapter_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_adapter_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT);
  hash = capy_tls_bearssl_adapter_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_adapter_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT);
  hash = capy_tls_bearssl_adapter_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_adapter_fnv1a_uint(hash, 0u);
  hash = capy_tls_bearssl_adapter_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_adapter_fnv1a_uint(hash, 0u);
  hash = capy_tls_bearssl_adapter_fnv1a_byte(hash, (uint8_t)':');
  return hash;
}

static const struct capy_tls_bearssl_adapter_contract
    g_capy_tls_default_bearssl_adapter = {
  CAPY_TLS_BEARSSL_ADAPTER_SCHEMA_VERSION,
  CAPY_TLS_BEARSSL_ADAPTER_ENGINE_BEARSSL_USERLAND,
  CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FLAGS,
  CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT,
  CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT,
  CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FINGERPRINT,
  1,
  0,
  0
};

const struct capy_tls_bearssl_adapter_contract *
capy_tls_default_bearssl_adapter_contract(void) {
  return &g_capy_tls_default_bearssl_adapter;
}

uint32_t capy_tls_default_bearssl_adapter_schema_version(void) {
  return g_capy_tls_default_bearssl_adapter.schema_version;
}

uint32_t capy_tls_default_bearssl_adapter_engine_id(void) {
  return g_capy_tls_default_bearssl_adapter.engine_id;
}

uint32_t capy_tls_default_bearssl_adapter_flags(void) {
  return g_capy_tls_default_bearssl_adapter.flags;
}

uint32_t capy_tls_default_bearssl_adapter_fingerprint(void) {
  return g_capy_tls_default_bearssl_adapter.fingerprint;
}

int capy_tls_default_bearssl_adapter_consistent(void) {
  const struct capy_tls_bearssl_adapter_contract *adapter;
  adapter = capy_tls_default_bearssl_adapter_contract();
  if (!adapter) return 0;
  if (adapter->schema_version != CAPY_TLS_BEARSSL_ADAPTER_SCHEMA_VERSION)
    return 0;
  if (adapter->engine_id != CAPY_TLS_BEARSSL_ADAPTER_ENGINE_BEARSSL_USERLAND)
    return 0;
  if (adapter->flags != CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FLAGS) return 0;
  if (adapter->backend_plan_fingerprint !=
      CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT) return 0;
  if (adapter->reserved_state_fingerprint !=
      CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT) return 0;
  if (adapter->trust_manifest_fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT) return 0;
  if (adapter->trust_anchor_bundle_fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT) return 0;
  if (adapter->fingerprint != CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FINGERPRINT)
    return 0;
  if (capy_tls_bearssl_adapter_fingerprint_compute() !=
      CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FINGERPRINT) return 0;
  if (adapter->metadata_only != 1) return 0;
  if (adapter->adapter_initialized != 0) return 0;
  if (adapter->handshake_allowed != 0) return 0;
  if ((adapter->flags & CAPY_TLS_BEARSSL_ADAPTER_FLAG_METADATA_ONLY) == 0)
    return 0;
  if ((adapter->flags & CAPY_TLS_BEARSSL_ADAPTER_FLAG_BACKEND_PLAN_GATED) == 0)
    return 0;
  if ((adapter->flags & CAPY_TLS_BEARSSL_ADAPTER_FLAG_RESERVED_STATE_GATED) == 0)
    return 0;
  if ((adapter->flags & CAPY_TLS_BEARSSL_ADAPTER_FLAG_ENGINE_INIT_DISABLED) == 0)
    return 0;
  if ((adapter->flags & CAPY_TLS_BEARSSL_ADAPTER_FLAG_HANDSHAKE_DISABLED) == 0)
    return 0;
  return 1;
}
