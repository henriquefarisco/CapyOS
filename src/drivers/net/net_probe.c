#include "drivers/net/net_probe.h"

#include "kernel/log/klog.h"
#include "drivers/hyperv/hyperv.h"
#include "drivers/pcie.h"

#define PCI_CLASS_NETWORK 0x02u

static void probe_zero(struct net_nic_probe *out) {
  if (!out) {
    return;
  }
  out->found = 0;
  out->kind = NET_NIC_KIND_UNKNOWN;
  out->runtime_supported = 0;
  out->bus = 0;
  out->device = 0;
  out->function = 0;
  out->vendor_id = 0;
  out->device_id = 0;
  out->mtu = 1500;
  out->bar0 = 0;
  out->bar0_is_io = 0;
  out->vmbus_relid = 0;
  out->vmbus_connection_id = 0;
  out->vmbus_dedicated_interrupt = 0;
  for (int i = 0; i < 6; ++i) {
    out->mac[i] = 0;
  }
}

static uint8_t match_nic_kind(uint16_t vendor, uint16_t device) {
  if (vendor == 0x8086u) {
    if (device == 0x100Eu || device == 0x100Fu || device == 0x10D3u ||
        device == 0x150Cu) {
      return NET_NIC_KIND_E1000;
    }
  }
  if (vendor == 0x10ECu && device == 0x8139u) {
    return NET_NIC_KIND_RTL8139;
  }
  if (vendor == 0x1AF4u && (device == 0x1000u || device == 0x1041u)) {
    return NET_NIC_KIND_VIRTIO_NET;
  }
  if (vendor == 0x1414u && device == 0x0001u) {
    return NET_NIC_KIND_HYPERV_NETVSC;
  }
  if (vendor == 0x1011u && (device == 0x0019u || device == 0x0009u)) {
    return NET_NIC_KIND_TULIP;
  }
  if (vendor == 0x15ADu && device == 0x07B0u) {
    return NET_NIC_KIND_VMXNET3;
  }
  return NET_NIC_KIND_UNKNOWN;
}

static void probe_set_fallback_mac(struct net_nic_probe *out) {
  if (!out) {
    return;
  }
  /* Locally administered, deterministic fallback MAC for bootstrap logs/tests.
   */
  out->mac[0] = 0x02u;
  out->mac[1] = 0xCAu;
  out->mac[2] = (uint8_t)(out->vendor_id & 0xFFu);
  out->mac[3] = (uint8_t)(out->device_id & 0xFFu);
  out->mac[4] = out->bus;
  out->mac[5] = out->device;
}

static void probe_fill_hyperv_fallback(struct net_nic_probe *out) {
  if (!out) {
    return;
  }
  probe_zero(out);
  out->found = 1;
  out->kind = NET_NIC_KIND_HYPERV_NETVSC;
  out->runtime_supported = 1;
  out->vendor_id = 0x1414u;
  out->device_id = 0x0001u;
  out->mtu = 1500;
  /* Do not negotiate VMBus offers from the early boot probe. Hyper-V is
   * sensitive to repeated contact/offer handshakes here, and the real NetVSC
   * backend will perform controlled channel discovery later in its own init.
   */
  probe_set_fallback_mac(out);
}

int net_probe_kind_runtime_supported(uint8_t kind) {
  switch (kind) {
  case NET_NIC_KIND_E1000:
  case NET_NIC_KIND_TULIP:
  case NET_NIC_KIND_HYPERV_NETVSC:
  case NET_NIC_KIND_VIRTIO_NET:
  case NET_NIC_KIND_RTL8139:
  case NET_NIC_KIND_VMXNET3:
    return 1;
  default:
    return 0;
  }
}

