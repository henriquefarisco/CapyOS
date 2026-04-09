#ifndef DRIVERS_NET_NET_PROBE_H
#define DRIVERS_NET_NET_PROBE_H

#include <stdint.h>

enum net_nic_kind {
  NET_NIC_KIND_UNKNOWN = 0,
  NET_NIC_KIND_E1000 = 1,
  NET_NIC_KIND_RTL8139 = 2,
  NET_NIC_KIND_VIRTIO_NET = 3,
  NET_NIC_KIND_HYPERV_NETVSC = 4,
  NET_NIC_KIND_TULIP = 5,
  NET_NIC_KIND_VMXNET3 = 6,
};

struct net_nic_probe {
  uint8_t found;
  uint8_t kind;
  uint8_t runtime_supported;
  uint8_t bus;
  uint8_t device;
  uint8_t function;
  uint16_t vendor_id;
  uint16_t device_id;
  uint16_t mtu;
  uint64_t bar0;
  uint8_t bar0_is_io;
  uint32_t vmbus_relid;
  uint32_t vmbus_connection_id;
  uint16_t vmbus_dedicated_interrupt;
  uint8_t mac[6];
};

int net_probe_first_supported(struct net_nic_probe *out);
int net_probe_kind_runtime_supported(uint8_t kind);
const char *net_probe_kind_name(uint8_t kind);

#endif /* DRIVERS_NET_NET_PROBE_H */
