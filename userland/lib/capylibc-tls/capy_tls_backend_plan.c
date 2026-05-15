#include "capy_tls_internal.h"

static uint32_t capy_tls_backend_plan_fnv1a_byte(uint32_t hash, uint8_t byte) {
  hash ^= byte;
  hash *= 16777619u;
  return hash;
}

static uint32_t capy_tls_backend_plan_fnv1a_cstr(uint32_t hash, const char *s) {
  while (*s) {
    hash = capy_tls_backend_plan_fnv1a_byte(hash, (uint8_t)*s);
    s++;
  }
  return hash;
}

static uint32_t capy_tls_backend_plan_fnv1a_uint(uint32_t hash, uint32_t value) {
  char digits[10];
  size_t i = 0;
  if (value == 0) return capy_tls_backend_plan_fnv1a_byte(hash, (uint8_t)'0');
  while (value > 0 && i < sizeof(digits)) {
    digits[i] = (char)('0' + (value % 10u));
    value /= 10u;
    i++;
  }
  while (i > 0) {
    i--;
    hash = capy_tls_backend_plan_fnv1a_byte(hash, (uint8_t)digits[i]);
  }
  return hash;
}

static uint32_t capy_tls_backend_plan_fingerprint_compute(void) {
  uint32_t hash = 2166136261u;
  hash = capy_tls_backend_plan_fnv1a_cstr(hash,
      "capy-tls-backend-plan:bearssl-userland:fail-closed:v1:");
  hash = capy_tls_backend_plan_fnv1a_uint(hash,
      CAPY_TLS_BACKEND_PLAN_SCHEMA_VERSION);
  hash = capy_tls_backend_plan_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_backend_plan_fnv1a_uint(hash,
      CAPY_TLS_BACKEND_PLAN_ENGINE_BEARSSL_USERLAND);
  hash = capy_tls_backend_plan_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_backend_plan_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_BACKEND_PLAN_FLAGS);
  hash = capy_tls_backend_plan_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_backend_plan_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION);
  hash = capy_tls_backend_plan_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_backend_plan_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT);
  hash = capy_tls_backend_plan_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_backend_plan_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT);
  hash = capy_tls_backend_plan_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_backend_plan_fnv1a_uint(hash, 0u);
  hash = capy_tls_backend_plan_fnv1a_byte(hash, (uint8_t)':');
  return hash;
}

static const struct capy_tls_backend_plan g_capy_tls_default_backend_plan = {
  CAPY_TLS_BACKEND_PLAN_SCHEMA_VERSION,
  CAPY_TLS_BACKEND_PLAN_ENGINE_BEARSSL_USERLAND,
  CAPY_TLS_DEFAULT_BACKEND_PLAN_FLAGS,
  CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION,
  CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT,
  CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT,
  0
};

const struct capy_tls_backend_plan *capy_tls_default_backend_plan(void) {
  return &g_capy_tls_default_backend_plan;
}

uint32_t capy_tls_default_backend_plan_schema_version(void) {
  return g_capy_tls_default_backend_plan.schema_version;
}

uint32_t capy_tls_default_backend_plan_engine_id(void) {
  return g_capy_tls_default_backend_plan.engine_id;
}

uint32_t capy_tls_default_backend_plan_flags(void) {
  return g_capy_tls_default_backend_plan.flags;
}

uint32_t capy_tls_default_backend_plan_fingerprint(void) {
  return g_capy_tls_default_backend_plan.fingerprint;
}

int capy_tls_default_backend_plan_consistent(void) {
  const struct capy_tls_backend_plan *plan;
  plan = capy_tls_default_backend_plan();
  if (!plan) return 0;
  if (plan->schema_version != CAPY_TLS_BACKEND_PLAN_SCHEMA_VERSION) return 0;
  if (plan->engine_id != CAPY_TLS_BACKEND_PLAN_ENGINE_BEARSSL_USERLAND)
    return 0;
  if (plan->flags != CAPY_TLS_DEFAULT_BACKEND_PLAN_FLAGS) return 0;
  if (plan->trust_manifest_schema_version !=
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION) return 0;
  if (plan->trust_manifest_fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT) return 0;
  if (plan->trust_anchor_bundle_fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT) return 0;
  if (plan->fingerprint != CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT)
    return 0;
  if (capy_tls_backend_plan_fingerprint_compute() !=
      CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT) return 0;
  if (plan->handshake_allowed != 0) return 0;
  if ((plan->flags & CAPY_TLS_BACKEND_PLAN_FLAG_FAIL_CLOSED_ONLY) == 0)
    return 0;
  if ((plan->flags & CAPY_TLS_BACKEND_PLAN_FLAG_HANDSHAKE_DISABLED) == 0)
    return 0;
  if ((plan->flags & CAPY_TLS_BACKEND_PLAN_FLAG_TRUST_METADATA_GATED) == 0)
    return 0;
  if ((plan->flags & CAPY_TLS_BACKEND_PLAN_FLAG_BEARSSL_STATE_ABSENT) == 0)
    return 0;
  return 1;
}
