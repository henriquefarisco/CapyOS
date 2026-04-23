#include "drivers/net/tulip.h"

#include <stdint.h>

#define TULIP_CSR0 0x00u
#define TULIP_CSR1 0x08u
#define TULIP_CSR2 0x10u
#define TULIP_CSR3 0x18u
#define TULIP_CSR4 0x20u
#define TULIP_CSR5 0x28u
#define TULIP_CSR6 0x30u
#define TULIP_CSR7 0x38u
#define TULIP_CSR12 0x60u
#define TULIP_CSR13 0x68u
#define TULIP_CSR14 0x70u
#define TULIP_CSR15 0x78u

#define TULIP_BUSMODE_SWR 0x00000001u

#define TULIP_OPMODE_SR (1u << 1)
#define TULIP_OPMODE_PR (1u << 6)
#define TULIP_OPMODE_PM (1u << 7)
#define TULIP_OPMODE_ST (1u << 13)
#define TULIP_OPMODE_BASE_100TX 0x82420000u

#define TULIP_DESC_OWN 0x80000000u
#define TULIP_DESC_ES 0x00008000u
#define TULIP_RDES0_FL_MASK 0x3FFF0000u
#define TULIP_RDES0_FL_SHIFT 16u

#define TULIP_TDES1_BUF1_MASK 0x000007FFu
#define TULIP_TDES1_FS (1u << 29)
#define TULIP_TDES1_LS (1u << 30)

#define TULIP_RDES1_BUF1_MASK 0x000007FFu
#define TULIP_DESC_END_RING (1u << 25)

#define TULIP_TX_DESC_COUNT 16u
#define TULIP_RX_DESC_COUNT 32u
#define TULIP_TX_BUF_SIZE 2048u
#define TULIP_RX_BUF_SIZE 2048u

struct tulip_desc {
  uint32_t status;
  uint32_t control;
  uint32_t buf1;
  uint32_t buf2;
} __attribute__((packed));

struct tulip_state {
  uint16_t io_base;
  uint16_t tx_tail;
  uint16_t rx_next;
  uint8_t ready;
};

static struct tulip_state g_tulip;

static struct tulip_desc g_tx_desc[TULIP_TX_DESC_COUNT]
    __attribute__((aligned(16)));
static struct tulip_desc g_rx_desc[TULIP_RX_DESC_COUNT]
    __attribute__((aligned(16)));

static uint8_t g_tx_buf[TULIP_TX_DESC_COUNT][TULIP_TX_BUF_SIZE]
    __attribute__((aligned(16)));
static uint8_t g_rx_buf[TULIP_RX_DESC_COUNT][TULIP_RX_BUF_SIZE]
    __attribute__((aligned(16)));

