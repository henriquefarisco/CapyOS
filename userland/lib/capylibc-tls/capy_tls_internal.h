#ifndef CAPYLIBC_TLS_CAPY_TLS_INTERNAL_H
#define CAPYLIBC_TLS_CAPY_TLS_INTERNAL_H

#include "capylibc-tls/capy_tls.h"

#define CAPY_TLS_HOSTNAME_MAX_LEN 253u
#define CAPY_TLS_HOSTNAME_BUFFER_LEN (CAPY_TLS_HOSTNAME_MAX_LEN + 1u)
#define CAPY_TLS_CONTEXT_SLOT_COUNT 1u
#define CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT 146u
#define CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT 106u
#define CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT 40u
#define CAPY_TLS_CUSTOM_TRUST_ANCHOR_SLOT_COUNT 1u
#define CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK 0x1u
#define CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK 0x2u
#define CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK 0x3u
#define CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT 0xDB22D94Au
#define CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT 0x07A622ABu
#define CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_METADATA_ONLY 0x1u
#define CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_SLOT_BACKED 0x2u
#define CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_KEY_TYPE_KNOWN 0x4u
#define CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_CERT_BYTES_ABSENT 0x8u
#define CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS 0xFu
#define CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT 0xE1A18A70u
#define CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION 3u
#define CAPY_TLS_DEFAULT_TRUST_MANIFEST_SOURCE_KERNEL_BUNDLE 0x1u
#define CAPY_TLS_TRUST_MANIFEST_FLAG_METADATA_ONLY 0x1u
#define CAPY_TLS_TRUST_MANIFEST_FLAG_KERNEL_BUNDLE_DERIVED 0x2u
#define CAPY_TLS_TRUST_MANIFEST_FLAG_CERT_BYTES_ABSENT 0x4u
#define CAPY_TLS_TRUST_MANIFEST_FLAG_FAIL_CLOSED_ONLY 0x8u
#define CAPY_TLS_DEFAULT_TRUST_MANIFEST_FLAGS 0xFu
#define CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT 0xBD2653A4u
#define CAPY_TLS_TRUST_MATERIAL_SUMMARY_FLAG_METADATA_ONLY 0x1u
#define CAPY_TLS_TRUST_MATERIAL_SUMMARY_FLAG_KERNEL_BUNDLE_DERIVED 0x2u
#define CAPY_TLS_TRUST_MATERIAL_SUMMARY_FLAG_CERT_BYTES_ABSENT 0x4u
#define CAPY_TLS_TRUST_MATERIAL_SUMMARY_FLAG_AGGREGATE_ONLY 0x8u
#define CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FLAGS 0xFu
#define CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT 0x8DFC9FAFu
#define CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES 14385u
#define CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES 47329u
#define CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES 213u
#define CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES 515u
#define CAPY_TLS_TRUST_ANCHOR_BUNDLE_ENTRY_FLAG_METADATA_ONLY 0x1u
#define CAPY_TLS_TRUST_ANCHOR_BUNDLE_ENTRY_FLAG_SUBJECT_DN_SIZE_KNOWN 0x2u
#define CAPY_TLS_TRUST_ANCHOR_BUNDLE_ENTRY_FLAG_KEY_MATERIAL_SIZE_KNOWN 0x4u
#define CAPY_TLS_TRUST_ANCHOR_BUNDLE_ENTRY_FLAG_CERT_BYTES_ABSENT 0x8u
#define CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_ENTRY_FLAGS 0xFu
#define CAPY_TLS_TRUST_ANCHOR_BUNDLE_FLAG_METADATA_ONLY 0x1u
#define CAPY_TLS_TRUST_ANCHOR_BUNDLE_FLAG_KERNEL_BUNDLE_DERIVED 0x2u
#define CAPY_TLS_TRUST_ANCHOR_BUNDLE_FLAG_CERT_BYTES_ABSENT 0x4u
#define CAPY_TLS_TRUST_ANCHOR_BUNDLE_FLAG_FAIL_CLOSED_ONLY 0x8u
#define CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FLAGS 0xFu
#define CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT 0x0ED4E969u
#define CAPY_TLS_BACKEND_PLAN_SCHEMA_VERSION 1u
#define CAPY_TLS_BACKEND_PLAN_ENGINE_BEARSSL_USERLAND 0x1u
#define CAPY_TLS_BACKEND_PLAN_FLAG_FAIL_CLOSED_ONLY 0x1u
#define CAPY_TLS_BACKEND_PLAN_FLAG_HANDSHAKE_DISABLED 0x2u
#define CAPY_TLS_BACKEND_PLAN_FLAG_TRUST_METADATA_GATED 0x4u
#define CAPY_TLS_BACKEND_PLAN_FLAG_BEARSSL_STATE_ABSENT 0x8u
#define CAPY_TLS_DEFAULT_BACKEND_PLAN_FLAGS 0xFu
#define CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT 0x4F809D54u
#define CAPY_TLS_BEARSSL_STATE_SCHEMA_VERSION 1u
#define CAPY_TLS_BEARSSL_STATE_ENGINE_BEARSSL_USERLAND 0x1u
#define CAPY_TLS_BEARSSL_STATE_FLAG_METADATA_ONLY 0x1u
#define CAPY_TLS_BEARSSL_STATE_FLAG_ENGINE_ABSENT 0x2u
#define CAPY_TLS_BEARSSL_STATE_FLAG_CONTEXT_BYTES_ABSENT 0x4u
#define CAPY_TLS_BEARSSL_STATE_FLAG_IO_BYTES_ABSENT 0x8u
#define CAPY_TLS_BEARSSL_STATE_FLAG_HANDSHAKE_DISABLED 0x10u
#define CAPY_TLS_DEFAULT_BEARSSL_STATE_FLAGS 0x1Fu
#define CAPY_TLS_DEFAULT_BEARSSL_CONTEXT_BYTES 0u
#define CAPY_TLS_DEFAULT_BEARSSL_IO_BUFFER_BYTES 0u
#define CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT 0x7D1732D0u
#define CAPY_TLS_BEARSSL_ADAPTER_SCHEMA_VERSION 1u
#define CAPY_TLS_BEARSSL_ADAPTER_ENGINE_BEARSSL_USERLAND 0x1u
#define CAPY_TLS_BEARSSL_ADAPTER_FLAG_METADATA_ONLY 0x1u
#define CAPY_TLS_BEARSSL_ADAPTER_FLAG_BACKEND_PLAN_GATED 0x2u
#define CAPY_TLS_BEARSSL_ADAPTER_FLAG_RESERVED_STATE_GATED 0x4u
#define CAPY_TLS_BEARSSL_ADAPTER_FLAG_ENGINE_INIT_DISABLED 0x8u
#define CAPY_TLS_BEARSSL_ADAPTER_FLAG_HANDSHAKE_DISABLED 0x10u
#define CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FLAGS 0x1Fu
#define CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FINGERPRINT 0xE73E3E65u

