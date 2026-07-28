#include "boot/boot_slot_block_provider.h"

#include "boot/gpt_identity.h"
#include "internal/boot_slot_internal.h"

struct provider_lease_context {
  struct block_device raw;
  struct boot_slot_layout layout;
  uint32_t boot_lba;
  uint32_t boot_sectors;
  boot_slot_block_flush_fn flusher;
  void *flush_ctx;
  uint64_t registration_epoch;
  uint64_t lease_epoch;
  int active;
};

struct provider_registration {
  struct boot_slot_block_provider *owner;
  struct block_device raw;
  struct capyos_gpt_identity identity;
  struct boot_slot_layout layout;
  boot_slot_block_flush_fn flusher;
  void *flush_ctx;
  uint64_t epoch;
  int active;
};

static struct provider_registration provider_registration;
static struct provider_lease_context provider_lease_context;
static struct boot_slot_block_provider *provider_leased;
static struct boot_slot_store *provider_leased_store;
static uint64_t provider_lease_epoch;
static uint64_t provider_next_registration_epoch = 1u;
static uint64_t provider_next_lease_epoch = 1u;

static void provider_zero(void *ptr, size_t len) {
  uint8_t *bytes = ptr;
  if (!bytes)
    return;
  for (size_t i = 0u; i < len; ++i)
    bytes[i] = 0u;
}

static int provider_equal(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t diff = 0u;
  if (!a || !b)
    return 0;
  for (size_t i = 0u; i < len; ++i)
    diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0u;
}

static int provider_raw_read(void *ctx, uint32_t lba,
                             uint8_t sector[CAPYOS_GPT_IDENTITY_SECTOR_SIZE]) {
  struct block_device *raw = ctx;
  return raw ? block_device_read(raw, lba, sector)
             : BOOT_SLOT_BLOCK_PROVIDER_ERR;
}

static struct provider_lease_context *provider_active_lease(void *ctx) {
  uint64_t epoch = (uint64_t)(uintptr_t)ctx;
  if (epoch == 0u || epoch != provider_lease_epoch ||
      !provider_lease_context.active ||
      provider_lease_context.lease_epoch != epoch ||
      !provider_registration.active ||
      provider_lease_context.registration_epoch != provider_registration.epoch)
    return NULL;
  return &provider_lease_context;
}

static int provider_store_read(void *ctx, uint32_t lba,
                               uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE]) {
  struct provider_lease_context *lease = provider_active_lease(ctx);
  if (!lease || !sector || lba >= lease->boot_sectors ||
      lease->boot_lba > UINT32_MAX - lba)
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  return block_device_read(&lease->raw, lease->boot_lba + lba, sector);
}

static int provider_store_write(
    void *ctx, uint32_t lba,
    const uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE]) {
  struct provider_lease_context *lease = provider_active_lease(ctx);
  if (!lease || !sector || lba >= lease->boot_sectors ||
      lease->boot_lba > UINT32_MAX - lba)
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  return block_device_write(&lease->raw, lease->boot_lba + lba, sector);
}

static int provider_store_flush(void *ctx) {
  struct provider_lease_context *lease = provider_active_lease(ctx);
  if (!lease || !lease->flusher)
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  return lease->flusher(lease->flush_ctx);
}

static int provider_init_locked(
    struct boot_slot_block_provider *provider, struct block_device *raw,
    const struct boot_slot_disk_binding *binding,
    boot_slot_block_flush_fn flusher, void *flush_ctx,
    uint64_t *out_registration_epoch) {
  struct capyos_gpt_identity identity;
  struct boot_slot_layout layout;
  if (out_registration_epoch)
    *out_registration_epoch = 0u;
  if (!provider || !out_registration_epoch || provider_leased ||
      provider_registration.active || provider_next_registration_epoch == 0u)
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  provider_zero(provider, sizeof(*provider));
  if (!raw || !binding || !flusher || !raw->ops ||
      (!raw->ops->read_block && !raw->ops->read_block_ex) ||
      (!raw->ops->write_block && !raw->ops->write_block_ex) ||
      binding->esp_sectors == 0u || binding->boot_sectors == 0u ||
      binding->data_sectors == 0u ||
      capyos_gpt_identity_read(provider_raw_read, raw, raw->block_size,
                               raw->block_count, &identity) != 0 ||
      identity.esp.lba != binding->esp_lba ||
      identity.esp.sectors != binding->esp_sectors ||
      identity.boot.lba != binding->boot_lba ||
      identity.boot.sectors != binding->boot_sectors ||
      identity.data.lba != binding->data_lba ||
      identity.data.sectors != binding->data_sectors ||
      !provider_equal(identity.disk_guid, binding->disk_guid, 16u) ||
      !provider_equal(identity.esp.guid, binding->esp_guid, 16u) ||
      !provider_equal(identity.boot.guid, binding->boot_guid, 16u) ||
      !provider_equal(identity.data.guid, binding->data_guid, 16u) ||
      boot_slot_layout_plan(identity.boot.sectors, &layout) != 0)
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  provider_zero(&provider_registration, sizeof(provider_registration));
  provider_registration.owner = provider;
  provider_registration.raw = *raw;
  provider_registration.identity = identity;
  provider_registration.layout = layout;
  provider_registration.flusher = flusher;
  provider_registration.flush_ctx = flush_ctx;
  provider_registration.epoch = provider_next_registration_epoch++;
  provider_registration.active = 1;
  provider->opaque_epoch = provider_registration.epoch;
  *out_registration_epoch = provider_registration.epoch;
  return 0;
}

