#include "internal/vmbus_offers.h"

#include <stdint.h>

#define VMBUS_OFFER_CACHE_CAPACITY 32u

struct vmbus_offer_cache_entry {
  uint8_t valid;
  struct vmbus_offer_guid_key guid;
  struct vmbus_offer_data offer;
};

static struct vmbus_offer_cache_entry g_offer_cache[VMBUS_OFFER_CACHE_CAPACITY];

static void offer_memzero(void *ptr, uint32_t len) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

int vmbus_guid_matches(const struct vmbus_offer_guid_key *left,
                       const struct vmbus_offer_guid_key *right) {
  if (!left || !right) {
    return 0;
  }
  if (left->data1 != right->data1 || left->data2 != right->data2 ||
      left->data3 != right->data3) {
    return 0;
  }
  for (uint32_t i = 0; i < 8u; ++i) {
    if (left->data4[i] != right->data4[i]) {
      return 0;
    }
  }
  return 1;
}

void vmbus_offer_info_zero(struct vmbus_offer_data *out) {
  if (!out) {
    return;
  }
  out->child_relid = 0u;
  out->connection_id = 0u;
  out->monitor_id = 0u;
  out->monitor_allocated = 0u;
  out->is_dedicated_interrupt = 0u;
}

void vmbus_offer_cache_reset(void) {
  offer_memzero(g_offer_cache, (uint32_t)sizeof(g_offer_cache));
}

void vmbus_offer_cache_store(const struct vmbus_offer_guid_key *guid,
                             const struct vmbus_offer_data *offer) {
  uint32_t slot = VMBUS_OFFER_CACHE_CAPACITY;

  if (!guid || !offer) {
    return;
  }

  for (uint32_t i = 0; i < VMBUS_OFFER_CACHE_CAPACITY; ++i) {
    if (g_offer_cache[i].valid &&
        vmbus_guid_matches(guid, &g_offer_cache[i].guid)) {
      slot = i;
      break;
    }
    if (!g_offer_cache[i].valid && slot == VMBUS_OFFER_CACHE_CAPACITY) {
      slot = i;
    }
  }

  if (slot == VMBUS_OFFER_CACHE_CAPACITY) {
    slot = 0u;
  }

  g_offer_cache[slot].valid = 1u;
  g_offer_cache[slot].guid = *guid;
  g_offer_cache[slot].offer = *offer;
}

int vmbus_offer_cache_lookup(const struct vmbus_offer_guid_key *guid,
                             struct vmbus_offer_data *out) {
  if (!guid || !out) {
    return -1;
  }

  for (uint32_t i = 0; i < VMBUS_OFFER_CACHE_CAPACITY; ++i) {
    if (g_offer_cache[i].valid &&
        vmbus_guid_matches(&g_offer_cache[i].guid, guid)) {
      *out = g_offer_cache[i].offer;
      return 0;
    }
  }
  return -1;
}