struct capy_tls_trust_anchor_catalog {
  uint32_t anchor_count;
  uint32_t rsa_anchor_count;
  uint32_t ec_anchor_count;
  uint32_t custom_anchor_slots;
  uint32_t key_type_mask;
  uint32_t fingerprint;
  uint32_t slot_layout_fingerprint;
  uint32_t descriptor_fingerprint;
  uint32_t anchor_bundle_fingerprint;
  uint32_t material_summary_fingerprint;
  int metadata_only;
};

struct capy_tls_trust_anchor_slot {
  uint16_t index;
  uint8_t key_type;
  uint8_t metadata_only;
};

struct capy_tls_trust_anchor_descriptor {
  uint16_t index;
  uint8_t key_type;
  uint8_t metadata_only;
  uint32_t flags;
};

struct capy_tls_trust_anchor_bundle_entry {
  uint16_t index;
  uint8_t key_type;
  uint8_t metadata_only;
  uint16_t subject_dn_bytes;
  uint16_t key_material_bytes;
  uint32_t flags;
};

struct capy_tls_trust_anchor_bundle {
  uint32_t entry_count;
  uint32_t subject_dn_total_bytes;
  uint32_t key_material_total_bytes;
  uint32_t subject_dn_max_bytes;
  uint32_t key_material_max_bytes;
  uint32_t fingerprint;
  uint32_t flags;
  int metadata_only;
};

struct capy_tls_trust_material_summary {
  uint32_t anchor_count;
  uint32_t subject_dn_total_bytes;
  uint32_t key_material_total_bytes;
  uint32_t subject_dn_max_bytes;
  uint32_t key_material_max_bytes;
  uint32_t fingerprint;
  uint32_t flags;
  int metadata_only;
};

struct capy_tls_trust_store_manifest {
  uint32_t schema_version;
  uint32_t source_id;
  uint32_t flags;
  uint32_t anchor_count;
  uint32_t rsa_anchor_count;
  uint32_t ec_anchor_count;
  uint32_t custom_anchor_slots;
  uint32_t key_type_mask;
  uint32_t catalog_fingerprint;
  uint32_t slot_layout_fingerprint;
  uint32_t descriptor_fingerprint;
  uint32_t anchor_bundle_fingerprint;
  uint32_t material_summary_fingerprint;
  uint32_t subject_dn_total_bytes;
  uint32_t key_material_total_bytes;
  uint32_t subject_dn_max_bytes;
  uint32_t key_material_max_bytes;
  uint32_t manifest_fingerprint;
  int metadata_only;
};

