/*
 * tests/userland/test_capylibc_tls_trust.c
 *
 * Trust-store metadata coverage for libcapy-tls (the userland TLS
 * façade in `userland/lib/capylibc-tls/`). This translation unit
 * pins:
 *
 *   - default trust anchor catalog identity, fingerprints and key
 *     distribution (RSA vs EC, custom-anchor slot count);
 *   - slot table layout and per-slot key types;
 *   - per-anchor descriptors (metadata-only flags, slot backing,
 *     cert-bytes-absent contract);
 *   - bundle entries and the aggregated material summary
 *     (subject DN / key material byte totals and maxima);
 *   - trust-store manifest provenance (schema version, source id,
 *     fail-closed-only flag) and its cross-consistency with the
 *     catalog.
 *
 * Carved out of the historical single-file
 * `tests/test_capylibc_tls.c` (1324 LOC) at the 2026-05-15 monolith
 * refactor. The TEST/PASS/FAIL macros, `tests_run`/`tests_passed`
 * counters and companion entry contract come from
 * `tests/userland/test_capylibc_tls_internal.h`. The owning entry is
 * `test_capylibc_tls_trust_cases()`, invoked by
 * `test_capylibc_tls_run` in `tests/userland/test_capylibc_tls.c`.
 */
#include <stdint.h>
#include <string.h>

#include "test_capylibc_tls_internal.h"

static void test_tls_default_trust_source_reports_bundle(void) {
  const struct capy_tls_trust_anchor_catalog *catalog;
  const struct capy_tls_trust_anchor_bundle *bundle;
  const struct capy_tls_trust_store_manifest *manifest;
  catalog = capy_tls_default_trust_anchor_catalog();
  bundle = capy_tls_default_trust_anchor_bundle();
  manifest = capy_tls_default_trust_store_manifest();
  TEST("capy_tls default trust source exposes metadata");
  if (catalog && bundle && manifest && capy_tls_default_trust_anchors_available() &&
      capy_tls_default_trust_anchor_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      catalog->anchor_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      catalog->custom_anchor_slots == CAPY_TLS_CUSTOM_TRUST_ANCHOR_SLOT_COUNT &&
      catalog->key_type_mask ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK &&
      catalog->fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT &&
      catalog->slot_layout_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT &&
      catalog->descriptor_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT &&
      catalog->anchor_bundle_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
      bundle->fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
      manifest->anchor_bundle_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
      manifest->manifest_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT &&
      catalog->metadata_only == 1 && bundle->metadata_only == 1 &&
      manifest->metadata_only == 1 &&
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT == 146u) PASS();
  else FAIL("default trust source metadata missing");
}

static void test_tls_default_trust_catalog_key_distribution(void) {
  const struct capy_tls_trust_anchor_catalog *catalog;
  catalog = capy_tls_default_trust_anchor_catalog();
  TEST("capy_tls default trust catalog records key distribution");
  if (catalog &&
      catalog->rsa_anchor_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT &&
      catalog->ec_anchor_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT &&
      catalog->rsa_anchor_count + catalog->ec_anchor_count ==
          catalog->anchor_count &&
      capy_tls_default_trust_anchor_rsa_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT &&
      capy_tls_default_trust_anchor_ec_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT &&
      capy_tls_default_trust_anchor_key_type_mask() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK &&
      capy_tls_default_trust_catalog_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT &&
      capy_tls_default_trust_anchor_slot_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      capy_tls_default_trust_anchor_slot_layout_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT &&
      capy_tls_default_trust_anchor_descriptor_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      capy_tls_default_trust_anchor_descriptor_flags() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS &&
      capy_tls_default_trust_anchor_descriptor_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT &&
      capy_tls_default_trust_anchor_bundle_entry_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      capy_tls_default_trust_anchor_bundle_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
      capy_tls_default_trust_anchor_bundle_flags() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FLAGS &&
      capy_tls_default_trust_material_summary_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT &&
      capy_tls_default_trust_subject_dn_total_bytes() ==
          CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES &&
      capy_tls_default_trust_key_material_total_bytes() ==
          CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES &&
      capy_tls_default_trust_manifest_schema_version() ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION &&
      capy_tls_default_trust_manifest_source_id() ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_SOURCE_KERNEL_BUNDLE &&
      capy_tls_default_trust_manifest_flags() ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FLAGS &&
      capy_tls_default_trust_manifest_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT &&
      capy_tls_default_trust_catalog_consistent() &&
      capy_tls_default_trust_anchor_slots_consistent() &&
      capy_tls_default_trust_anchor_descriptors_consistent() &&
      capy_tls_default_trust_anchor_bundle_consistent() &&
      capy_tls_default_trust_material_summary_consistent() &&
      capy_tls_default_trust_store_manifest_consistent() &&
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT == 106u &&
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT == 40u) PASS();
  else FAIL("default trust catalog key distribution changed");
}

