/* vmxnet3.c: VMware VMXNET3 paravirtual network driver.
 * PCI vendor=0x15AD device=0x07B0.
 * Uses two BARs: BAR0 (PT passthrough registers) and BAR1 (VD registers).
 * Implements a simplified single-queue TX/RX path using VMXNET3 gen3 shared
 * memory descriptors. */
#include "drivers/net/vmxnet3.h"
#include "drivers/io.h"
#include "kernel/log/klog.h"

#include <stddef.h>
#include <stdint.h>

/* BAR1 (VD) register offsets */
#define VMXNET3_VD_CMD        0x020
#define VMXNET3_VD_MAC_LO     0x024
#define VMXNET3_VD_MAC_HI     0x028
#define VMXNET3_VD_DEV_STATUS 0x02C
#define VMXNET3_VD_ICR        0x038
#define VMXNET3_VD_DRSS       0x040

/* BAR0 (PT) register offsets */
#define VMXNET3_PT_TXPROD     0x600
#define VMXNET3_PT_RXPROD0    0x800

/* Commands */
#define VMXNET3_CMD_ACTIVATE      0xCAFE0001u
#define VMXNET3_CMD_QUIESCE       0xCAFE0002u
#define VMXNET3_CMD_RESET         0xCAFE0003u
#define VMXNET3_CMD_GET_STATUS    0xF00D0000u
#define VMXNET3_CMD_GET_MACLO     0xF00D0001u
#define VMXNET3_CMD_GET_MACHI     0xF00D0002u
#define VMXNET3_CMD_GET_LINK      0xF00D0003u
#define VMXNET3_CMD_GET_CONF_INTR 0xF00D0008u

/* Descriptor flags */
#define VMXNET3_TXD_GEN   (1u << 14)
#define VMXNET3_TXD_EOP   (1u << 12)
#define VMXNET3_TXD_CQ    (1u << 13)
#define VMXNET3_RXD_GEN   (1u << 31)
#define VMXNET3_RXCD_GEN  (1u << 31)

#define TX_RING_SIZE  64
#define RX_RING_SIZE  64
#define TX_BUF_SIZE   2048
#define RX_BUF_SIZE   2048

struct vmxnet3_tx_desc {
  uint64_t addr;
  uint32_t len_flags;
  uint32_t csum_flags;
} __attribute__((packed));

struct vmxnet3_tx_comp {
  uint32_t index;
  uint32_t reserved[2];
  uint32_t flags;
} __attribute__((packed));

struct vmxnet3_rx_desc {
  uint64_t addr;
  uint32_t len_flags;
  uint32_t reserved;
} __attribute__((packed));

struct vmxnet3_rx_comp {
  uint32_t index;
  uint32_t rss;
  uint32_t len;
  uint32_t flags;
} __attribute__((packed));

struct vmxnet3_state {
  int initialized;
  int ready;
  volatile uint32_t *bar0;  /* PT registers (MMIO) */
  volatile uint32_t *bar1;  /* VD registers (MMIO) */
  uint8_t mac[6];

  struct vmxnet3_tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(512)));
  struct vmxnet3_tx_comp tx_comp[TX_RING_SIZE] __attribute__((aligned(512)));
  struct vmxnet3_rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(512)));
  struct vmxnet3_rx_comp rx_comp[RX_RING_SIZE] __attribute__((aligned(512)));

  uint8_t tx_bufs[TX_RING_SIZE][TX_BUF_SIZE] __attribute__((aligned(16)));
  uint8_t rx_bufs[RX_RING_SIZE][RX_BUF_SIZE] __attribute__((aligned(16)));

  uint32_t tx_prod;
  uint32_t tx_cons;
  uint32_t tx_gen;
  uint32_t rx_fill;
  uint32_t rx_comp_idx;
  uint32_t rx_gen;
  uint32_t rx_comp_gen;
};

static struct vmxnet3_state g_vmx;

static void vmx_zero(void *dst, size_t len) {
  uint8_t *p = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) p[i] = 0;
}

static void vmx_copy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < len; i++) d[i] = s[i];
}

static uint32_t vmx_vd_read(uint32_t off) {
  return *(volatile uint32_t *)((uint8_t *)g_vmx.bar1 + off);
}

static void vmx_vd_write(uint32_t off, uint32_t val) {
  *(volatile uint32_t *)((uint8_t *)g_vmx.bar1 + off) = val;
}

static void vmx_pt_write(uint32_t off, uint32_t val) {
  *(volatile uint32_t *)((uint8_t *)g_vmx.bar0 + off) = val;
}

static void vmx_populate_rx_ring(void) {
  for (uint32_t i = 0; i < RX_RING_SIZE; i++) {
    g_vmx.rx_ring[i].addr = (uint64_t)(uintptr_t)g_vmx.rx_bufs[i];
    g_vmx.rx_ring[i].len_flags = (RX_BUF_SIZE & 0x3FFFu) | VMXNET3_RXD_GEN;
    g_vmx.rx_ring[i].reserved = 0;
  }
  g_vmx.rx_fill = RX_RING_SIZE;
  g_vmx.rx_comp_idx = 0;
  g_vmx.rx_gen = 1;
  g_vmx.rx_comp_gen = 1;
}