static void probe_fill_candidate(struct net_nic_probe *out, uint16_t vendor,
                                 uint16_t device_id, uint16_t bus,
                                 uint8_t dev, uint8_t func) {
  uint32_t bar0_raw = 0;

  probe_zero(out);
  out->found = 1;
  out->kind = match_nic_kind(vendor, device_id);
  out->runtime_supported = (uint8_t)net_probe_kind_runtime_supported(out->kind);
  out->bus = (uint8_t)bus;
  out->device = dev;
  out->function = func;
  out->vendor_id = vendor;
  out->device_id = device_id;
  out->mtu = 1500;
  bar0_raw = pci_config_read32((uint8_t)bus, dev, func, PCI_BAR0);
  out->bar0_is_io = (bar0_raw & 0x1u) ? 1u : 0u;
  out->bar0 = out->bar0_is_io ? (uint64_t)(bar0_raw & ~0x3u)
                              : pci_read_bar64((uint8_t)bus, dev, func, 0);
  probe_set_fallback_mac(out);
}

static void probe_enable_selected_device(const struct net_nic_probe *nic) {
  uint16_t cmd = 0;

  if (!nic || !nic->found || !nic->runtime_supported) {
    return;
  }
  cmd = pci_config_read16(nic->bus, nic->device, nic->function, PCI_COMMAND);
  cmd |=
      (uint16_t)(PCI_CMD_IO_SPACE | PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER);
  pci_config_write16(nic->bus, nic->device, nic->function, PCI_COMMAND, cmd);
}

int net_probe_first_supported(struct net_nic_probe *out) {
  struct net_nic_probe fallback;
  int have_fallback = 0;

  if (!out) {
    return -1;
  }
  probe_zero(out);
  probe_zero(&fallback);
  pci_init();
  klog(KLOG_INFO, "[probe] PCI bus scan starting...");

  for (uint16_t bus = 0; bus < PCI_MAX_BUS; ++bus) {
    for (uint8_t dev = 0; dev < PCI_MAX_DEV; ++dev) {
      for (uint8_t func = 0; func < PCI_MAX_FUNC; ++func) {
        uint16_t vendor = pci_config_read16((uint8_t)bus, dev, func, PCI_VENDOR_ID);
        if (vendor == 0xFFFFu || vendor == 0x0000u) {
          if (func == 0u) {
            break;
          }
          continue;
        }

        uint32_t class_rev =
            pci_config_read32((uint8_t)bus, dev, func, PCI_CLASS_REVISION);
        uint8_t class_code = (uint8_t)(class_rev >> 24);
        if (class_code != PCI_CLASS_NETWORK) {
          if (func == 0u) {
            uint8_t header =
                pci_config_read8((uint8_t)bus, dev, func, PCI_HEADER_TYPE);
            if ((header & 0x80u) == 0u) {
              break;
            }
          }
          continue;
        }

        uint16_t device_id =
            pci_config_read16((uint8_t)bus, dev, func, PCI_DEVICE_ID);
        uint8_t kind = match_nic_kind(vendor, device_id);
        if (kind != NET_NIC_KIND_UNKNOWN) {
          struct net_nic_probe candidate;
          probe_fill_candidate(&candidate, vendor, device_id, bus, dev, func);
          if (candidate.runtime_supported) {
            *out = candidate;
            probe_enable_selected_device(out);
            return 0;
          }
          if (!have_fallback) {
            fallback = candidate;
            have_fallback = 1;
          }
        }

        if (func == 0u) {
          uint8_t header =
              pci_config_read8((uint8_t)bus, dev, func, PCI_HEADER_TYPE);
          if ((header & 0x80u) == 0u) {
            break;
          }
        }
      }
    }
  }

  if (have_fallback) {
    *out = fallback;
    return 0;
  }
  if (hyperv_detect()) {
    probe_fill_hyperv_fallback(out);
    return 0;
  }
  return -1;
}

const char *net_probe_kind_name(uint8_t kind) {
  switch (kind) {
  case NET_NIC_KIND_E1000:
    return "e1000";
  case NET_NIC_KIND_RTL8139:
    return "rtl8139";
  case NET_NIC_KIND_VIRTIO_NET:
    return "virtio-net";
  case NET_NIC_KIND_HYPERV_NETVSC:
    return "hyperv-netvsc";
  case NET_NIC_KIND_TULIP:
    return "tulip-2114x";
  case NET_NIC_KIND_VMXNET3:
    return "vmxnet3";
  default:
    return "unknown";
  }
}