struct capy_tls_backend_plan {
  uint32_t schema_version;
  uint32_t engine_id;
  uint32_t flags;
  uint32_t trust_manifest_schema_version;
  uint32_t trust_manifest_fingerprint;
  uint32_t trust_anchor_bundle_fingerprint;
  uint32_t fingerprint;
  int handshake_allowed;
};

struct capy_tls_bearssl_reserved_state {
  uint32_t schema_version;
  uint32_t engine_id;
  uint32_t flags;
  uint32_t backend_plan_fingerprint;
  uint32_t trust_manifest_fingerprint;
  uint32_t trust_anchor_bundle_fingerprint;
  uint32_t reserved_context_bytes;
  uint32_t reserved_io_bytes;
  uint32_t fingerprint;
  int metadata_only;
  int engine_initialized;
  int handshake_allowed;
};

struct capy_tls_bearssl_adapter_contract {
  uint32_t schema_version;
  uint32_t engine_id;
  uint32_t flags;
  uint32_t backend_plan_fingerprint;
  uint32_t reserved_state_fingerprint;
  uint32_t trust_manifest_fingerprint;
  uint32_t trust_anchor_bundle_fingerprint;
  uint32_t fingerprint;
  int metadata_only;
  int adapter_initialized;
  int handshake_allowed;
};

struct capy_tls_effective_config {
  int verify_peer;
  const uint8_t *ca_cert;
  size_t ca_cert_len;
  uint32_t timeout_ms;
};

struct capy_tls_backend_state {
  int context_ready;
  int sni_ready;
  int timeout_ready;
  int trust_anchors_ready;
  int custom_anchor_ready;
  int handshake_started;
  int backend_plan_ready;
  int handshake_allowed;
  uint32_t backend_plan_schema_version;
  uint32_t backend_plan_engine_id;
  uint32_t backend_plan_flags;
  uint32_t backend_plan_fingerprint;
  int bearssl_state_ready;
  int bearssl_engine_initialized;
  uint32_t bearssl_state_schema_version;
  uint32_t bearssl_state_engine_id;
  uint32_t bearssl_state_flags;
  uint32_t bearssl_state_fingerprint;
  uint32_t bearssl_context_bytes;
  uint32_t bearssl_io_buffer_bytes;
  int bearssl_adapter_ready;
  int bearssl_adapter_initialized;
  uint32_t bearssl_adapter_schema_version;
  uint32_t bearssl_adapter_engine_id;
  uint32_t bearssl_adapter_flags;
  uint32_t bearssl_adapter_fingerprint;
  uint32_t timeout_ms;
  uint32_t trust_anchor_count;
  uint32_t trust_anchor_rsa_count;
  uint32_t trust_anchor_ec_count;
  uint32_t trust_anchor_key_type_mask;
  uint32_t trust_catalog_fingerprint;
  uint32_t trust_anchor_slot_count;
  uint32_t trust_slot_layout_fingerprint;
  uint32_t trust_anchor_descriptor_count;
  uint32_t trust_descriptor_fingerprint;
  uint32_t trust_anchor_bundle_entry_count;
  uint32_t trust_anchor_bundle_fingerprint;
  uint32_t trust_material_summary_fingerprint;
  uint32_t trust_subject_dn_total_bytes;
  uint32_t trust_key_material_total_bytes;
  uint32_t trust_subject_dn_max_bytes;
  uint32_t trust_key_material_max_bytes;
  uint32_t trust_manifest_schema_version;
  uint32_t trust_manifest_source_id;
  uint32_t trust_manifest_flags;
  uint32_t trust_manifest_fingerprint;
  size_t custom_anchor_len;
  char sni[CAPY_TLS_HOSTNAME_BUFFER_LEN];
};

struct capy_tls_context {
  int socket_fd;
  char hostname[CAPY_TLS_HOSTNAME_BUFFER_LEN];
  struct capy_tls_effective_config config;
  struct capy_tls_backend_state backend;
};

int capy_tls_config_resolve(
    const struct capy_tls_config *config,
    struct capy_tls_effective_config *out);
