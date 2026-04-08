/* CAPYOS Hyper-V VMBus Synthetic Keyboard Driver */
#include <stddef.h>
#include <stdint.h>

#include "drivers/hyperv/hyperv.h"
#include "vmbus_core.h"
#include "vmbus_keyboard_internal.h"
#include "vmbus_ring.h"
#include "vmbus_transport.h"

extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree_aligned(void *ptr);
extern void fbcon_print(const char *s);
extern void fbcon_print_hex(uint64_t val);

#define KBD_VSC_SEND_RING_BUFFER_SIZE (36u * 1024u)
#define KBD_VSC_RECV_RING_BUFFER_SIZE (36u * 1024u)
#define KBD_VSC_OPEN_ID 0x48564B44u
#define KBD_VSC_GPADL_HANDLE 0x000E1E10u

static struct vmbus_keyboard g_kbd = {0};

static inline void cpu_relax(void) { __asm__ volatile("pause" ::: "memory"); }

static void keyboard_memzero(void *ptr, uint32_t len) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

static void vmbus_keyboard_reset_buffers(struct vmbus_keyboard *kbd) {
  if (!kbd) {
    return;
  }
  if (kbd->ring_buffer) {
    kfree_aligned(kbd->ring_buffer);
  }
  kbd->ring_buffer = NULL;
  kbd->ring_size = 0;
  kbd->send_ring_size = 0;
  kbd->recv_ring_size = 0;
  kbd->send_ring = NULL;
  kbd->recv_ring = NULL;
  kbd->open_id = 0;
  kbd->gpadl_handle = 0;
  kbd->protocol_accepted = 0;
  kbd->connected = 0;
  kbd->initialized = 0;
}

static int vmbus_write_inband_packet(struct vmbus_keyboard *kbd,
                                     const void *payload, uint32_t payload_len,
                                     uint16_t flags, uint64_t trans_id) {
  if (!kbd) {
    return -1;
  }
  return vmbus_write_inband_packet_runtime(
      kbd->child_relid, kbd->connection_id, kbd->monitor_id,
      kbd->monitor_allocated, kbd->is_dedicated_interrupt, kbd->send_ring,
      kbd->send_ring_size, payload, payload_len, flags, trans_id,
      vmbus_signal_relid, vmbus_signal_monitor, vmbus_signal_event);
}

static int vmbus_read_raw_packet(struct vmbus_keyboard *kbd, void *buffer,
                                 uint32_t buffer_size,
                                 uint32_t *out_packet_len) {
  if (!kbd) {
    return -1;
  }
  return vmbus_read_raw_packet_runtime(kbd->recv_ring, kbd->recv_ring_size,
                                       buffer, buffer_size, out_packet_len);
}

static void vmbus_keyboard_protocol_fill_ops(
    struct vmbus_keyboard_protocol_ops *ops) {
  if (!ops) {
    return;
  }
  ops->send_packet = vmbus_write_inband_packet;
  ops->read_packet = vmbus_read_raw_packet;
  ops->drain_transport = vmbus_transport_drain_simp;
  ops->cpu_relax = cpu_relax;
}

static void vmbus_keyboard_channel_from_device(
    const struct vmbus_keyboard *kbd, struct vmbus_channel_runtime *channel) {
  keyboard_memzero(channel, (uint32_t)sizeof(*channel));
  channel->child_relid = kbd->child_relid;
  channel->connection_id = kbd->connection_id;
  channel->monitor_id = kbd->monitor_id;
  channel->monitor_allocated = kbd->monitor_allocated;
  channel->is_dedicated_interrupt = kbd->is_dedicated_interrupt;
  channel->open_id = KBD_VSC_OPEN_ID;
  channel->gpadl_handle = KBD_VSC_GPADL_HANDLE;
  channel->send_ring_size = KBD_VSC_SEND_RING_BUFFER_SIZE;
  channel->recv_ring_size = KBD_VSC_RECV_RING_BUFFER_SIZE;
}

static void vmbus_keyboard_channel_apply(
    struct vmbus_keyboard *kbd, const struct vmbus_channel_runtime *channel) {
  kbd->open_id = channel->open_id;
  kbd->gpadl_handle = channel->gpadl_handle;
  kbd->ring_buffer = channel->ring_buffer;
  kbd->ring_size = channel->ring_size;
  kbd->send_ring_size = channel->send_ring_size;
  kbd->recv_ring_size = channel->recv_ring_size;
  kbd->send_ring = (volatile struct hv_ring_buffer *)channel->send_ring;
  kbd->recv_ring = (volatile struct hv_ring_buffer *)channel->recv_ring;
}

static int vmbus_keyboard_open_runtime(struct vmbus_keyboard *kbd) {
  struct vmbus_channel_runtime channel;

  if (!kbd) {
    return -1;
  }

  fbcon_print("[vmbus] keyboard: preparando canal runtime.\n");
  vmbus_keyboard_channel_from_device(kbd, &channel);
  fbcon_print("[vmbus] keyboard: params relid=");
  fbcon_print_hex((uint64_t)channel.child_relid);
  fbcon_print(" conn=");
  fbcon_print_hex((uint64_t)channel.connection_id);
  fbcon_print(" mon=");
  fbcon_print_hex((uint64_t)channel.monitor_id);
  fbcon_print(" alloc=");
  fbcon_print_hex((uint64_t)channel.monitor_allocated);
  fbcon_print(" openid=");
  fbcon_print_hex((uint64_t)channel.open_id);
  fbcon_print(" gpadl=");
  fbcon_print_hex((uint64_t)channel.gpadl_handle);
  fbcon_print("\n");
  if (vmbus_channel_runtime_open(&channel) != 0) {
    fbcon_print("[vmbus] keyboard: falha ao abrir canal runtime.\n");
    return -2;
  }
  vmbus_keyboard_channel_apply(kbd, &channel);
  fbcon_print("[vmbus] keyboard: canal runtime pronto.\n");
  return 0;
}

