#include "capy_tls_internal.h"

static uint32_t capy_tls_trust_fnv1a_byte(uint32_t hash, uint8_t byte) {
  hash ^= byte;
  hash *= 16777619u;
  return hash;
}

static uint32_t capy_tls_trust_fnv1a_cstr(uint32_t hash, const char *s) {
  while (*s) {
    hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)*s);
    s++;
  }
  return hash;
}

static uint32_t capy_tls_trust_fnv1a_uint(uint32_t hash, uint32_t value) {
  char digits[10];
  size_t i = 0;
  if (value == 0) return capy_tls_trust_fnv1a_byte(hash, (uint8_t)'0');
  while (value > 0 && i < sizeof(digits)) {
    digits[i] = (char)('0' + (value % 10u));
    value /= 10u;
    i++;
  }
  while (i > 0) {
    i--;
    hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)digits[i]);
  }
  return hash;
}

static uint32_t capy_tls_trust_catalog_fingerprint_compute(void) {
  uint32_t hash = 2166136261u;
  hash = capy_tls_trust_fnv1a_cstr(hash,
      "capy-tls-default-trust-catalog:metadata-only:v1:");
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_CUSTOM_TRUST_ANCHOR_SLOT_COUNT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK);
  return hash;
}

static const struct capy_tls_trust_anchor_slot g_capy_tls_default_trust_slots[CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT] = {
  {0u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {1u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {2u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {3u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {4u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {5u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {6u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {7u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {8u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {9u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {10u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {11u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {12u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {13u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {14u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {15u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {16u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {17u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {18u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {19u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {20u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {21u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {22u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {23u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {24u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {25u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {26u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {27u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {28u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {29u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {30u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {31u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {32u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {33u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {34u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {35u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {36u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {37u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {38u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {39u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {40u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {41u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {42u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {43u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {44u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {45u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {46u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {47u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {48u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {49u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {50u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {51u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {52u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {53u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {54u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {55u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {56u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {57u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {58u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {59u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {60u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {61u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {62u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {63u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {64u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {65u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {66u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {67u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {68u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {69u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {70u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {71u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {72u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {73u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {74u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {75u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {76u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {77u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {78u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {79u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {80u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {81u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {82u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {83u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {84u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {85u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {86u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {87u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {88u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {89u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {90u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {91u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {92u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {93u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {94u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {95u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {96u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {97u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {98u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {99u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {100u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {101u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {102u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {103u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {104u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {105u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {106u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {107u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {108u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {109u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {110u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {111u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {112u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {113u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {114u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {115u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {116u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {117u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {118u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {119u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {120u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {121u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {122u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {123u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {124u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {125u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {126u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {127u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {128u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {129u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {130u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {131u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {132u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {133u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {134u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {135u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {136u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {137u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {138u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {139u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {140u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {141u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {142u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {143u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
  {144u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK, 1},
  {145u, CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK, 1},
};

static uint32_t capy_tls_trust_slot_layout_fingerprint_compute(void) {
  uint32_t hash = 2166136261u;
  uint32_t i;
  hash = capy_tls_trust_fnv1a_cstr(hash,
      "capy-tls-default-trust-slots:metadata-only:v1:");
  for (i = 0; i < CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT; i++) {
    hash = capy_tls_trust_fnv1a_uint(hash,
        g_capy_tls_default_trust_slots[i].index);
    hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
    hash = capy_tls_trust_fnv1a_uint(hash,
        g_capy_tls_default_trust_slots[i].key_type);
    hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
    hash = capy_tls_trust_fnv1a_uint(hash,
        g_capy_tls_default_trust_slots[i].metadata_only);
    hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  }
  return hash;
}

static uint32_t capy_tls_trust_descriptor_fingerprint_compute(void) {
  uint32_t hash = 2166136261u;
  uint32_t i;
  hash = capy_tls_trust_fnv1a_cstr(hash,
      "capy-tls-default-trust-descriptors:metadata-only:v1:");
  for (i = 0; i < CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT; i++) {
    hash = capy_tls_trust_fnv1a_uint(hash,
        g_capy_tls_default_trust_slots[i].index);
    hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
    hash = capy_tls_trust_fnv1a_uint(hash,
        g_capy_tls_default_trust_slots[i].key_type);
    hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
    hash = capy_tls_trust_fnv1a_uint(hash,
        g_capy_tls_default_trust_slots[i].metadata_only);
    hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
    hash = capy_tls_trust_fnv1a_uint(hash,
        CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS);
    hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  }
  return hash;
}

static uint32_t capy_tls_trust_material_summary_fingerprint_compute(void) {
  uint32_t hash = 2166136261u;
  hash = capy_tls_trust_fnv1a_cstr(hash,
      "capy-tls-default-trust-material-summary:metadata-only:v1:");
  hash = capy_tls_trust_fnv1a_uint(hash, CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FLAGS);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash, 1u);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  return hash;
}

static uint32_t capy_tls_trust_manifest_fingerprint_compute(void) {
  uint32_t hash = 2166136261u;
  hash = capy_tls_trust_fnv1a_cstr(hash,
      "capy-tls-default-trust-manifest:metadata-only:v3:");
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_SOURCE_KERNEL_BUNDLE);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_FLAGS);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_CUSTOM_TRUST_ANCHOR_SLOT_COUNT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash,
      CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  hash = capy_tls_trust_fnv1a_uint(hash, 1u);
  hash = capy_tls_trust_fnv1a_byte(hash, (uint8_t)':');
  return hash;
}

static void capy_tls_trust_anchor_descriptor_zero(
    struct capy_tls_trust_anchor_descriptor *descriptor) {
  uint8_t *p = (uint8_t *)descriptor;
  size_t i;
  if (!descriptor) return;
  for (i = 0; i < sizeof(*descriptor); i++) p[i] = 0;
}

static const struct capy_tls_trust_anchor_catalog g_capy_tls_default_trust_catalog = {
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT,
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT,
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT,
  CAPY_TLS_CUSTOM_TRUST_ANCHOR_SLOT_COUNT,
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK,
  CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT,
  1
};

static const struct capy_tls_trust_material_summary g_capy_tls_default_trust_material_summary = {
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT,
  CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES,
  CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES,
  CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES,
  CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES,
  CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FLAGS,
  1
};

static const struct capy_tls_trust_store_manifest g_capy_tls_default_trust_manifest = {
  CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION,
  CAPY_TLS_DEFAULT_TRUST_MANIFEST_SOURCE_KERNEL_BUNDLE,
  CAPY_TLS_DEFAULT_TRUST_MANIFEST_FLAGS,
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT,
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT,
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT,
  CAPY_TLS_CUSTOM_TRUST_ANCHOR_SLOT_COUNT,
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK,
  CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT,
  CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES,
  CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES,
  CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES,
  CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES,
  CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT,
  1
};

const struct capy_tls_trust_anchor_catalog *capy_tls_default_trust_anchor_catalog(void) {
  return &g_capy_tls_default_trust_catalog;
}

const struct capy_tls_trust_anchor_slot *capy_tls_default_trust_anchor_slots(void) {
  return g_capy_tls_default_trust_slots;
}

const struct capy_tls_trust_material_summary *capy_tls_default_trust_material_summary(void) {
  return &g_capy_tls_default_trust_material_summary;
}

const struct capy_tls_trust_store_manifest *capy_tls_default_trust_store_manifest(void) {
  return &g_capy_tls_default_trust_manifest;
}

uint32_t capy_tls_default_trust_anchor_count(void) {
  return g_capy_tls_default_trust_catalog.anchor_count;
}

uint32_t capy_tls_default_trust_anchor_rsa_count(void) {
  return g_capy_tls_default_trust_catalog.rsa_anchor_count;
}

uint32_t capy_tls_default_trust_anchor_ec_count(void) {
  return g_capy_tls_default_trust_catalog.ec_anchor_count;
}

uint32_t capy_tls_default_trust_anchor_key_type_mask(void) {
  return g_capy_tls_default_trust_catalog.key_type_mask;
}

uint32_t capy_tls_default_trust_catalog_fingerprint(void) {
  return g_capy_tls_default_trust_catalog.fingerprint;
}

uint32_t capy_tls_default_trust_anchor_slot_count(void) {
  return g_capy_tls_default_trust_catalog.anchor_count;
}

uint32_t capy_tls_default_trust_anchor_slot_key_type(uint32_t index) {
  if (index >= capy_tls_default_trust_anchor_slot_count()) return 0;
  return g_capy_tls_default_trust_slots[index].key_type;
}

uint32_t capy_tls_default_trust_anchor_slot_layout_fingerprint(void) {
  return g_capy_tls_default_trust_catalog.slot_layout_fingerprint;
}

int capy_tls_default_trust_anchor_descriptor(
    uint32_t index,
    struct capy_tls_trust_anchor_descriptor *out) {
  if (!out) return 0;
  capy_tls_trust_anchor_descriptor_zero(out);
  if (index >= capy_tls_default_trust_anchor_descriptor_count()) return 0;
  out->index = g_capy_tls_default_trust_slots[index].index;
  out->key_type = g_capy_tls_default_trust_slots[index].key_type;
  out->metadata_only = 1;
  out->flags = CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS;
  return 1;
}

uint32_t capy_tls_default_trust_anchor_descriptor_count(void) {
  return g_capy_tls_default_trust_catalog.anchor_count;
}

uint32_t capy_tls_default_trust_anchor_descriptor_flags(void) {
  return CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS;
}

uint32_t capy_tls_default_trust_anchor_descriptor_fingerprint(void) {
  return g_capy_tls_default_trust_catalog.descriptor_fingerprint;
}

uint32_t capy_tls_default_trust_material_summary_fingerprint(void) {
  return g_capy_tls_default_trust_material_summary.fingerprint;
}

uint32_t capy_tls_default_trust_material_summary_flags(void) {
  return g_capy_tls_default_trust_material_summary.flags;
}

uint32_t capy_tls_default_trust_subject_dn_total_bytes(void) {
  return g_capy_tls_default_trust_material_summary.subject_dn_total_bytes;
}

uint32_t capy_tls_default_trust_key_material_total_bytes(void) {
  return g_capy_tls_default_trust_material_summary.key_material_total_bytes;
}

uint32_t capy_tls_default_trust_subject_dn_max_bytes(void) {
  return g_capy_tls_default_trust_material_summary.subject_dn_max_bytes;
}

uint32_t capy_tls_default_trust_key_material_max_bytes(void) {
  return g_capy_tls_default_trust_material_summary.key_material_max_bytes;
}

uint32_t capy_tls_default_trust_manifest_schema_version(void) {
  return g_capy_tls_default_trust_manifest.schema_version;
}

uint32_t capy_tls_default_trust_manifest_source_id(void) {
  return g_capy_tls_default_trust_manifest.source_id;
}

uint32_t capy_tls_default_trust_manifest_flags(void) {
  return g_capy_tls_default_trust_manifest.flags;
}

uint32_t capy_tls_default_trust_manifest_fingerprint(void) {
  return g_capy_tls_default_trust_manifest.manifest_fingerprint;
}

int capy_tls_default_trust_catalog_consistent(void) {
  const struct capy_tls_trust_anchor_catalog *catalog;
  catalog = capy_tls_default_trust_anchor_catalog();
  if (!catalog) return 0;
  if (catalog->metadata_only != 1) return 0;
  if (catalog->anchor_count != CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT) return 0;
  if (catalog->rsa_anchor_count !=
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT) return 0;
  if (catalog->ec_anchor_count !=
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT) return 0;
  if (catalog->custom_anchor_slots !=
      CAPY_TLS_CUSTOM_TRUST_ANCHOR_SLOT_COUNT) return 0;
  if (catalog->key_type_mask !=
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK) return 0;
  if (catalog->fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT) return 0;
  if (catalog->slot_layout_fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT) return 0;
  if (catalog->descriptor_fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT) return 0;
  if (catalog->anchor_bundle_fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT) return 0;
  if (catalog->material_summary_fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT) return 0;
  if (capy_tls_trust_catalog_fingerprint_compute() !=
      CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT) return 0;
  if (capy_tls_trust_descriptor_fingerprint_compute() !=
      CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT) return 0;
  if (capy_tls_trust_material_summary_fingerprint_compute() !=
      CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT) return 0;
  if (capy_tls_trust_manifest_fingerprint_compute() !=
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT) return 0;
  if (catalog->anchor_count == 0) return 0;
  if (catalog->rsa_anchor_count + catalog->ec_anchor_count !=
      catalog->anchor_count) return 0;
  if ((catalog->key_type_mask &
       CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK) == 0) return 0;
  if ((catalog->key_type_mask &
       CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK) == 0) return 0;
  return 1;
}

int capy_tls_default_trust_anchor_slots_consistent(void) {
  const struct capy_tls_trust_anchor_catalog *catalog;
  const struct capy_tls_trust_anchor_slot *slots;
  uint32_t rsa_count = 0;
  uint32_t ec_count = 0;
  uint32_t key_type_mask = 0;
  uint32_t i;
  catalog = capy_tls_default_trust_anchor_catalog();
  slots = capy_tls_default_trust_anchor_slots();
  if (!catalog || !slots) return 0;
  if (capy_tls_default_trust_anchor_slot_count() !=
      catalog->anchor_count) return 0;
  if (capy_tls_trust_slot_layout_fingerprint_compute() !=
      CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT) return 0;
  for (i = 0; i < catalog->anchor_count; i++) {
    if (slots[i].index != i) return 0;
    if (slots[i].metadata_only != 1) return 0;
    if (slots[i].key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK) {
      rsa_count++;
    } else if (slots[i].key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK) {
      ec_count++;
    } else {
      return 0;
    }
    key_type_mask |= slots[i].key_type;
  }
  if (rsa_count != catalog->rsa_anchor_count) return 0;
  if (ec_count != catalog->ec_anchor_count) return 0;
  if (key_type_mask != catalog->key_type_mask) return 0;
  return 1;
}

int capy_tls_default_trust_anchor_descriptors_consistent(void) {
  const struct capy_tls_trust_anchor_slot *slots;
  struct capy_tls_trust_anchor_descriptor descriptor;
  uint32_t rsa_count = 0;
  uint32_t ec_count = 0;
  uint32_t key_type_mask = 0;
  uint32_t i;
  slots = capy_tls_default_trust_anchor_slots();
  if (!slots) return 0;
  if (!capy_tls_default_trust_anchor_slots_consistent()) return 0;
  if (capy_tls_default_trust_anchor_descriptor_count() !=
      capy_tls_default_trust_anchor_slot_count()) return 0;
  if (capy_tls_default_trust_anchor_descriptor_flags() !=
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS) return 0;
  if (capy_tls_default_trust_anchor_descriptor_fingerprint() !=
      CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT) return 0;
  if (capy_tls_trust_descriptor_fingerprint_compute() !=
      CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT) return 0;
  for (i = 0; i < capy_tls_default_trust_anchor_descriptor_count(); i++) {
    if (!capy_tls_default_trust_anchor_descriptor(i, &descriptor)) return 0;
    if (descriptor.index != slots[i].index) return 0;
    if (descriptor.key_type != slots[i].key_type) return 0;
    if (descriptor.metadata_only != 1) return 0;
    if (descriptor.flags !=
        CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS) return 0;
    if (descriptor.key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK) {
      rsa_count++;
    } else if (descriptor.key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK) {
      ec_count++;
    } else return 0;
    key_type_mask |= descriptor.key_type;
  }
  if (capy_tls_default_trust_anchor_descriptor(
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT, &descriptor)) return 0;
  if (descriptor.index != 0 || descriptor.key_type != 0 ||
      descriptor.metadata_only != 0 || descriptor.flags != 0) return 0;
  if (rsa_count != CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT) return 0;
  if (ec_count != CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT) return 0;
  if (key_type_mask != CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK) return 0;
  return 1;
}

int capy_tls_default_trust_material_summary_consistent(void) {
  const struct capy_tls_trust_anchor_catalog *catalog;
  const struct capy_tls_trust_anchor_bundle *bundle;
  const struct capy_tls_trust_material_summary *summary;
  catalog = capy_tls_default_trust_anchor_catalog();
  bundle = capy_tls_default_trust_anchor_bundle();
  summary = capy_tls_default_trust_material_summary();
  if (!catalog || !bundle || !summary) return 0;
  if (!capy_tls_default_trust_anchor_descriptors_consistent()) return 0;
  if (!capy_tls_default_trust_anchor_bundle_consistent()) return 0;
  if (summary->metadata_only != 1) return 0;
  if (summary->anchor_count != catalog->anchor_count) return 0;
  if (summary->anchor_count != bundle->entry_count) return 0;
  if (summary->subject_dn_total_bytes !=
      CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES) return 0;
  if (summary->subject_dn_total_bytes !=
      bundle->subject_dn_total_bytes) return 0;
  if (summary->key_material_total_bytes !=
      CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES) return 0;
  if (summary->key_material_total_bytes !=
      bundle->key_material_total_bytes) return 0;
  if (summary->subject_dn_max_bytes !=
      CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES) return 0;
  if (summary->subject_dn_max_bytes !=
      bundle->subject_dn_max_bytes) return 0;
  if (summary->key_material_max_bytes !=
      CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES) return 0;
  if (summary->key_material_max_bytes !=
      bundle->key_material_max_bytes) return 0;
  if (summary->fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT) return 0;
  if (summary->flags !=
      CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FLAGS) return 0;
  if (catalog->material_summary_fingerprint != summary->fingerprint) return 0;
  if (capy_tls_trust_material_summary_fingerprint_compute() !=
      CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT) return 0;
  if ((summary->flags &
       CAPY_TLS_TRUST_MATERIAL_SUMMARY_FLAG_METADATA_ONLY) == 0)
    return 0;
  if ((summary->flags &
       CAPY_TLS_TRUST_MATERIAL_SUMMARY_FLAG_KERNEL_BUNDLE_DERIVED) == 0)
    return 0;
  if ((summary->flags &
       CAPY_TLS_TRUST_MATERIAL_SUMMARY_FLAG_CERT_BYTES_ABSENT) == 0)
    return 0;
  if ((summary->flags &
       CAPY_TLS_TRUST_MATERIAL_SUMMARY_FLAG_AGGREGATE_ONLY) == 0)
    return 0;
  return 1;
}

int capy_tls_default_trust_store_manifest_consistent(void) {
  const struct capy_tls_trust_anchor_catalog *catalog;
  const struct capy_tls_trust_anchor_bundle *bundle;
  const struct capy_tls_trust_material_summary *summary;
  const struct capy_tls_trust_store_manifest *manifest;
  catalog = capy_tls_default_trust_anchor_catalog();
  bundle = capy_tls_default_trust_anchor_bundle();
  summary = capy_tls_default_trust_material_summary();
  manifest = capy_tls_default_trust_store_manifest();
  if (!catalog || !bundle || !summary || !manifest) return 0;
  if (!capy_tls_default_trust_catalog_consistent()) return 0;
  if (!capy_tls_default_trust_anchor_slots_consistent()) return 0;
  if (!capy_tls_default_trust_anchor_descriptors_consistent()) return 0;
  if (!capy_tls_default_trust_anchor_bundle_consistent()) return 0;
  if (!capy_tls_default_trust_material_summary_consistent()) return 0;
  if (manifest->metadata_only != 1) return 0;
  if (manifest->schema_version !=
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION) return 0;
  if (manifest->source_id !=
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_SOURCE_KERNEL_BUNDLE) return 0;
  if (manifest->flags != CAPY_TLS_DEFAULT_TRUST_MANIFEST_FLAGS) return 0;
  if (manifest->anchor_count != catalog->anchor_count) return 0;
  if (manifest->rsa_anchor_count != catalog->rsa_anchor_count) return 0;
  if (manifest->ec_anchor_count != catalog->ec_anchor_count) return 0;
  if (manifest->custom_anchor_slots != catalog->custom_anchor_slots) return 0;
  if (manifest->key_type_mask != catalog->key_type_mask) return 0;
  if (manifest->catalog_fingerprint != catalog->fingerprint) return 0;
  if (manifest->slot_layout_fingerprint !=
      catalog->slot_layout_fingerprint) return 0;
  if (manifest->descriptor_fingerprint !=
      catalog->descriptor_fingerprint) return 0;
  if (manifest->anchor_bundle_fingerprint !=
      catalog->anchor_bundle_fingerprint) return 0;
  if (manifest->anchor_bundle_fingerprint != bundle->fingerprint) return 0;
  if (manifest->material_summary_fingerprint !=
      catalog->material_summary_fingerprint) return 0;
  if (manifest->material_summary_fingerprint !=
      summary->fingerprint) return 0;
  if (manifest->subject_dn_total_bytes !=
      summary->subject_dn_total_bytes) return 0;
  if (manifest->key_material_total_bytes !=
      summary->key_material_total_bytes) return 0;
  if (manifest->subject_dn_max_bytes !=
      summary->subject_dn_max_bytes) return 0;
  if (manifest->key_material_max_bytes !=
      summary->key_material_max_bytes) return 0;
  if (manifest->manifest_fingerprint !=
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT) return 0;
  if (capy_tls_trust_manifest_fingerprint_compute() !=
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT) return 0;
  if ((manifest->flags & CAPY_TLS_TRUST_MANIFEST_FLAG_METADATA_ONLY) == 0)
    return 0;
  if ((manifest->flags &
       CAPY_TLS_TRUST_MANIFEST_FLAG_KERNEL_BUNDLE_DERIVED) == 0)
    return 0;
  if ((manifest->flags &
       CAPY_TLS_TRUST_MANIFEST_FLAG_CERT_BYTES_ABSENT) == 0)
    return 0;
  if ((manifest->flags &
       CAPY_TLS_TRUST_MANIFEST_FLAG_FAIL_CLOSED_ONLY) == 0)
    return 0;
  return 1;
}

int capy_tls_default_trust_anchors_available(void) {
  if (!capy_tls_default_trust_catalog_consistent()) return 0;
  if (!capy_tls_default_trust_anchor_slots_consistent()) return 0;
  if (!capy_tls_default_trust_anchor_descriptors_consistent()) return 0;
  if (!capy_tls_default_trust_anchor_bundle_consistent()) return 0;
  if (!capy_tls_default_trust_material_summary_consistent()) return 0;
  if (!capy_tls_default_trust_store_manifest_consistent()) return 0;
  return 1;
}
