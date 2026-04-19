/* virtio_net.c: VirtIO network driver for QEMU/KVM.
 * Implements legacy (transitional) VirtIO over PCI with split virtqueues.
 * Supports VirtIO device IDs 0x1000 (legacy) and 0x1041 (modern/transitional). */
#include "drivers/net/virtio_net.h"
#include "drivers/io.h"
#include "kernel/log/klog.h"

#include <stddef.h>
#include <stdint.h>

/* VirtIO PCI legacy I/O registers (offsets from BAR0 when BAR0 is I/O) */
#define VIRTIO_PCI_HOST_FEATURES    0x00
#define VIRTIO_PCI_GUEST_FEATURES   0x04
#define VIRTIO_PCI_QUEUE_PFN        0x08
#define VIRTIO_PCI_QUEUE_NUM        0x0C
#define VIRTIO_PCI_QUEUE_SEL        0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY     0x10
#define VIRTIO_PCI_STATUS           0x12
#define VIRTIO_PCI_ISR              0x13
#define VIRTIO_PCI_MAC_BASE         0x14

/* VirtIO device status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_DRIVER_OK     0x04
#define VIRTIO_STATUS_FEATURES_OK   0x08
#define VIRTIO_STATUS_FAILED        0x80

/* VirtIO net feature bits */
#define VIRTIO_NET_F_MAC            (1u << 5)
#define VIRTIO_NET_F_STATUS         (1u << 16)

/* Virtqueue descriptor flags */
#define VRING_DESC_F_NEXT           1
#define VRING_DESC_F_WRITE          2

#define VIRTIO_RX_QUEUE  0
#define VIRTIO_TX_QUEUE  1

#define QUEUE_SIZE       64
#define RX_BUF_SIZE      2048
#define TX_BUF_SIZE      2048
#define VIRTIO_NET_HDR_SIZE 10

struct vring_desc {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed));

struct vring_avail {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[QUEUE_SIZE];
} __attribute__((packed));

struct vring_used_elem {
  uint32_t id;
  uint32_t len;
} __attribute__((packed));

struct vring_used {
  uint16_t flags;
  uint16_t idx;
  struct vring_used_elem ring[QUEUE_SIZE];
} __attribute__((packed));

struct virtqueue {
  struct vring_desc desc[QUEUE_SIZE] __attribute__((aligned(16)));
  struct vring_avail avail __attribute__((aligned(2)));
  uint8_t _pad[4096];
  struct vring_used used __attribute__((aligned(4096)));
};

struct virtio_net_state {
  int initialized;
  int ready;
  uint16_t io_base;
  int is_mmio;
  uint64_t mmio_base;
  uint8_t mac[6];

  struct virtqueue rxq __attribute__((aligned(4096)));
  struct virtqueue txq __attribute__((aligned(4096)));

  uint8_t rx_bufs[QUEUE_SIZE][RX_BUF_SIZE] __attribute__((aligned(16)));
  uint8_t tx_bufs[QUEUE_SIZE][TX_BUF_SIZE] __attribute__((aligned(16)));

  uint16_t rx_last_used;
  uint16_t tx_next;
  uint16_t tx_last_used;
};

static struct virtio_net_state g_vnet;

static void vnet_zero(void *dst, size_t len) {
  uint8_t *p = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) p[i] = 0;
}

static void vnet_copy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < len; i++) d[i] = s[i];
}

/* I/O accessors for legacy VirtIO PCI (I/O port based) */
static uint8_t vnet_read8(uint16_t off) {
  return inb(g_vnet.io_base + off);
}
static uint16_t vnet_read16(uint16_t off) {
  return inw(g_vnet.io_base + off);
}
static uint32_t vnet_read32(uint16_t off) {
  return inl(g_vnet.io_base + off);
}
static void vnet_write8(uint16_t off, uint8_t val) {
  outb(g_vnet.io_base + off, val);
}
static void vnet_write16(uint16_t off, uint16_t val) {
  outw(g_vnet.io_base + off, val);
}
static void vnet_write32(uint16_t off, uint32_t val) {
  outl(g_vnet.io_base + off, val);
}