#ifdef UNIT_TEST
int boot_slot_block_provider_init(
    struct boot_slot_block_provider *provider, struct block_device *raw,
    const struct boot_slot_disk_binding *binding,
    boot_slot_block_flush_fn flusher, void *flush_ctx,
    uint64_t *out_registration_epoch) {
  int rc = boot_slot_internal_operation_begin();
  if (rc != 0) {
    if (out_registration_epoch)
      *out_registration_epoch = 0u;
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  }
  rc = provider_init_locked(provider, raw, binding, flusher, flush_ctx,
                            out_registration_epoch);
  boot_slot_internal_operation_end();
  return rc;
}

#endif
static int provider_block_device_flush(void *ctx) {
  return block_device_flush((struct block_device *)ctx);
}

int boot_slot_block_provider_init_from_block_device(
    struct boot_slot_block_provider *provider, struct block_device *raw,
    const struct boot_slot_disk_binding *binding,
    uint64_t *out_registration_epoch) {
  int rc = boot_slot_internal_operation_begin();
  if (rc != 0) {
    if (out_registration_epoch)
      *out_registration_epoch = 0u;
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  }
  if (!block_device_supports_flush(raw)) {
    if (out_registration_epoch)
      *out_registration_epoch = 0u;
    rc = BOOT_SLOT_BLOCK_PROVIDER_ERR;
  } else {
    rc = provider_init_locked(provider, raw, binding,
                              provider_block_device_flush,
                              &provider_registration.raw,
                              out_registration_epoch);
  }
  boot_slot_internal_operation_end();
  return rc;
}

int boot_slot_block_provider_unregister(
    struct boot_slot_block_provider *provider, uint64_t registration_epoch) {
  int rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  if (!provider || registration_epoch == 0u || provider_leased ||
      !provider_registration.active || provider_registration.owner != provider ||
      provider_registration.epoch != registration_epoch ||
      provider->opaque_epoch != registration_epoch) {
    rc = BOOT_SLOT_BLOCK_PROVIDER_ERR;
  } else {
    provider_zero(&provider_registration, sizeof(provider_registration));
    provider_zero(provider, sizeof(*provider));
    rc = 0;
  }
  boot_slot_internal_operation_end();
  return rc;
}

static int provider_identity_equal(
    const struct capyos_gpt_identity *a,
    const struct capyos_gpt_identity *b) {
  return a && b && a->disk_sectors == b->disk_sectors &&
         a->esp.lba == b->esp.lba && a->esp.sectors == b->esp.sectors &&
         a->boot.lba == b->boot.lba && a->boot.sectors == b->boot.sectors &&
         a->data.lba == b->data.lba && a->data.sectors == b->data.sectors &&
         provider_equal(a->disk_guid, b->disk_guid, 16u) &&
         provider_equal(a->esp.guid, b->esp.guid, 16u) &&
         provider_equal(a->boot.guid, b->boot.guid, 16u) &&
         provider_equal(a->data.guid, b->data.guid, 16u);
}

static int provider_revalidate_locked(
    const struct boot_slot_block_provider *provider,
    uint64_t registration_epoch) {
  struct capyos_gpt_identity identity;
  if (!provider || registration_epoch == 0u ||
      provider->opaque_epoch != registration_epoch ||
      !provider_registration.active || provider_registration.owner != provider ||
      provider_registration.epoch != registration_epoch ||
      capyos_gpt_identity_read(provider_raw_read, &provider_registration.raw,
                               provider_registration.raw.block_size,
                               provider_registration.raw.block_count,
                               &identity) != 0 ||
      !provider_identity_equal(&identity, &provider_registration.identity))
    return 0;
  return 1;
}

