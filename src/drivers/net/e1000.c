#include "drivers/net/e1000.h"

#include <stdint.h>

#define E1000_REG_CTRL 0x0000u
#define E1000_REG_STATUS 0x0008u
#define E1000_REG_IMC 0x00D8u
#define E1000_REG_RCTL 0x0100u
#define E1000_REG_TCTL 0x0400u
#define E1000_REG_TIPG 0x0410u

#define E1000_REG_RDBAL 0x2800u
#define E1000_REG_RDBAH 0x2804u
#define E1000_REG_RDLEN 0x2808u
#define E1000_REG_RDH 0x2810u
#define E1000_REG_RDT 0x2818u

#define E1000_REG_TDBAL 0x3800u
#define E1000_REG_TDBAH 0x3804u
#define E1000_REG_TDLEN 0x3808u
#define E1000_REG_TDH 0x3810u
#define E1000_REG_TDT 0x3818u

#define E1000_REG_RAL0 0x5400u
#define E1000_REG_RAH0 0x5404u

#define E1000_CTRL_RST (1u << 26)

#define E1000_RCTL_EN (1u << 1)
#define E1000_RCTL_BAM (1u << 15)
#define E1000_RCTL_SECRC (1u << 26)

#define E1000_TCTL_EN (1u << 1)
#define E1000_TCTL_PSP (1u << 3)

#define E1000_TX_CMD_EOP (1u << 0)
#define E1000_TX_CMD_IFCS (1u << 1)
#define E1000_TX_CMD_RS (1u << 3)

#define E1000_TX_STATUS_DD (1u << 0)
#define E1000_RX_STATUS_DD (1u << 0)

#define E1000_TX_DESC_COUNT 16u
#define E1000_RX_DESC_COUNT 32u
#define E1000_TX_BUF_SIZE 2048u
#define E1000_RX_BUF_SIZE 2048u

struct e1000_tx_desc {
  uint64_t addr;
  uint16_t length;
  uint8_t cso;
  uint8_t cmd;
  uint8_t status;
  uint8_t css;
  uint16_t special;
} __attribute__((packed));

struct e1000_rx_desc {
  uint64_t addr;
  uint16_t length;
  uint16_t checksum;
  uint8_t status;
  uint8_t errors;
  uint16_t special;
} __attribute__((packed));

struct e1000_state {
  volatile uint8_t *mmio;
  uint16_t tx_tail;
  uint16_t rx_next;
  uint8_t ready;
};

static struct e1000_state g_e1000;

static struct e1000_tx_desc g_tx_desc[E1000_TX_DESC_COUNT]
    __attribute__((aligned(16)));
static struct e1000_rx_desc g_rx_desc[E1000_RX_DESC_COUNT]
    __attribute__((aligned(16)));
static uint8_t g_tx_buf[E1000_TX_DESC_COUNT][E1000_TX_BUF_SIZE]
    __attribute__((aligned(16)));
static uint8_t g_rx_buf[E1000_RX_DESC_COUNT][E1000_RX_BUF_SIZE]
    __attribute__((aligned(16)));

static inline uint32_t mmio_read32(uint32_t off) {
  return *(volatile uint32_t *)(g_e1000.mmio + off);
}

static inline void mmio_write32(uint32_t off, uint32_t val) {
  *(volatile uint32_t *)(g_e1000.mmio + off) = val;
  __asm__ volatile("" ::: "memory");
}