void capy_tls_context_reset(struct capy_tls_context *ctx);
void capy_tls_context_clear(struct capy_tls_context *ctx);
struct capy_tls_context *capy_tls_context_acquire(void);
void capy_tls_context_release(struct capy_tls_context *ctx);
const struct capy_tls_backend_plan *capy_tls_default_backend_plan(void);
uint32_t capy_tls_default_backend_plan_schema_version(void);
uint32_t capy_tls_default_backend_plan_engine_id(void);
uint32_t capy_tls_default_backend_plan_flags(void);
uint32_t capy_tls_default_backend_plan_fingerprint(void);
int capy_tls_default_backend_plan_consistent(void);
const struct capy_tls_bearssl_reserved_state *
capy_tls_default_bearssl_reserved_state(void);
uint32_t capy_tls_default_bearssl_state_schema_version(void);
uint32_t capy_tls_default_bearssl_state_engine_id(void);
uint32_t capy_tls_default_bearssl_state_flags(void);
uint32_t capy_tls_default_bearssl_state_fingerprint(void);
int capy_tls_default_bearssl_reserved_state_consistent(void);
const struct capy_tls_bearssl_adapter_contract *
capy_tls_default_bearssl_adapter_contract(void);
uint32_t capy_tls_default_bearssl_adapter_schema_version(void);
uint32_t capy_tls_default_bearssl_adapter_engine_id(void);
uint32_t capy_tls_default_bearssl_adapter_flags(void);
uint32_t capy_tls_default_bearssl_adapter_fingerprint(void);
int capy_tls_default_bearssl_adapter_consistent(void);
const struct capy_tls_trust_anchor_catalog *capy_tls_default_trust_anchor_catalog(void);
const struct capy_tls_trust_anchor_slot *capy_tls_default_trust_anchor_slots(void);
const struct capy_tls_trust_material_summary *capy_tls_default_trust_material_summary(void);
const struct capy_tls_trust_anchor_bundle_entry *capy_tls_default_trust_anchor_bundle_entries(void);
const struct capy_tls_trust_anchor_bundle *capy_tls_default_trust_anchor_bundle(void);
const struct capy_tls_trust_store_manifest *capy_tls_default_trust_store_manifest(void);
uint32_t capy_tls_default_trust_anchor_count(void);
uint32_t capy_tls_default_trust_anchor_rsa_count(void);
uint32_t capy_tls_default_trust_anchor_ec_count(void);
uint32_t capy_tls_default_trust_anchor_key_type_mask(void);
uint32_t capy_tls_default_trust_catalog_fingerprint(void);
uint32_t capy_tls_default_trust_anchor_slot_count(void);
uint32_t capy_tls_default_trust_anchor_slot_key_type(uint32_t index);
uint32_t capy_tls_default_trust_anchor_slot_layout_fingerprint(void);
int capy_tls_default_trust_anchor_descriptor(
    uint32_t index,
    struct capy_tls_trust_anchor_descriptor *out);
int capy_tls_default_trust_anchor_bundle_entry(
    uint32_t index,
    struct capy_tls_trust_anchor_bundle_entry *out);
uint32_t capy_tls_default_trust_anchor_descriptor_count(void);
uint32_t capy_tls_default_trust_anchor_descriptor_flags(void);
uint32_t capy_tls_default_trust_anchor_descriptor_fingerprint(void);
uint32_t capy_tls_default_trust_anchor_bundle_entry_count(void);
uint32_t capy_tls_default_trust_anchor_bundle_fingerprint(void);
uint32_t capy_tls_default_trust_anchor_bundle_flags(void);
uint32_t capy_tls_default_trust_material_summary_fingerprint(void);
uint32_t capy_tls_default_trust_material_summary_flags(void);
uint32_t capy_tls_default_trust_subject_dn_total_bytes(void);
uint32_t capy_tls_default_trust_key_material_total_bytes(void);
uint32_t capy_tls_default_trust_subject_dn_max_bytes(void);
uint32_t capy_tls_default_trust_key_material_max_bytes(void);
uint32_t capy_tls_default_trust_manifest_schema_version(void);
uint32_t capy_tls_default_trust_manifest_source_id(void);
uint32_t capy_tls_default_trust_manifest_flags(void);
uint32_t capy_tls_default_trust_manifest_fingerprint(void);
int capy_tls_default_trust_catalog_consistent(void);
int capy_tls_default_trust_anchor_slots_consistent(void);
int capy_tls_default_trust_anchor_descriptors_consistent(void);
int capy_tls_default_trust_anchor_bundle_consistent(void);
int capy_tls_default_trust_material_summary_consistent(void);
int capy_tls_default_trust_store_manifest_consistent(void);
int capy_tls_default_trust_anchors_available(void);
capy_tls_err_t capy_tls_backend_connect(struct capy_tls_context *ctx);
int capy_tls_context_prepare(
    struct capy_tls_context *ctx,
    int socket_fd,
    const char *hostname,
    const struct capy_tls_effective_config *config);

#endif
