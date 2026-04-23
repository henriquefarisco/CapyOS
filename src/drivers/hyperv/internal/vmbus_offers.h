#ifndef VMBUS_OFFERS_H
#define VMBUS_OFFERS_H

#include <stdint.h>

struct vmbus_offer_guid_key {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
} __attribute__((packed));

struct vmbus_offer_data {
  uint32_t child_relid;
  uint32_t connection_id;
  uint8_t monitor_id;
  uint8_t monitor_allocated;
  uint16_t is_dedicated_interrupt;
};

int vmbus_guid_matches(const struct vmbus_offer_guid_key *left,
                       const struct vmbus_offer_guid_key *right);
void vmbus_offer_info_zero(struct vmbus_offer_data *out);
void vmbus_offer_cache_reset(void);
void vmbus_offer_cache_store(const struct vmbus_offer_guid_key *guid,
                             const struct vmbus_offer_data *offer);
int vmbus_offer_cache_lookup(const struct vmbus_offer_guid_key *guid,
                             struct vmbus_offer_data *out);

#endif /* VMBUS_OFFERS_H */