static void test_tls_default_trust_catalog_identity_is_stable(void) {
  const struct capy_tls_trust_anchor_catalog *catalog;
  catalog = capy_tls_default_trust_anchor_catalog();
  TEST("capy_tls default trust catalog identity is stable");
  if (catalog &&
      catalog->key_type_mask ==
          (CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK |
           CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK) &&
      catalog->fingerprint == 0xDB22D94Au &&
      CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT == 0xDB22D94Au) PASS();
  else FAIL("default trust catalog identity changed");
}

static void test_tls_default_trust_slot_table_reports_layout(void) {
  const struct capy_tls_trust_anchor_slot *slots;
  slots = capy_tls_default_trust_anchor_slots();
  TEST("capy_tls default trust slot table reports layout");
  if (slots &&
      capy_tls_default_trust_anchor_slot_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      slots[0].index == 0 &&
      slots[0].key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK &&
      slots[0].metadata_only == 1 &&
      slots[1].index == 1 &&
      slots[1].key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK &&
      slots[2].index == 2 &&
      slots[2].key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK &&
      capy_tls_default_trust_anchor_slot_key_type(0) ==
          CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK &&
      capy_tls_default_trust_anchor_slot_key_type(2) ==
          CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK &&
      capy_tls_default_trust_anchor_slot_key_type(
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT) == 0 &&
      capy_tls_default_trust_anchor_slot_layout_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT &&
      CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT == 0x07A622ABu) PASS();
  else FAIL("default trust slot layout changed");
}

static void test_tls_default_trust_slot_table_matches_catalog(void) {
  const struct capy_tls_trust_anchor_slot *slots;
  uint32_t rsa_count = 0;
  uint32_t ec_count = 0;
  uint32_t key_type_mask = 0;
  uint32_t i;
  int ok = 1;
  slots = capy_tls_default_trust_anchor_slots();
  TEST("capy_tls default trust slot table matches catalog");
  if (!slots) ok = 0;
  for (i = 0; ok && i < CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT; i++) {
    if (slots[i].index != i || slots[i].metadata_only != 1) ok = 0;
    else if (slots[i].key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK) {
      rsa_count++;
      key_type_mask |= slots[i].key_type;
    } else if (slots[i].key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK) {
      ec_count++;
      key_type_mask |= slots[i].key_type;
    } else ok = 0;
  }
  if (ok && rsa_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT &&
      ec_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT &&
      key_type_mask == CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK &&
      capy_tls_default_trust_anchor_slots_consistent()) PASS();
  else FAIL("default trust slot table did not match catalog");
}

static void test_tls_default_trust_descriptor_reports_metadata(void) {
  struct capy_tls_trust_anchor_descriptor descriptor;
  TEST("capy_tls default trust descriptor reports metadata");
  memset(&descriptor, 0xA5, sizeof(descriptor));
  if (capy_tls_default_trust_anchor_descriptor(0, &descriptor) &&
      descriptor.index == 0 &&
      descriptor.key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK &&
      descriptor.metadata_only == 1 &&
      descriptor.flags == CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS &&
      (descriptor.flags &
       CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_METADATA_ONLY) &&
      (descriptor.flags &
       CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_SLOT_BACKED) &&
      (descriptor.flags &
       CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_KEY_TYPE_KNOWN) &&
      (descriptor.flags &
       CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_CERT_BYTES_ABSENT) &&
      capy_tls_default_trust_anchor_descriptor_count() ==
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      capy_tls_default_trust_anchor_descriptor_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT &&
      CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT == 0xE1A18A70u) PASS();
  else FAIL("default trust descriptor metadata changed");
}