static void mem_zero(void *dst, uint32_t len) {
  uint8_t *p = (uint8_t *)dst;
  for (uint32_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

static void mem_copy(void *dst, const void *src, uint32_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (uint32_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static void io_relax_delay(uint32_t loops) {
  for (volatile uint32_t i = 0; i < loops; ++i) {
    __asm__ volatile("pause");
  }
}

static int mac_nonzero(const uint8_t mac[6]) {
  uint8_t any = 0;
  for (int i = 0; i < 6; ++i) {
    any |= mac[i];
  }
  return any != 0;
}

static void e1000_setup_rings(void) {
  for (uint32_t i = 0; i < E1000_TX_DESC_COUNT; ++i) {
    g_tx_desc[i].addr = (uint64_t)(uintptr_t)&g_tx_buf[i][0];
    g_tx_desc[i].length = 0;
    g_tx_desc[i].cso = 0;
    g_tx_desc[i].cmd = 0;
    g_tx_desc[i].status = E1000_TX_STATUS_DD;
    g_tx_desc[i].css = 0;
    g_tx_desc[i].special = 0;
  }

  for (uint32_t i = 0; i < E1000_RX_DESC_COUNT; ++i) {
    g_rx_desc[i].addr = (uint64_t)(uintptr_t)&g_rx_buf[i][0];
    g_rx_desc[i].length = 0;
    g_rx_desc[i].checksum = 0;
    g_rx_desc[i].status = 0;
    g_rx_desc[i].errors = 0;
    g_rx_desc[i].special = 0;
  }

  mmio_write32(E1000_REG_TDBAL, (uint32_t)(uintptr_t)&g_tx_desc[0]);
  mmio_write32(E1000_REG_TDBAH, 0);
  mmio_write32(E1000_REG_TDLEN, E1000_TX_DESC_COUNT * sizeof(g_tx_desc[0]));
  mmio_write32(E1000_REG_TDH, 0);
  mmio_write32(E1000_REG_TDT, 0);

  mmio_write32(E1000_REG_RDBAL, (uint32_t)(uintptr_t)&g_rx_desc[0]);
  mmio_write32(E1000_REG_RDBAH, 0);
  mmio_write32(E1000_REG_RDLEN, E1000_RX_DESC_COUNT * sizeof(g_rx_desc[0]));
  mmio_write32(E1000_REG_RDH, 0);
  mmio_write32(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1u);

  g_e1000.tx_tail = 0;
  g_e1000.rx_next = 0;
}

int e1000_init(uint64_t bar0, uint8_t mac_out[6]) {
  mem_zero(&g_e1000, sizeof(g_e1000));
  if (bar0 == 0) {
    return -1;
  }
  g_e1000.mmio = (volatile uint8_t *)(uintptr_t)(bar0 & ~0xFull);

  mmio_write32(E1000_REG_IMC, 0xFFFFFFFFu);

  uint32_t ctrl = mmio_read32(E1000_REG_CTRL);
  mmio_write32(E1000_REG_CTRL, ctrl | E1000_CTRL_RST);
  io_relax_delay(50000u);
  mmio_write32(E1000_REG_IMC, 0xFFFFFFFFu);

  e1000_setup_rings();

  uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC;
  mmio_write32(E1000_REG_RCTL, rctl);

  uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (0x10u << 4) | (0x40u << 12);
  mmio_write32(E1000_REG_TCTL, tctl);
  mmio_write32(E1000_REG_TIPG, 0x0060200Au);

  if (mac_out) {
    uint8_t mac[6];
    uint32_t ral = mmio_read32(E1000_REG_RAL0);
    uint32_t rah = mmio_read32(E1000_REG_RAH0);
    mac[0] = (uint8_t)(ral & 0xFFu);
    mac[1] = (uint8_t)((ral >> 8) & 0xFFu);
    mac[2] = (uint8_t)((ral >> 16) & 0xFFu);
    mac[3] = (uint8_t)((ral >> 24) & 0xFFu);
    mac[4] = (uint8_t)(rah & 0xFFu);
    mac[5] = (uint8_t)((rah >> 8) & 0xFFu);
    if (mac_nonzero(mac)) {
      for (uint32_t i = 0; i < 6; ++i) {
        mac_out[i] = mac[i];
      }
    }
  }

  g_e1000.ready = 1;
  return 0;
}

int e1000_ready(void) { return g_e1000.ready ? 1 : 0; }

int e1000_send_frame(const uint8_t *frame, uint16_t len) {
  if (!g_e1000.ready || !frame || len == 0u || len > E1000_TX_BUF_SIZE) {
    return -1;
  }

  uint16_t idx = g_e1000.tx_tail;
  struct e1000_tx_desc *desc = &g_tx_desc[idx];
  if ((desc->status & E1000_TX_STATUS_DD) == 0u) {
    return -1;
  }

  mem_copy(&g_tx_buf[idx][0], frame, len);
  desc->length = len;
  desc->cso = 0;
  desc->cmd = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
  desc->status = 0;
  desc->css = 0;
  desc->special = 0;
  __asm__ volatile("" ::: "memory");

  g_e1000.tx_tail = (uint16_t)((idx + 1u) % E1000_TX_DESC_COUNT);
  mmio_write32(E1000_REG_TDT, g_e1000.tx_tail);

  for (uint32_t spin = 0; spin < 200000u; ++spin) {
    if (desc->status & E1000_TX_STATUS_DD) {
      return 0;
    }
    __asm__ volatile("pause");
  }
  return -1;
}

int e1000_poll_frame(uint8_t *out_frame, uint16_t out_cap, uint16_t *out_len) {
  if (!g_e1000.ready || !out_frame || !out_len) {
    return -1;
  }

  struct e1000_rx_desc *desc = &g_rx_desc[g_e1000.rx_next];
  if ((desc->status & E1000_RX_STATUS_DD) == 0u) {
    return 0;
  }

  uint16_t len = desc->length;
  if (len > out_cap) {
    len = out_cap;
  }
  mem_copy(out_frame, &g_rx_buf[g_e1000.rx_next][0], len);
  *out_len = len;

  desc->status = 0;
  mmio_write32(E1000_REG_RDT, g_e1000.rx_next);
  g_e1000.rx_next = (uint16_t)((g_e1000.rx_next + 1u) % E1000_RX_DESC_COUNT);
  return 1;
}