static int setup_virtqueue(int qidx, struct virtqueue *vq) {
  vnet_zero(vq, sizeof(*vq));
  vnet_write16(VIRTIO_PCI_QUEUE_SEL, (uint16_t)qidx);
  uint16_t qsz = vnet_read16(VIRTIO_PCI_QUEUE_NUM);
  if (qsz == 0 || qsz > QUEUE_SIZE) {
    klog(KLOG_WARN, "[virtio-net] Queue size unsupported or zero.");
    return -1;
  }
  uint64_t phys = (uint64_t)(uintptr_t)vq;
  vnet_write32(VIRTIO_PCI_QUEUE_PFN, (uint32_t)(phys >> 12));
  return 0;
}

static void populate_rx_buffers(void) {
  for (uint16_t i = 0; i < QUEUE_SIZE; i++) {
    g_vnet.rxq.desc[i].addr = (uint64_t)(uintptr_t)g_vnet.rx_bufs[i];
    g_vnet.rxq.desc[i].len = RX_BUF_SIZE;
    g_vnet.rxq.desc[i].flags = VRING_DESC_F_WRITE;
    g_vnet.rxq.desc[i].next = 0;
    g_vnet.rxq.avail.ring[i] = i;
  }
  g_vnet.rxq.avail.idx = QUEUE_SIZE;
  g_vnet.rx_last_used = 0;
  /* Notify device that RX buffers are available */
  vnet_write16(VIRTIO_PCI_QUEUE_NOTIFY, VIRTIO_RX_QUEUE);
}