static void test_tls_default_trust_descriptors_match_slots(void) {
  struct capy_tls_trust_anchor_descriptor descriptor;
  const struct capy_tls_trust_anchor_slot *slots;
  uint32_t i;
  int ok = 1;
  slots = capy_tls_default_trust_anchor_slots();
  TEST("capy_tls default trust descriptors match slots");
  if (!slots) ok = 0;
  for (i = 0; ok && i < CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT; i++) {
    if (!capy_tls_default_trust_anchor_descriptor(i, &descriptor)) ok = 0;
    else if (descriptor.index != slots[i].index ||
             descriptor.key_type != slots[i].key_type ||
             descriptor.metadata_only != slots[i].metadata_only ||
             descriptor.flags !=
                 CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS) ok = 0;
  }
  memset(&descriptor, 0xA5, sizeof(descriptor));
  if (ok && !capy_tls_default_trust_anchor_descriptor(
          CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT, &descriptor) &&
      descriptor.index == 0 && descriptor.key_type == 0 &&
      descriptor.metadata_only == 0 && descriptor.flags == 0 &&
      capy_tls_default_trust_anchor_descriptors_consistent()) PASS();
  else FAIL("default trust descriptors diverged from slots");
}

static void test_tls_default_trust_anchor_bundle_reports_material(void) {
  const struct capy_tls_trust_anchor_bundle_entry *entries;
  const struct capy_tls_trust_anchor_bundle *bundle;
  struct capy_tls_trust_anchor_bundle_entry entry;
  int ok;
  entries = capy_tls_default_trust_anchor_bundle_entries();
  bundle = capy_tls_default_trust_anchor_bundle();
  memset(&entry, 0xA5, sizeof(entry));
  TEST("capy_tls default trust bundle reports material metadata");
  ok = entries && bundle &&
       capy_tls_default_trust_anchor_bundle_entry(0, &entry) &&
       entry.index == 0 &&
       entry.key_type == CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK &&
       entry.metadata_only == 1 &&
       entry.subject_dn_bytes == 68u &&
       entry.key_material_bytes == 515u &&
       entry.flags == CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_ENTRY_FLAGS &&
       entries[120].subject_dn_bytes ==
           CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES &&
       entries[0].key_material_bytes ==
           CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES &&
       bundle->entry_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
       bundle->subject_dn_total_bytes ==
           CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES &&
       bundle->key_material_total_bytes ==
           CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES &&
       bundle->subject_dn_max_bytes ==
           CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES &&
       bundle->key_material_max_bytes ==
           CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES &&
       bundle->fingerprint ==
           CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
       bundle->flags == CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FLAGS &&
       (bundle->flags & CAPY_TLS_TRUST_ANCHOR_BUNDLE_FLAG_METADATA_ONLY) &&
       (bundle->flags &
        CAPY_TLS_TRUST_ANCHOR_BUNDLE_FLAG_KERNEL_BUNDLE_DERIVED) &&
       (bundle->flags & CAPY_TLS_TRUST_ANCHOR_BUNDLE_FLAG_CERT_BYTES_ABSENT) &&
       (bundle->flags & CAPY_TLS_TRUST_ANCHOR_BUNDLE_FLAG_FAIL_CLOSED_ONLY) &&
       capy_tls_default_trust_anchor_bundle_fingerprint() ==
           CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT &&
       capy_tls_default_trust_anchor_bundle_flags() ==
           CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FLAGS &&
       capy_tls_default_trust_anchor_bundle_consistent() &&
       CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT == 0x0ED4E969u;
  memset(&entry, 0xA5, sizeof(entry));
  ok = ok && !capy_tls_default_trust_anchor_bundle_entry(
      CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT, &entry) &&
      entry.index == 0 && entry.key_type == 0 && entry.metadata_only == 0 &&
      entry.subject_dn_bytes == 0 && entry.key_material_bytes == 0 &&
      entry.flags == 0;
  if (ok) PASS();
  else FAIL("default trust bundle material metadata changed");
}

static void test_tls_default_trust_material_summary_reports_aggregate(void) {
  const struct capy_tls_trust_material_summary *summary;
  summary = capy_tls_default_trust_material_summary();
  TEST("capy_tls default trust material summary reports aggregate sizes");
  if (summary && summary->anchor_count == CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT &&
      summary->subject_dn_total_bytes == CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES &&
      summary->key_material_total_bytes == CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES &&
      summary->subject_dn_max_bytes == CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES &&
      summary->key_material_max_bytes == CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES &&
      summary->fingerprint == CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT &&
      summary->flags == CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FLAGS &&
      summary->metadata_only == 1 &&
      capy_tls_default_trust_material_summary_flags() ==
          CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FLAGS &&
      capy_tls_default_trust_material_summary_consistent() &&
      CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT == 0x8DFC9FAFu) PASS();
  else FAIL("default trust material summary changed");
}

