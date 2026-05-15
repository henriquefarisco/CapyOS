#include "capy_tls_internal.h"

static uint32_t capy_tls_bearssl_state_fnv1a_byte(uint32_t hash, uint8_t byte) {
  hash ^= byte;
  hash *= 16777619u;
  return hash;
}

static uint32_t capy_tls_bearssl_state_fnv1a_cstr(uint32_t hash,
    const char *s) {
  while (*s) {
    hash = capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)*s);
    s++;
  }
  return hash;
}

static uint32_t capy_tls_bearssl_state_fnv1a_uint(uint32_t hash,
    uint32_t value) {
  char digits[10];
  size_t i = 0;
  if (value == 0)
    return capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)'0');
  while (value > 0 && i < sizeof(digits)) {
    digits[i] = (char)('0' + (value % 10u));
    value /= 10u;
    i++;
  }
  while (i > 0) {
    i--;
    hash = capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)digits[i]);
  }
  return hash;
}

static uint32_t capy_tls_bearssl_state_fingerprint_compute(void) {
  uint32_t hash = 2166136261u;
  hash = capy_tls_bearssl_state_fnv1a_cstr(hash,
      "capy-tls-bearssl-reserved-state:metadata-only:fail-closed:v1:");
  hash = capy_tls_bearssl_state_fnv1a_uint(hash,
      CAPY_TLS_BEARSSL_STATE_SCHEMA_VERSION);
  hash = capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_state_fnv1a_uint(hash,
      CAPY_TLS_BEARSSL_STATE_ENGINE_BEARSSL_USERLAND);
  hash = capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_state_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_BEARSSL_STATE_FLAGS);
  hash = capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_state_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT);
  hash = capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_state_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT);
  hash = capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_state_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT);
  hash = capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_state_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_BEARSSL_CONTEXT_BYTES);
  hash = capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_state_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_BEARSSL_IO_BUFFER_BYTES);
  hash = capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_state_fnv1a_uint(hash, 0u);
  hash = capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_bearssl_state_fnv1a_uint(hash, 0u);
  hash = capy_tls_bearssl_state_fnv1a_byte(hash, (uint8_t)':');
  return hash;
}

static const struct capy_tls_bearssl_reserved_state
    g_capy_tls_default_bearssl_reserved_state = {
  CAPY_TLS_BEARSSL_STATE_SCHEMA_VERSION,
  CAPY_TLS_BEARSSL_STATE_ENGINE_BEARSSL_USERLAND,
  CAPY_TLS_DEFAULT_BEARSSL_STATE_FLAGS,
  CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT,
  CAPY_TLS_DEFAULT_BEARSSL_CONTEXT_BYTES,
  CAPY_TLS_DEFAULT_BEARSSL_IO_BUFFER_BYTES,
  CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT,
  1,
  0,
  0
};

const struct capy_tls_bearssl_reserved_state *
capy_tls_default_bearssl_reserved_state(void) {
  return &g_capy_tls_default_bearssl_reserved_state;
}

uint32_t capy_tls_default_bearssl_state_schema_version(void) {
  return g_capy_tls_default_bearssl_reserved_state.schema_version;
}

uint32_t capy_tls_default_bearssl_state_engine_id(void) {
  return g_capy_tls_default_bearssl_reserved_state.engine_id;
}

uint32_t capy_tls_default_bearssl_state_flags(void) {
  return g_capy_tls_default_bearssl_reserved_state.flags;
}

uint32_t capy_tls_default_bearssl_state_fingerprint(void) {
  return g_capy_tls_default_bearssl_reserved_state.fingerprint;
}

int capy_tls_default_bearssl_reserved_state_consistent(void) {
  const struct capy_tls_bearssl_reserved_state *state;
  state = capy_tls_default_bearssl_reserved_state();
  if (!state) return 0;
  if (state->schema_version != CAPY_TLS_BEARSSL_STATE_SCHEMA_VERSION)
    return 0;
  if (state->engine_id != CAPY_TLS_BEARSSL_STATE_ENGINE_BEARSSL_USERLAND)
    return 0;
  if (state->flags != CAPY_TLS_DEFAULT_BEARSSL_STATE_FLAGS) return 0;
  if (state->backend_plan_fingerprint !=
      CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT) return 0;
  if (state->trust_manifest_fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT) return 0;
  if (state->trust_anchor_bundle_fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT) return 0;
  if (state->reserved_context_bytes != CAPY_TLS_DEFAULT_BEARSSL_CONTEXT_BYTES)
    return 0;
  if (state->reserved_io_bytes != CAPY_TLS_DEFAULT_BEARSSL_IO_BUFFER_BYTES)
    return 0;
  if (state->fingerprint != CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT)
    return 0;
  if (capy_tls_bearssl_state_fingerprint_compute() !=
      CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT) return 0;
  if (state->metadata_only != 1) return 0;
  if (state->engine_initialized != 0) return 0;
  if (state->handshake_allowed != 0) return 0;
  if ((state->flags & CAPY_TLS_BEARSSL_STATE_FLAG_METADATA_ONLY) == 0)
    return 0;
  if ((state->flags & CAPY_TLS_BEARSSL_STATE_FLAG_ENGINE_ABSENT) == 0)
    return 0;
  if ((state->flags & CAPY_TLS_BEARSSL_STATE_FLAG_CONTEXT_BYTES_ABSENT) == 0)
    return 0;
  if ((state->flags & CAPY_TLS_BEARSSL_STATE_FLAG_IO_BYTES_ABSENT) == 0)
    return 0;
  if ((state->flags & CAPY_TLS_BEARSSL_STATE_FLAG_HANDSHAKE_DISABLED) == 0)
    return 0;
  return 1;
}