int vmxnet3_init(uint64_t bar0, uint64_t bar1, uint8_t mac[6]) {
  uint32_t mac_lo, mac_hi;

  vmx_zero(&g_vmx, sizeof(g_vmx));

  if (!bar0 || !bar1) {
    klog(KLOG_WARN, "[vmxnet3] BAR0 or BAR1 is zero.");
    return -1;
  }

  g_vmx.bar0 = (volatile uint32_t *)(uintptr_t)bar0;
  g_vmx.bar1 = (volatile uint32_t *)(uintptr_t)bar1;

  /* Reset device */
  vmx_vd_write(VMXNET3_VD_CMD, VMXNET3_CMD_RESET);

  /* Read MAC address */
  vmx_vd_write(VMXNET3_VD_CMD, VMXNET3_CMD_GET_MACLO);
  mac_lo = vmx_vd_read(VMXNET3_VD_CMD);
  vmx_vd_write(VMXNET3_VD_CMD, VMXNET3_CMD_GET_MACHI);
  mac_hi = vmx_vd_read(VMXNET3_VD_CMD);

  g_vmx.mac[0] = (uint8_t)(mac_lo & 0xFF);
  g_vmx.mac[1] = (uint8_t)((mac_lo >> 8) & 0xFF);
  g_vmx.mac[2] = (uint8_t)((mac_lo >> 16) & 0xFF);
  g_vmx.mac[3] = (uint8_t)((mac_lo >> 24) & 0xFF);
  g_vmx.mac[4] = (uint8_t)(mac_hi & 0xFF);
  g_vmx.mac[5] = (uint8_t)((mac_hi >> 8) & 0xFF);

  if (mac) {
    vmx_copy(mac, g_vmx.mac, 6);
  }

  /* Setup rings */
  vmx_populate_rx_ring();
  g_vmx.tx_prod = 0;
  g_vmx.tx_cons = 0;
  g_vmx.tx_gen = 1;

  /* Activate device */
  vmx_vd_write(VMXNET3_VD_CMD, VMXNET3_CMD_ACTIVATE);
  uint32_t status = vmx_vd_read(VMXNET3_VD_DEV_STATUS);
  if (status == 0) {
    klog(KLOG_WARN, "[vmxnet3] Activation returned status 0 (may need shared memory setup).");
  }

  /* Notify device about RX buffers */
  vmx_pt_write(VMXNET3_PT_RXPROD0, RX_RING_SIZE - 1);

  g_vmx.initialized = 1;
  g_vmx.ready = 1;

  klog(KLOG_INFO, "[vmxnet3] Driver initialized.");
  return 0;
}

int vmxnet3_ready(void) {
  return g_vmx.initialized && g_vmx.ready;
}

int vmxnet3_send_frame(const uint8_t *frame, uint16_t len) {
  if (!g_vmx.ready || !frame || len == 0) return -1;
  if (len > TX_BUF_SIZE) return -2;

  uint32_t idx = g_vmx.tx_prod % TX_RING_SIZE;

  vmx_copy(g_vmx.tx_bufs[idx], frame, len);

  g_vmx.tx_ring[idx].addr = (uint64_t)(uintptr_t)g_vmx.tx_bufs[idx];
  g_vmx.tx_ring[idx].len_flags =
      ((uint32_t)len & 0x3FFFu) | VMXNET3_TXD_EOP | VMXNET3_TXD_CQ |
      (g_vmx.tx_gen ? VMXNET3_TXD_GEN : 0);
  g_vmx.tx_ring[idx].csum_flags = 0;

  __asm__ volatile("mfence" ::: "memory");

  g_vmx.tx_prod++;
  if ((g_vmx.tx_prod % TX_RING_SIZE) == 0) {
    g_vmx.tx_gen ^= 1;
  }

  /* Notify device */
  vmx_pt_write(VMXNET3_PT_TXPROD, g_vmx.tx_prod % TX_RING_SIZE);

  return 0;
}

int vmxnet3_poll_frame(uint8_t *out, uint16_t cap, uint16_t *len) {
  if (!g_vmx.ready || !out || !len) return -1;

  uint32_t ci = g_vmx.rx_comp_idx % RX_RING_SIZE;
  uint32_t flags = g_vmx.rx_comp[ci].flags;
  uint32_t gen = (flags & VMXNET3_RXCD_GEN) ? 1 : 0;

  if (gen != g_vmx.rx_comp_gen) {
    return 0; /* No frames */
  }

  uint32_t rx_idx = g_vmx.rx_comp[ci].index % RX_RING_SIZE;
  uint32_t frame_len = g_vmx.rx_comp[ci].len & 0x3FFFu;

  if (frame_len == 0 || frame_len > RX_BUF_SIZE) {
    g_vmx.rx_comp_idx++;
    if ((g_vmx.rx_comp_idx % RX_RING_SIZE) == 0) {
      g_vmx.rx_comp_gen ^= 1;
    }
    return 0;
  }

  uint16_t copy_len = (uint16_t)(frame_len > cap ? cap : frame_len);
  vmx_copy(out, g_vmx.rx_bufs[rx_idx], copy_len);
  *len = copy_len;

  /* Re-post RX buffer */
  g_vmx.rx_ring[rx_idx].addr = (uint64_t)(uintptr_t)g_vmx.rx_bufs[rx_idx];
  g_vmx.rx_ring[rx_idx].len_flags = (RX_BUF_SIZE & 0x3FFFu) |
      (g_vmx.rx_gen ? VMXNET3_RXD_GEN : 0);

  g_vmx.rx_comp_idx++;
  if ((g_vmx.rx_comp_idx % RX_RING_SIZE) == 0) {
    g_vmx.rx_comp_gen ^= 1;
  }

  vmx_pt_write(VMXNET3_PT_RXPROD0, rx_idx);

  return 1;
}