static void test_tls_default_trust_manifest_reports_provenance(void) {
  const struct capy_tls_trust_store_manifest *manifest;
  manifest = capy_tls_default_trust_store_manifest();
  TEST("capy_tls default trust manifest reports provenance");
  if (manifest &&
      manifest->schema_version ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION &&
      manifest->source_id ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_SOURCE_KERNEL_BUNDLE &&
      manifest->flags == CAPY_TLS_DEFAULT_TRUST_MANIFEST_FLAGS &&
      (manifest->flags & CAPY_TLS_TRUST_MANIFEST_FLAG_METADATA_ONLY) &&
      (manifest->flags &
       CAPY_TLS_TRUST_MANIFEST_FLAG_KERNEL_BUNDLE_DERIVED) &&
      (manifest->flags & CAPY_TLS_TRUST_MANIFEST_FLAG_CERT_BYTES_ABSENT) &&
      (manifest->flags & CAPY_TLS_TRUST_MANIFEST_FLAG_FAIL_CLOSED_ONLY) &&
      manifest->manifest_fingerprint ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT &&
      capy_tls_default_trust_manifest_fingerprint() ==
          CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT &&
      CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT == 0xBD2653A4u) PASS();
  else FAIL("default trust manifest provenance changed");
}

static void test_tls_default_trust_manifest_matches_catalog(void) {
  const struct capy_tls_trust_anchor_catalog *catalog;
  const struct capy_tls_trust_store_manifest *manifest;
  catalog = capy_tls_default_trust_anchor_catalog();
  manifest = capy_tls_default_trust_store_manifest();
  TEST("capy_tls default trust manifest matches catalog");
  if (catalog && manifest &&
      manifest->anchor_count == catalog->anchor_count &&
      manifest->rsa_anchor_count == catalog->rsa_anchor_count &&
      manifest->ec_anchor_count == catalog->ec_anchor_count &&
      manifest->custom_anchor_slots == catalog->custom_anchor_slots &&
      manifest->key_type_mask == catalog->key_type_mask &&
      manifest->catalog_fingerprint == catalog->fingerprint &&
      manifest->slot_layout_fingerprint ==
          catalog->slot_layout_fingerprint &&
      manifest->descriptor_fingerprint == catalog->descriptor_fingerprint &&
      manifest->anchor_bundle_fingerprint ==
          catalog->anchor_bundle_fingerprint &&
      manifest->anchor_bundle_fingerprint ==
          capy_tls_default_trust_anchor_bundle_fingerprint() &&
      manifest->material_summary_fingerprint ==
          catalog->material_summary_fingerprint &&
      manifest->subject_dn_total_bytes ==
          capy_tls_default_trust_subject_dn_total_bytes() &&
      manifest->key_material_total_bytes ==
          capy_tls_default_trust_key_material_total_bytes() &&
      manifest->subject_dn_max_bytes ==
          capy_tls_default_trust_subject_dn_max_bytes() &&
      manifest->key_material_max_bytes ==
          capy_tls_default_trust_key_material_max_bytes() &&
      capy_tls_default_trust_store_manifest_consistent()) PASS();
  else FAIL("default trust manifest diverged from catalog");
}

void test_capylibc_tls_trust_cases(void) {
  test_tls_default_trust_source_reports_bundle();
  test_tls_default_trust_catalog_key_distribution();
  test_tls_default_trust_catalog_identity_is_stable();
  test_tls_default_trust_slot_table_reports_layout();
  test_tls_default_trust_slot_table_matches_catalog();
  test_tls_default_trust_descriptor_reports_metadata();
  test_tls_default_trust_descriptors_match_slots();
  test_tls_default_trust_anchor_bundle_reports_material();
  test_tls_default_trust_material_summary_reports_aggregate();
  test_tls_default_trust_manifest_reports_provenance();
  test_tls_default_trust_manifest_matches_catalog();
}
