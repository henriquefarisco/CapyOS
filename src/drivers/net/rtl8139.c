/* rtl8139.c: Realtek RTL8139 network driver.
 * Common in QEMU with -net nic,model=rtl8139.
 * Implements basic TX (4 descriptors) and RX (ring buffer). */
#include "drivers/net/rtl8139.h"
#include "drivers/io.h"
#include "core/klog.h"

#include <stddef.h>
#include <stdint.h>

/* RTL8139 register offsets */
#define RTL_IDR0        0x00
#define RTL_MAR0        0x08
#define RTL_TSD0        0x10
#define RTL_TSAD0       0x20
#define RTL_RBSTART     0x30
#define RTL_CMD         0x37
#define RTL_CAPR        0x38
#define RTL_IMR         0x3C
#define RTL_ISR         0x3E
#define RTL_TCR         0x40
#define RTL_RCR         0x44
#define RTL_CONFIG1     0x52

/* CMD bits */
#define RTL_CMD_RST     0x10
#define RTL_CMD_RE      0x08
#define RTL_CMD_TE      0x04

/* RCR bits */
#define RTL_RCR_AAP     (1u << 0)  /* Accept All Packets */
#define RTL_RCR_APM     (1u << 1)  /* Accept Physical Match */
#define RTL_RCR_AM      (1u << 2)  /* Accept Multicast */
#define RTL_RCR_AB      (1u << 3)  /* Accept Broadcast */
#define RTL_RCR_WRAP    (1u << 7)  /* Wrap around */

/* TSD bits */
#define RTL_TSD_OWN     (1u << 13)
#define RTL_TSD_TOK     (1u << 15)

#define RX_BUF_SIZE     (8192 + 16 + 1500)
#define TX_BUF_SIZE     2048
#define TX_DESC_COUNT   4

struct rtl8139_state {
  int initialized;
  int ready;
  uint16_t io_base;
  uint8_t mac[6];
  uint8_t rx_buf[RX_BUF_SIZE] __attribute__((aligned(16)));
  uint8_t tx_bufs[TX_DESC_COUNT][TX_BUF_SIZE] __attribute__((aligned(16)));
  uint16_t rx_offset;
  uint8_t tx_cur;
};

static struct rtl8139_state g_rtl;

static void rtl_zero(void *dst, size_t len) {
  uint8_t *p = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) p[i] = 0;
}

static void rtl_copy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < len; i++) d[i] = s[i];
}

static uint8_t rtl_r8(uint16_t off) { return inb(g_rtl.io_base + off); }
static uint16_t rtl_r16(uint16_t off) { return inw(g_rtl.io_base + off); }
static uint32_t rtl_r32(uint16_t off) { return inl(g_rtl.io_base + off); }
static void rtl_w8(uint16_t off, uint8_t v) { outb(g_rtl.io_base + off, v); }
static void rtl_w16(uint16_t off, uint16_t v) { outw(g_rtl.io_base + off, v); }
static void rtl_w32(uint16_t off, uint32_t v) { outl(g_rtl.io_base + off, v); }