int boot_slot_block_provider_open_store(
    struct boot_slot_block_provider *provider, uint64_t registration_epoch,
    struct boot_slot_store *store, uint64_t *out_lease_epoch) {
  uint64_t lease_epoch = 0u;
  void *callback_ctx = NULL;
  int rc = boot_slot_internal_operation_begin();
  if (out_lease_epoch)
    *out_lease_epoch = 0u;
  if (rc != 0)
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  lease_epoch = provider_next_lease_epoch;
  if (!store || !out_lease_epoch || provider_leased || bsp.configured ||
      bsp.lease_owner || lease_epoch == 0u || lease_epoch > UINTPTR_MAX ||
      !provider_revalidate_locked(provider, registration_epoch)) {
    boot_slot_internal_operation_end();
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  }
  callback_ctx = (void *)(uintptr_t)lease_epoch;
  if (!callback_ctx || (uint64_t)(uintptr_t)callback_ctx != lease_epoch) {
    boot_slot_internal_operation_end();
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  }
  provider_next_lease_epoch++;
  boot_slot_internal_init_locked();
  provider_zero(&provider_lease_context, sizeof(provider_lease_context));
  provider_lease_context.raw = provider_registration.raw;
  provider_lease_context.layout = provider_registration.layout;
  provider_lease_context.boot_lba = provider_registration.identity.boot.lba;
  provider_lease_context.boot_sectors =
      provider_registration.identity.boot.sectors;
  provider_lease_context.flusher = provider_registration.flusher;
  provider_lease_context.flush_ctx = provider_registration.flush_ctx;
  provider_lease_context.registration_epoch = registration_epoch;
  provider_lease_context.lease_epoch = lease_epoch;
  rc = boot_slot_store_internal_open_locked(
      store, &provider_lease_context.layout, provider_store_read,
      provider_store_write, provider_store_flush, callback_ctx, lease_epoch);
  if (rc == 0) {
    provider_leased = provider;
    provider_leased_store = store;
    bsp.lease_owner = store;
    bsp.lease_epoch = lease_epoch;
    boot_slot_phase = BOOT_SLOT_PHASE_LEASED_UNBOUND;
    provider_lease_epoch = lease_epoch;
    provider_lease_context.active = 1;
    *out_lease_epoch = lease_epoch;
  } else {
    boot_slot_phase = BOOT_SLOT_PHASE_CLOSED;
    provider_zero(&provider_lease_context, sizeof(provider_lease_context));
    provider_zero(store, sizeof(*store));
  }
  boot_slot_internal_operation_end();
  return rc == 0 ? 0 : BOOT_SLOT_BLOCK_PROVIDER_ERR;
}

#ifdef UNIT_TEST
int boot_slot_block_provider_test_capture_io(
    struct boot_slot_block_provider_test_io *out) {
  int rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  if (!out || !provider_lease_context.active || provider_lease_epoch == 0u) {
    rc = BOOT_SLOT_BLOCK_PROVIDER_ERR;
  } else {
    out->reader = provider_store_read;
    out->writer = provider_store_write;
    out->flusher = provider_store_flush;
    out->ctx = (void *)(uintptr_t)provider_lease_epoch;
    rc = 0;
  }
  boot_slot_internal_operation_end();
  return rc;
}
#endif

int boot_slot_block_provider_close_store(
    struct boot_slot_block_provider *provider, uint64_t registration_epoch,
    struct boot_slot_store *store, uint64_t lease_epoch) {
  int rc = boot_slot_internal_operation_begin();
  if (rc != 0)
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  if (!provider || !store || registration_epoch == 0u || lease_epoch == 0u ||
      provider_leased != provider || provider_leased_store != store ||
      provider_lease_epoch != lease_epoch ||
      !provider_registration.active || provider_registration.owner != provider ||
      provider_registration.epoch != registration_epoch ||
      provider->opaque_epoch != registration_epoch ||
      !provider_lease_context.active ||
      provider_lease_context.registration_epoch != registration_epoch ||
      provider_lease_context.lease_epoch != lease_epoch ||
      store->opaque_epoch != lease_epoch || bsp.stage_claimed ||
      bsp.lease_owner != store || bsp.lease_epoch != lease_epoch) {
    boot_slot_internal_operation_end();
    return BOOT_SLOT_BLOCK_PROVIDER_ERR;
  }
  provider_lease_context.active = 0;
  provider_lease_epoch = 0u;
  boot_slot_internal_init_locked();
  rc = boot_slot_store_internal_revoke_locked(store, lease_epoch);
  if (rc == 0) {
    provider_leased = NULL;
    provider_leased_store = NULL;
    provider_lease_epoch = 0u;
    provider_zero(&provider_lease_context, sizeof(provider_lease_context));
    bsp.lease_owner = NULL;
    bsp.lease_epoch = 0u;
    boot_slot_phase = BOOT_SLOT_PHASE_CLOSED;
  }
  boot_slot_internal_operation_end();
  return rc == 0 ? 0 : BOOT_SLOT_BLOCK_PROVIDER_ERR;
}
