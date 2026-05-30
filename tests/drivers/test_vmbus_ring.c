#include <stdio.h>

#include "drivers/hyperv/internal/vmbus_ring.h"

static uint32_t g_signal_event_calls;
static uint32_t g_signal_event_connection;
static uint32_t g_signal_relid_calls;
static uint32_t g_signal_relid_value;

static void write_u16_le(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void write_u64_le(uint8_t *dst, uint64_t value) {
  for (uint8_t i = 0u; i < 8u; ++i) {
    dst[i] = (uint8_t)((value >> (i * 8u)) & 0xFFu);
  }
}

static uint16_t read_u16_le(const uint8_t *src) {
  return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint64_t read_u64_le(const uint8_t *src) {
  uint64_t value = 0u;
  for (uint8_t i = 0u; i < 8u; ++i) {
    value |= ((uint64_t)src[i]) << (i * 8u);
  }
  return value;
}

static void zero_bytes(void *dst, uint32_t len) {
  uint8_t *bytes = (uint8_t *)dst;
  for (uint32_t i = 0u; i < len; ++i) {
    bytes[i] = 0u;
  }
}

static void signal_relid(uint32_t relid) {
  g_signal_relid_calls++;
  g_signal_relid_value = relid;
}

static int signal_event(uint32_t connection_id) {
  g_signal_event_calls++;
  g_signal_event_connection = connection_id;
  return 0;
}

static void build_packet(uint8_t *packet, uint16_t type, uint16_t len8,
                         uint64_t trans_id) {
  zero_bytes(packet, 64u);
  write_u16_le(packet, type);
  write_u16_le(packet + 2u, 5u);
  write_u16_le(packet + 4u, len8);
  write_u16_le(packet + 6u, 1u);
  write_u64_le(packet + 8u, trans_id);
  packet[40] = 0xA5u;
}

int run_vmbus_ring_tests(void) {
  int fails = 0;
  uint8_t ring_storage[VMBUS_PAGE_SIZE + 256u];
  volatile struct hv_ring_buffer *ring =
      (volatile struct hv_ring_buffer *)ring_storage;
  uint8_t packet[64];
  uint8_t out[64];
  uint32_t out_len = 0u;
  int rc;

  zero_bytes(ring_storage, sizeof(ring_storage));
  vmbus_ring_init(ring);
  build_packet(packet, VMBUS_PKT_DATA_USING_GPA_DIRECT, 6u,
               0x1122334455667788ULL);
  g_signal_event_calls = 0u;
  g_signal_event_connection = 0u;
  g_signal_relid_calls = 0u;
  g_signal_relid_value = 0u;
  rc = vmbus_write_prebuilt_packet_runtime(
      7u, 9u, 0u, 0u, 0u, ring, sizeof(ring_storage), packet, 48u,
      signal_relid, NULL, signal_event);
  if (rc != 0 || ring->write_index != 56u || g_signal_event_calls != 1u ||
      g_signal_event_connection != 9u || g_signal_relid_calls != 1u ||
      g_signal_relid_value != 7u) {
    printf("[vmbus_ring] prebuilt write/signaling failed\n");
    fails++;
  }
  zero_bytes(out, sizeof(out));
  rc = vmbus_read_raw_packet_runtime(ring, sizeof(ring_storage), out,
                                     sizeof(out), &out_len);
  if (rc != 1 || out_len != 48u ||
      read_u16_le(out) != VMBUS_PKT_DATA_USING_GPA_DIRECT ||
      read_u16_le(out + 4u) != 6u ||
      read_u64_le(out + 8u) != 0x1122334455667788ULL ||
      out[40] != 0xA5u) {
    printf("[vmbus_ring] prebuilt readback failed\n");
    fails++;
  }

  zero_bytes(ring_storage, sizeof(ring_storage));
  vmbus_ring_init(ring);
  ring->interrupt_mask = 1u;
  g_signal_event_calls = 0u;
  rc = vmbus_write_prebuilt_packet_runtime(
      7u, 9u, 0u, 0u, 0u, ring, sizeof(ring_storage), packet, 48u,
      signal_relid, NULL, signal_event);
  if (rc != 0 || g_signal_event_calls != 0u) {
    printf("[vmbus_ring] prebuilt interrupt-mask signal suppression failed\n");
    fails++;
  }

  build_packet(packet, VMBUS_PKT_DATA_USING_GPA_DIRECT, 5u, 1u);
  if (vmbus_write_prebuilt_packet_runtime(
          7u, 9u, 0u, 0u, 0u, ring, sizeof(ring_storage), packet, 48u,
          signal_relid, NULL, signal_event) != -5) {
    printf("[vmbus_ring] prebuilt len8 mismatch was accepted\n");
    fails++;
  }

  if (vmbus_write_prebuilt_packet_runtime(
          7u, 9u, 0u, 0u, 0u, ring, sizeof(ring_storage), packet, 47u,
          signal_relid, NULL, signal_event) != -4) {
    printf("[vmbus_ring] unaligned prebuilt packet was accepted\n");
    fails++;
  }

  build_packet(packet, VMBUS_PKT_DATA_USING_GPA_DIRECT, 6u, 1u);
  write_u16_le(packet + 2u, 1u);
  if (vmbus_write_prebuilt_packet_runtime(
          7u, 9u, 0u, 0u, 0u, ring, sizeof(ring_storage), packet, 48u,
          signal_relid, NULL, signal_event) != -6) {
    printf("[vmbus_ring] invalid prebuilt payload offset was accepted\n");
    fails++;
  }

  if (fails == 0) {
    printf("[tests] vmbus_ring OK\n");
  }
  return fails;
}