static inline void io_write32(uint16_t port, uint32_t val) {
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t io_read32(uint16_t port) {
  uint32_t ret = 0;
  __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline uint32_t reg_read32(uint16_t off) {
  return io_read32((uint16_t)(g_tulip.io_base + off));
}

static inline void reg_write32(uint16_t off, uint32_t val) {
  io_write32((uint16_t)(g_tulip.io_base + off), val);
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

static void io_delay(uint32_t loops) {
  for (volatile uint32_t i = 0; i < loops; ++i) {
    __asm__ volatile("pause");
  }
}

static void setup_descriptor_rings(void) {
  for (uint32_t i = 0; i < TULIP_TX_DESC_COUNT; ++i) {
    g_tx_desc[i].status = 0;
    g_tx_desc[i].control = (i == (TULIP_TX_DESC_COUNT - 1u)) ? TULIP_DESC_END_RING : 0u;
    g_tx_desc[i].buf1 = (uint32_t)(uintptr_t)&g_tx_buf[i][0];
    g_tx_desc[i].buf2 =
        (uint32_t)(uintptr_t)&g_tx_desc[(i + 1u) % TULIP_TX_DESC_COUNT];
  }

  for (uint32_t i = 0; i < TULIP_RX_DESC_COUNT; ++i) {
    g_rx_desc[i].status = TULIP_DESC_OWN;
    g_rx_desc[i].control = (uint32_t)(TULIP_RX_BUF_SIZE & TULIP_RDES1_BUF1_MASK);
    if (i == (TULIP_RX_DESC_COUNT - 1u)) {
      g_rx_desc[i].control |= TULIP_DESC_END_RING;
    }
    g_rx_desc[i].buf1 = (uint32_t)(uintptr_t)&g_rx_buf[i][0];
    g_rx_desc[i].buf2 =
        (uint32_t)(uintptr_t)&g_rx_desc[(i + 1u) % TULIP_RX_DESC_COUNT];
  }

  reg_write32(TULIP_CSR3, (uint32_t)(uintptr_t)&g_rx_desc[0]);
  reg_write32(TULIP_CSR4, (uint32_t)(uintptr_t)&g_tx_desc[0]);

  g_tulip.tx_tail = 0;
  g_tulip.rx_next = 0;
}

int tulip_init(uint64_t bar0, uint8_t bar0_is_io) {
  mem_zero(&g_tulip, sizeof(g_tulip));

  if (!bar0_is_io || bar0 == 0) {
    return -1;
  }
  g_tulip.io_base = (uint16_t)(bar0 & 0xFFFCu);

  reg_write32(TULIP_CSR0, TULIP_BUSMODE_SWR);
  io_delay(50000u);
  (void)reg_read32(TULIP_CSR5);

  reg_write32(TULIP_CSR7, 0);
  setup_descriptor_rings();

  /* Bring up 21143 SIA for 100base-TX (legacy Hyper-V/QEMU tulip path). */
  reg_write32(TULIP_CSR13, 0u);
  reg_write32(TULIP_CSR14, 0x0003FF7Fu);
  reg_write32(TULIP_CSR15, 0x00000008u);
  reg_write32(TULIP_CSR13, 1u);
  reg_write32(TULIP_CSR12, 0x00000301u);

  uint32_t csr6 = TULIP_OPMODE_BASE_100TX | TULIP_OPMODE_PR | TULIP_OPMODE_PM;
  reg_write32(TULIP_CSR6, csr6 | TULIP_OPMODE_SR);
  reg_write32(TULIP_CSR6, csr6 | TULIP_OPMODE_SR | TULIP_OPMODE_ST);
  reg_write32(TULIP_CSR2, 1u);
  reg_write32(TULIP_CSR1, 1u);

  g_tulip.ready = 1;
  return 0;
}

int tulip_ready(void) { return g_tulip.ready ? 1 : 0; }

int tulip_send_frame(const uint8_t *frame, uint16_t len) {
  if (!g_tulip.ready || !frame || len == 0u || len > TULIP_TX_BUF_SIZE) {
    return -1;
  }

  uint16_t idx = g_tulip.tx_tail;
  struct tulip_desc *desc = &g_tx_desc[idx];
  if (desc->status & TULIP_DESC_OWN) {
    return -1;
  }

  uint16_t wire_len = len;
  if (wire_len < 60u) {
    wire_len = 60u;
  }

  mem_copy(&g_tx_buf[idx][0], frame, len);
  if (wire_len > len) {
    mem_zero(&g_tx_buf[idx][len], (uint32_t)(wire_len - len));
  }

  uint32_t ctrl = (uint32_t)(wire_len & TULIP_TDES1_BUF1_MASK) |
                  TULIP_TDES1_FS | TULIP_TDES1_LS;
  if (idx == (TULIP_TX_DESC_COUNT - 1u)) {
    ctrl |= TULIP_DESC_END_RING;
  }
  desc->control = ctrl;
  __asm__ volatile("" ::: "memory");
  desc->status = TULIP_DESC_OWN;
  __asm__ volatile("" ::: "memory");

  reg_write32(TULIP_CSR1, 1u);

  for (uint32_t spin = 0; spin < 300000u; ++spin) {
    if ((desc->status & TULIP_DESC_OWN) == 0u) {
      g_tulip.tx_tail = (uint16_t)((idx + 1u) % TULIP_TX_DESC_COUNT);
      if (desc->status & TULIP_DESC_ES) {
        return -1;
      }
      return 0;
    }
    __asm__ volatile("pause");
  }

  return -1;
}

static int pull_rx_frame(uint8_t *out_frame, uint16_t out_cap, uint16_t *out_len) {
  struct tulip_desc *desc = &g_rx_desc[g_tulip.rx_next];
  if (desc->status & TULIP_DESC_OWN) {
    return 0;
  }

  uint32_t status = desc->status;
  uint16_t idx = g_tulip.rx_next;
  g_tulip.rx_next = (uint16_t)((g_tulip.rx_next + 1u) % TULIP_RX_DESC_COUNT);

  uint16_t frame_len = 0;
  if ((status & TULIP_DESC_ES) == 0u) {
    uint16_t reported_len =
        (uint16_t)((status & TULIP_RDES0_FL_MASK) >> TULIP_RDES0_FL_SHIFT);
    if (reported_len >= 4u) {
      reported_len = (uint16_t)(reported_len - 4u);
    }
    if (reported_len > 0u) {
      frame_len = (reported_len > out_cap) ? out_cap : reported_len;
      mem_copy(out_frame, &g_rx_buf[idx][0], frame_len);
    }
  }

  desc->status = TULIP_DESC_OWN;
  reg_write32(TULIP_CSR2, 1u);

  if (frame_len == 0u) {
    return -1;
  }
  *out_len = frame_len;
  return 1;
}

int tulip_poll_frame(uint8_t *out_frame, uint16_t out_cap, uint16_t *out_len) {
  if (!g_tulip.ready || !out_frame || !out_len || out_cap == 0u) {
    return -1;
  }

  for (uint32_t attempts = 0; attempts < TULIP_RX_DESC_COUNT; ++attempts) {
    int rc = pull_rx_frame(out_frame, out_cap, out_len);
    if (rc == 1) {
      return 1;
    }
    if (rc == 0) {
      return 0;
    }
  }
  return 0;
}