int rtl8139_init(uint64_t bar0, int is_io, uint8_t mac[6]) {
  rtl_zero(&g_rtl, sizeof(g_rtl));

  if (!is_io) {
    klog(KLOG_WARN, "[rtl8139] Only I/O-port BAR supported.");
    return -1;
  }
  g_rtl.io_base = (uint16_t)(bar0 & 0xFFFFu);

  /* Power on */
  rtl_w8(RTL_CONFIG1, 0x00);

  /* Software reset */
  rtl_w8(RTL_CMD, RTL_CMD_RST);
  for (int i = 0; i < 100000; i++) {
    if (!(rtl_r8(RTL_CMD) & RTL_CMD_RST)) break;
    __asm__ volatile("pause");
  }
  if (rtl_r8(RTL_CMD) & RTL_CMD_RST) {
    klog(KLOG_ERROR, "[rtl8139] Reset timeout.");
    return -1;
  }

  /* Read MAC address */
  for (int i = 0; i < 6; i++) {
    g_rtl.mac[i] = rtl_r8((uint16_t)(RTL_IDR0 + i));
  }
  if (mac) {
    rtl_copy(mac, g_rtl.mac, 6);
  }

  /* Set RX buffer address */
  rtl_w32(RTL_RBSTART, (uint32_t)(uintptr_t)g_rtl.rx_buf);

  /* Enable interrupts (or mask all — we poll) */
  rtl_w16(RTL_IMR, 0x0000);

  /* Configure RX: accept broadcast + physical match, wrap mode */
  rtl_w32(RTL_RCR, RTL_RCR_APM | RTL_RCR_AB | RTL_RCR_AM | RTL_RCR_WRAP);

  /* Enable TX and RX */
  rtl_w8(RTL_CMD, RTL_CMD_RE | RTL_CMD_TE);

  /* Setup TX descriptors */
  for (int i = 0; i < TX_DESC_COUNT; i++) {
    rtl_w32((uint16_t)(RTL_TSAD0 + i * 4),
            (uint32_t)(uintptr_t)g_rtl.tx_bufs[i]);
  }

  g_rtl.rx_offset = 0;
  g_rtl.tx_cur = 0;
  g_rtl.initialized = 1;
  g_rtl.ready = 1;

  klog(KLOG_INFO, "[rtl8139] Driver initialized and ready.");
  return 0;
}

int rtl8139_ready(void) {
  return g_rtl.initialized && g_rtl.ready;
}

int rtl8139_send_frame(const uint8_t *frame, uint16_t len) {
  if (!g_rtl.ready || !frame || len == 0) return -1;
  if (len > TX_BUF_SIZE) return -2;

  uint8_t desc = g_rtl.tx_cur;
  uint16_t tsd_off = (uint16_t)(RTL_TSD0 + desc * 4);

  /* Wait for descriptor to be free */
  for (int i = 0; i < 100000; i++) {
    uint32_t tsd = rtl_r32(tsd_off);
    if (tsd & RTL_TSD_OWN) break;
    if (tsd & RTL_TSD_TOK) break;
    __asm__ volatile("pause");
  }

  rtl_copy(g_rtl.tx_bufs[desc], frame, len);
  rtl_w32(tsd_off, (uint32_t)len);

  g_rtl.tx_cur = (uint8_t)((desc + 1) % TX_DESC_COUNT);
  return 0;
}

int rtl8139_poll_frame(uint8_t *out, uint16_t cap, uint16_t *len) {
  if (!g_rtl.ready || !out || !len) return -1;

  /* Check if RX buffer is empty */
  uint8_t cmd = rtl_r8(RTL_CMD);
  if (cmd & 0x01) {
    return 0; /* Buffer empty */
  }

  /* Read packet header: status (2 bytes) + length (2 bytes) */
  uint16_t off = g_rtl.rx_offset;
  uint16_t status = *(uint16_t *)(g_rtl.rx_buf + off);
  uint16_t pkt_len = *(uint16_t *)(g_rtl.rx_buf + off + 2);

  if (!(status & 0x01)) {
    return 0; /* Packet not ready */
  }
  if (pkt_len < 4 || pkt_len > 1600) {
    /* Bad packet, skip it */
    g_rtl.rx_offset = (uint16_t)((off + pkt_len + 4 + 3) & ~3u);
    rtl_w16(RTL_CAPR, (uint16_t)(g_rtl.rx_offset - 16));
    return 0;
  }

  /* Strip CRC (4 bytes) */
  uint16_t data_len = (uint16_t)(pkt_len - 4);
  if (data_len > cap) data_len = cap;

  rtl_copy(out, g_rtl.rx_buf + off + 4, data_len);
  *len = data_len;

  /* Advance RX pointer (4-byte aligned) */
  g_rtl.rx_offset = (uint16_t)((off + pkt_len + 4 + 3) & ~3u);
  if (g_rtl.rx_offset >= 8192) {
    g_rtl.rx_offset -= 8192;
  }
  rtl_w16(RTL_CAPR, (uint16_t)(g_rtl.rx_offset - 16));

  return 1;
}
