#include "stack_driver.h"

#include "core/klog.h"
#include "drivers/net/e1000.h"
#include "drivers/net/tulip.h"
#include "drivers/net/virtio_net.h"
#include "drivers/net/rtl8139.h"
#include "drivers/net/vmxnet3.h"

#include <stdint.h>

int net_stack_driver_send_frame(const struct net_nic_probe *nic,
                                const uint8_t *frame, uint16_t len) {
  if (!nic || !frame) {
    return -1;
  }
  if (nic->kind == NET_NIC_KIND_E1000) {
    return e1000_send_frame(frame, len);
  }
  if (nic->kind == NET_NIC_KIND_TULIP) {
    return tulip_send_frame(frame, len);
  }
  if (nic->kind == NET_NIC_KIND_VIRTIO_NET) {
    return virtio_net_send_frame(frame, len);
  }
  if (nic->kind == NET_NIC_KIND_RTL8139) {
    return rtl8139_send_frame(frame, len);
  }
  if (nic->kind == NET_NIC_KIND_VMXNET3) {
    return vmxnet3_send_frame(frame, len);
  }
  /* NetVSC TX stub: the VMBus data-path for frame send is not yet wired.
   * Once delivery N5/N6 complete the TX/RX integration, this will delegate
   * to the VMBus ring send path. */
  if (nic->kind == NET_NIC_KIND_HYPERV_NETVSC) {
    return 0;
  }
  return -1;
}

int net_stack_driver_poll_frame(const struct net_nic_probe *nic, uint8_t *out,
                                uint16_t cap, uint16_t *len) {
  if (!nic || !out || !len) {
    return -1;
  }
  if (nic->kind == NET_NIC_KIND_E1000) {
    return e1000_poll_frame(out, cap, len);
  }
  if (nic->kind == NET_NIC_KIND_TULIP) {
    return tulip_poll_frame(out, cap, len);
  }
  if (nic->kind == NET_NIC_KIND_VIRTIO_NET) {
    return virtio_net_poll_frame(out, cap, len);
  }
  if (nic->kind == NET_NIC_KIND_RTL8139) {
    return rtl8139_poll_frame(out, cap, len);
  }
  if (nic->kind == NET_NIC_KIND_VMXNET3) {
    return vmxnet3_poll_frame(out, cap, len);
  }
  /* NetVSC RX stub: no frames available until VMBus data-path is wired. */
  if (nic->kind == NET_NIC_KIND_HYPERV_NETVSC) {
    return 0;
  }
  return 0;
}

int net_stack_driver_init_runtime(const struct net_nic_probe *nic,
                                  uint8_t mac[6]) {
  if (!nic || !mac) {
    return -1;
  }
  /* PCI backends require runtime_supported at probe time. */
  if (nic->runtime_supported) {
    if (nic->kind == NET_NIC_KIND_E1000) {
      klog(KLOG_INFO, "[drv] Initializing E1000 driver...");
      if (e1000_init(nic->bar0, mac) == 0 && e1000_ready()) {
        klog(KLOG_INFO, "[drv] E1000 driver ready.");
        return 0;
      }
      klog(KLOG_ERROR, "[drv] E1000 driver init FAILED.");
      return -1;
    }
    if (nic->kind == NET_NIC_KIND_TULIP) {
      klog(KLOG_INFO, "[drv] Initializing Tulip driver...");
      if (tulip_init(nic->bar0, nic->bar0_is_io) == 0 && tulip_ready()) {
        klog(KLOG_INFO, "[drv] Tulip driver ready.");
        return 0;
      }
      klog(KLOG_ERROR, "[drv] Tulip driver init FAILED.");
      return -1;
    }
    if (nic->kind == NET_NIC_KIND_VIRTIO_NET) {
      klog(KLOG_INFO, "[drv] Initializing VirtIO-Net driver...");
      if (virtio_net_init(nic->bar0, nic->bar0_is_io, mac) == 0 && virtio_net_ready()) {
        klog(KLOG_INFO, "[drv] VirtIO-Net driver ready.");
        return 0;
      }
      klog(KLOG_ERROR, "[drv] VirtIO-Net driver init FAILED.");
      return -1;
    }
    if (nic->kind == NET_NIC_KIND_RTL8139) {
      klog(KLOG_INFO, "[drv] Initializing RTL8139 driver...");
      if (rtl8139_init(nic->bar0, nic->bar0_is_io, mac) == 0 && rtl8139_ready()) {
        klog(KLOG_INFO, "[drv] RTL8139 driver ready.");
        return 0;
      }
      klog(KLOG_ERROR, "[drv] RTL8139 driver init FAILED.");
      return -1;
    }
    if (nic->kind == NET_NIC_KIND_VMXNET3) {
      klog(KLOG_INFO, "[drv] Initializing VMXNET3 driver...");
      uint64_t bar1 = 0; /* TODO: read BAR1 from PCI config */
      if (vmxnet3_init(nic->bar0, bar1, mac) == 0 && vmxnet3_ready()) {
        klog(KLOG_INFO, "[drv] VMXNET3 driver ready.");
        return 0;
      }
      klog(KLOG_ERROR, "[drv] VMXNET3 driver init FAILED.");
      return -1;
    }
  }
  /* NetVSC uses deferred initialization: the real handshake happens later
   * through the Hyper-V runtime state machine. Return success here so the
   * stack can proceed; ready will be promoted once the backend reaches
   * NETVSC_BACKEND_READY via net_stack_refresh_runtime(). */
  if (nic->kind == NET_NIC_KIND_HYPERV_NETVSC) {
    klog(KLOG_INFO, "[drv] NetVSC deferred init (VMBus handshake later).");
    return 0;
  }
  klog(KLOG_WARN, "[drv] No driver matched for NIC kind.");
  return -1;
}