int vmbus_keyboard_init(struct vmbus_keyboard *kbd) {
  struct vmbus_keyboard_protocol_ops protocol_ops;
  int send_rc = 0;
  int wait_rc = 0;

  if (!kbd || !vmbus_runtime_connected()) {
    return -1;
  }
  if (kbd->initialized && kbd->connected) {
    return 0;
  }
  if (kbd->child_relid == 0u || kbd->connection_id == 0u) {
    return -2;
  }

  vmbus_keyboard_reset_buffers(kbd);
  if (vmbus_keyboard_open_runtime(kbd) != 0) {
    vmbus_keyboard_reset_buffers(kbd);
    return -3;
  }
  vmbus_keyboard_protocol_fill_ops(&protocol_ops);
  fbcon_print("[vmbus] keyboard: drenando mensagens pendentes SIMP.\n");
  vmbus_transport_drain_simp();
  fbcon_print("[vmbus] keyboard: enviando handshake de protocolo.\n");
  fbcon_print("[vmbus] keyboard: mantendo IRQs ativas durante handshake.\n");
  send_rc = vmbus_keyboard_protocol_send_request(kbd, &protocol_ops);
  if (send_rc != 0) {
    fbcon_print("[vmbus] keyboard: envio do handshake falhou.\n");
    fbcon_print("[vmbus] keyboard: send_rc=");
    fbcon_print_hex((uint64_t)(uint32_t)send_rc);
    fbcon_print("\n");
    vmbus_keyboard_reset_buffers(kbd);
    return -4;
  }
  fbcon_print("[vmbus] keyboard: aguardando resposta do protocolo.\n");
  wait_rc = vmbus_keyboard_protocol_wait(kbd, &protocol_ops);
  if (wait_rc != 0) {
    fbcon_print("[vmbus] keyboard: resposta do protocolo falhou.\n");
    fbcon_print("[vmbus] keyboard: wait_rc=");
    fbcon_print_hex((uint64_t)(uint32_t)wait_rc);
    fbcon_print("\n");
    vmbus_keyboard_reset_buffers(kbd);
    return -5;
  }

  kbd->initialized = 1;
  kbd->connected = 1;
  return 0;
}

int vmbus_keyboard_poll(struct vmbus_keyboard *kbd, uint8_t *scancode,
                        int *is_break, int *is_extended) {
  uint8_t packet[128];

  if (!kbd || !kbd->initialized || !kbd->connected) {
    return -1;
  }

  for (;;) {
    uint32_t packet_len = 0;
    int ret = vmbus_read_raw_packet(kbd, packet, (uint32_t)sizeof(packet),
                                    &packet_len);
    if (ret < 0) {
      vmbus_keyboard_reset_buffers(kbd);
      return -1;
    }
    if (ret == 0) {
      return 0;
    }
    ret = vmbus_keyboard_protocol_process_packet(kbd, packet, packet_len,
                                                 scancode, is_break,
                                                 is_extended);
    if (ret == 1) {
      return 1;
    }
    if (ret < 0) {
      vmbus_keyboard_reset_buffers(kbd);
      return -1;
    }
  }
}

struct vmbus_keyboard *vmbus_get_keyboard(void) {
  return (g_kbd.initialized && g_kbd.connected) ? &g_kbd : NULL;
}

int hyperv_keyboard_init(void) {
  static const struct hv_guid k_hyperv_kbd_guid = {
      .data1 = HV_KBD_GUID_DATA1,
      .data2 = HV_KBD_GUID_DATA2,
      .data3 = HV_KBD_GUID_DATA3,
      .data4 = {HV_KBD_GUID_DATA4_0, HV_KBD_GUID_DATA4_1, HV_KBD_GUID_DATA4_2,
                HV_KBD_GUID_DATA4_3, HV_KBD_GUID_DATA4_4, HV_KBD_GUID_DATA4_5,
                HV_KBD_GUID_DATA4_6, HV_KBD_GUID_DATA4_7}};
  struct vmbus_offer_info offer;

  if (!hyperv_detect()) {
    return -1;
  }
  if (g_kbd.initialized && g_kbd.connected) {
    return 0;
  }

  fbcon_print("[vmbus] === Driver VMBus Hyper-V ===\n");

  if (vmbus_runtime_connect() != 0) {
    fbcon_print("[vmbus] Falha ao conectar barramento.\n");
    return -2;
  }

  if ((g_kbd.child_relid == 0u || g_kbd.connection_id == 0u) &&
      vmbus_query_offer(&k_hyperv_kbd_guid, &offer) != 0) {
    fbcon_print("[vmbus] Teclado nao encontrado.\n");
    return -3;
  }
  if (g_kbd.child_relid == 0u || g_kbd.connection_id == 0u) {
    g_kbd.child_relid = offer.child_relid;
    g_kbd.connection_id = offer.connection_id;
    g_kbd.monitor_id = offer.monitor_id;
    g_kbd.monitor_allocated = offer.monitor_allocated;
    g_kbd.is_dedicated_interrupt = offer.is_dedicated_interrupt;
  }

  fbcon_print("[vmbus] TECLADO encontrado! relid=");
  fbcon_print_hex(g_kbd.child_relid);
  fbcon_print("\n");

  if (vmbus_keyboard_init(&g_kbd) != 0) {
    fbcon_print("[vmbus] Falha ao preparar canal do teclado.\n");
    return -4;
  }

  fbcon_print("[vmbus] Teclado VMBus configurado!\n");
  return 0;
}