int virtio_net_init(uint64_t bar0, int is_io, uint8_t mac[6]) {
  uint8_t status = 0;
  uint32_t features;

  vnet_zero(&g_vnet, sizeof(g_vnet));

  if (!is_io) {
    klog(KLOG_WARN, "[virtio-net] Only I/O-port BAR supported for legacy VirtIO.");
    return -1;
  }
  g_vnet.io_base = (uint16_t)(bar0 & 0xFFFFu);
  g_vnet.is_mmio = 0;

  /* Reset device */
  vnet_write8(VIRTIO_PCI_STATUS, 0);
  status = VIRTIO_STATUS_ACKNOWLEDGE;
  vnet_write8(VIRTIO_PCI_STATUS, status);
  status |= VIRTIO_STATUS_DRIVER;
  vnet_write8(VIRTIO_PCI_STATUS, status);

  /* Read and negotiate features */
  features = vnet_read32(VIRTIO_PCI_HOST_FEATURES);
  uint32_t guest_features = 0;
  if (features & VIRTIO_NET_F_MAC) {
    guest_features |= VIRTIO_NET_F_MAC;
  }
  vnet_write32(VIRTIO_PCI_GUEST_FEATURES, guest_features);

  /* Read MAC address */
  if (features & VIRTIO_NET_F_MAC) {
    for (int i = 0; i < 6; i++) {
      g_vnet.mac[i] = vnet_read8((uint16_t)(VIRTIO_PCI_MAC_BASE + i));
    }
    if (mac) {
      vnet_copy(mac, g_vnet.mac, 6);
    }
    klog(KLOG_INFO, "[virtio-net] MAC read from device.");
  } else if (mac) {
    vnet_copy(g_vnet.mac, mac, 6);
  }

  /* Setup RX and TX virtqueues */
  if (setup_virtqueue(VIRTIO_RX_QUEUE, &g_vnet.rxq) != 0) {
    vnet_write8(VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
    return -1;
  }
  if (setup_virtqueue(VIRTIO_TX_QUEUE, &g_vnet.txq) != 0) {
    vnet_write8(VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
    return -1;
  }

  /* Tell device we're ready */
  status |= VIRTIO_STATUS_DRIVER_OK;
  vnet_write8(VIRTIO_PCI_STATUS, status);

  /* Populate RX ring with buffers */
  populate_rx_buffers();

  g_vnet.tx_next = 0;
  g_vnet.tx_last_used = 0;
  g_vnet.initialized = 1;
  g_vnet.ready = 1;

  klog(KLOG_INFO, "[virtio-net] Driver initialized and ready.");
  return 0;
}

int virtio_net_ready(void) {
  return g_vnet.initialized && g_vnet.ready;
}

int virtio_net_send_frame(const uint8_t *frame, uint16_t len) {
  if (!g_vnet.ready || !frame || len == 0) return -1;
  if (len + VIRTIO_NET_HDR_SIZE > TX_BUF_SIZE) return -2;

  uint16_t idx = g_vnet.tx_next % QUEUE_SIZE;

  /* Prepend virtio-net header (all zeros for simple TX) */
  vnet_zero(g_vnet.tx_bufs[idx], VIRTIO_NET_HDR_SIZE);
  vnet_copy(g_vnet.tx_bufs[idx] + VIRTIO_NET_HDR_SIZE, frame, len);

  g_vnet.txq.desc[idx].addr = (uint64_t)(uintptr_t)g_vnet.tx_bufs[idx];
  g_vnet.txq.desc[idx].len = (uint32_t)(len + VIRTIO_NET_HDR_SIZE);
  g_vnet.txq.desc[idx].flags = 0;
  g_vnet.txq.desc[idx].next = 0;

  uint16_t avail_idx = g_vnet.txq.avail.idx % QUEUE_SIZE;
  g_vnet.txq.avail.ring[avail_idx] = idx;
  __asm__ volatile("mfence" ::: "memory");
  g_vnet.txq.avail.idx++;
  vnet_write16(VIRTIO_PCI_QUEUE_NOTIFY, VIRTIO_TX_QUEUE);

  g_vnet.tx_next++;
  return 0;
}

int virtio_net_poll_frame(uint8_t *out, uint16_t cap, uint16_t *len) {
  if (!g_vnet.ready || !out || !len) return -1;

  uint16_t used_idx = g_vnet.rxq.used.idx;
  if (g_vnet.rx_last_used == used_idx) {
    return 0; /* No frames available */
  }

  uint16_t ring_idx = g_vnet.rx_last_used % QUEUE_SIZE;
  uint32_t desc_idx = g_vnet.rxq.used.ring[ring_idx].id;
  uint32_t frame_len = g_vnet.rxq.used.ring[ring_idx].len;

  if (desc_idx >= QUEUE_SIZE) {
    g_vnet.rx_last_used++;
    return -1;
  }

  /* Skip virtio-net header */
  if (frame_len <= VIRTIO_NET_HDR_SIZE) {
    g_vnet.rx_last_used++;
    /* Re-post buffer */
    g_vnet.rxq.avail.ring[g_vnet.rxq.avail.idx % QUEUE_SIZE] = (uint16_t)desc_idx;
    g_vnet.rxq.avail.idx++;
    vnet_write16(VIRTIO_PCI_QUEUE_NOTIFY, VIRTIO_RX_QUEUE);
    return 0;
  }

  uint32_t payload_len = frame_len - VIRTIO_NET_HDR_SIZE;
  if (payload_len > cap) payload_len = cap;
  vnet_copy(out, g_vnet.rx_bufs[desc_idx] + VIRTIO_NET_HDR_SIZE, payload_len);
  *len = (uint16_t)payload_len;

  g_vnet.rx_last_used++;

  /* Re-post the buffer to the RX ring */
  g_vnet.rxq.desc[desc_idx].addr = (uint64_t)(uintptr_t)g_vnet.rx_bufs[desc_idx];
  g_vnet.rxq.desc[desc_idx].len = RX_BUF_SIZE;
  g_vnet.rxq.desc[desc_idx].flags = VRING_DESC_F_WRITE;
  g_vnet.rxq.avail.ring[g_vnet.rxq.avail.idx % QUEUE_SIZE] = (uint16_t)desc_idx;
  g_vnet.rxq.avail.idx++;
  vnet_write16(VIRTIO_PCI_QUEUE_NOTIFY, VIRTIO_RX_QUEUE);

  return 1;
}
