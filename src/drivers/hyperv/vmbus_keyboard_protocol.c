#include "arch/x86_64/framebuffer_console.h"
#include <stddef.h>
#include <stdint.h>

#ifndef UNIT_TEST
#include "arch/x86_64/timebase.h"
#endif
#include "drivers/hyperv/hyperv.h"
#include "internal/vmbus_keyboard_internal.h"

#ifndef UNIT_TEST
#endif

static void protocol_memcpy(void *dst, const void *src, uint32_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (!d || !s) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static void protocol_log(const char *s) {
#ifndef UNIT_TEST
  fbcon_print(s);
#else
  (void)s;
#endif
}

static void protocol_log_hex(uint64_t value) {
#ifndef UNIT_TEST
  fbcon_print_hex(value);
#else
  (void)value;
#endif
}

struct vmbus_keyboard_packet_desc {
  uint16_t type;
  uint16_t offset8;
  uint16_t len8;
  uint16_t flags;
  uint64_t trans_id;
} __attribute__((packed));

static uint64_t protocol_now_ticks_100hz(void) {
#ifndef UNIT_TEST
  return x64_timebase_ticks_100hz();
#else
  static uint64_t fake_ticks = 0;
  return fake_ticks++;
#endif
}

static void protocol_log_packet_desc(const struct vmbus_keyboard_packet_desc *desc,
                                     uint32_t packet_len) {
  if (!desc) {
    return;
  }
  protocol_log("[vmbus] keyboard: recv desc type=");
  protocol_log_hex((uint64_t)desc->type);
  protocol_log(" flags=");
  protocol_log_hex((uint64_t)desc->flags);
  protocol_log(" trans=");
  protocol_log_hex(desc->trans_id);
  protocol_log(" off8=");
  protocol_log_hex((uint64_t)desc->offset8);
  protocol_log(" len8=");
  protocol_log_hex((uint64_t)desc->len8);
  protocol_log(" packet_len=");
  protocol_log_hex((uint64_t)packet_len);
  protocol_log("\n");
}

int vmbus_keyboard_protocol_process_packet(struct vmbus_keyboard *kbd,
                                          const uint8_t *packet,
                                          uint32_t packet_len,
                                          uint8_t *scancode, int *is_break,
                                          int *is_extended) {
  struct vmbus_keyboard_packet_desc desc;
  const uint8_t *payload = NULL;
  uint32_t payload_len = 0;
  uint32_t msgtype = 0;
  int rc = 0;

  if (!kbd || !packet) {
    return 0;
  }

  if (packet_len >= (uint32_t)sizeof(desc)) {
    protocol_memcpy(&desc, packet, (uint32_t)sizeof(desc));
    protocol_log_packet_desc(&desc, packet_len);
    if (desc.type == VMBUS_PKT_COMP) {
      protocol_log("[vmbus] keyboard: recv completion packet; aguardando payload inband.\n");
      return 0;
    }
  } else {
    protocol_log("[vmbus] keyboard: recv pacote curto demais.\n");
    return 0;
  }

  rc = vmbus_packet_extract_inband(packet, packet_len, &payload, &payload_len);
  if (rc <= 0) {
    protocol_log("[vmbus] keyboard: extract rc=");
    protocol_log_hex((uint64_t)(uint32_t)rc);
    protocol_log("\n");
    return rc < 0 ? 0 : rc;
  }
  if (payload_len < sizeof(uint32_t)) {
    protocol_log("[vmbus] keyboard: payload insuficiente len=");
    protocol_log_hex((uint64_t)payload_len);
    protocol_log("\n");
    return 0;
  }

  protocol_memcpy(&msgtype, payload, sizeof(msgtype));
  protocol_log("[vmbus] keyboard: msgtype=");
  protocol_log_hex((uint64_t)msgtype);
  protocol_log(" payload_len=");
  protocol_log_hex((uint64_t)payload_len);
  protocol_log("\n");
  if (msgtype == SYNTH_KBD_PROTOCOL_RESPONSE) {
    struct synth_kbd_protocol_response_msg response;
    if (payload_len < (uint32_t)sizeof(response)) {
      protocol_log("[vmbus] keyboard: protocol response curta.\n");
      return 0;
    }
    protocol_memcpy(&response, payload, (uint32_t)sizeof(response));
    protocol_log("[vmbus] keyboard: protocol status=");
    protocol_log_hex((uint64_t)response.proto_status);
    protocol_log("\n");
    kbd->protocol_accepted =
        (response.proto_status & SYNTH_KBD_PROTOCOL_ACCEPTED) ? 1 : 0;
    kbd->connected = kbd->protocol_accepted;
    return kbd->protocol_accepted ? 2 : -1;
  }

  if (msgtype == SYNTH_KBD_EVENT) {
    struct synth_kbd_keystroke_msg event;
    if (payload_len < (uint32_t)sizeof(event)) {
      return 0;
    }
    protocol_memcpy(&event, payload, (uint32_t)sizeof(event));
    if (scancode) {
      *scancode = (uint8_t)(event.make_code & 0xFFu);
    }
    if (is_break) {
      *is_break = (event.info & SYNTH_KBD_INFO_BREAK) ? 1 : 0;
    }
    if (is_extended) {
      *is_extended = (event.info & SYNTH_KBD_INFO_E0) ? 1 : 0;
    }
    return 1;
  }

  return 0;
}

int vmbus_keyboard_protocol_send_request(
    struct vmbus_keyboard *kbd, const struct vmbus_keyboard_protocol_ops *ops) {
  struct synth_kbd_protocol_request_msg request;
  uint64_t trans_id = 0;
  int rc = 0;

  if (!kbd || !ops || !ops->send_packet) {
    return -1;
  }

  request.type = SYNTH_KBD_PROTOCOL_REQUEST;
  request.version_requested = SYNTH_KBD_VERSION;
  trans_id = (uint64_t)(uintptr_t)&request;
  protocol_log("[vmbus] keyboard: protocol request type=");
  protocol_log_hex((uint64_t)request.type);
  protocol_log(" version=");
  protocol_log_hex((uint64_t)request.version_requested);
  protocol_log(" trans=");
  protocol_log_hex(trans_id);
  protocol_log("\n");
  rc = ops->send_packet(kbd, &request, (uint32_t)sizeof(request),
                        VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED,
                        trans_id);
  protocol_log("[vmbus] keyboard: protocol send rc=");
  protocol_log_hex((uint64_t)(uint32_t)rc);
  protocol_log("\n");
  return rc;
}

int vmbus_keyboard_protocol_wait(struct vmbus_keyboard *kbd,
                                 const struct vmbus_keyboard_protocol_ops *ops) {
  uint8_t packet[128];
  uint64_t start_tick = 0;
  uint64_t now_tick = 0;
  uint64_t next_log_tick = 0;

  enum {
    SYNTH_KBD_PROTOCOL_WAIT_TICKS = 200u,
    SYNTH_KBD_PROTOCOL_LOG_INTERVAL_TICKS = 100u,
  };

  if (!kbd || !ops || !ops->read_packet) {
    return -1;
  }

  start_tick = protocol_now_ticks_100hz();
  next_log_tick = start_tick + SYNTH_KBD_PROTOCOL_LOG_INTERVAL_TICKS;

  for (;;) {
    uint32_t packet_len = 0;
    int ret = ops->read_packet(kbd, packet, sizeof(packet), &packet_len);
    if (ret < 0) {
      protocol_log("[vmbus] keyboard: read_packet rc=");
      protocol_log_hex((uint64_t)(uint32_t)ret);
      protocol_log("\n");
      return -2;
    }
    if (ret == 0) {
      if (ops->drain_transport) {
        ops->drain_transport();
      }
      now_tick = protocol_now_ticks_100hz();
      if ((now_tick - start_tick) >= SYNTH_KBD_PROTOCOL_WAIT_TICKS) {
        protocol_log("[vmbus] keyboard: timeout aguardando protocolo ticks=");
        protocol_log_hex(now_tick - start_tick);
        protocol_log("\n");
        return -4;
      }
      if (now_tick >= next_log_tick) {
        protocol_log("[vmbus] keyboard: aguardando resposta ticks=");
        protocol_log_hex(now_tick - start_tick);
        protocol_log("\n");
        next_log_tick = now_tick + SYNTH_KBD_PROTOCOL_LOG_INTERVAL_TICKS;
      }
      if (ops->cpu_relax) {
        ops->cpu_relax();
      }
      continue;
    }
    protocol_log("[vmbus] keyboard: raw packet len=");
    protocol_log_hex((uint64_t)packet_len);
    protocol_log("\n");
    ret = vmbus_keyboard_protocol_process_packet(kbd, packet, packet_len, NULL,
                                                 NULL, NULL);
    if (ret == 2) {
      protocol_log("[vmbus] keyboard: protocolo aceito.\n");
      return 0;
    }
    if (ret < 0) {
      protocol_log("[vmbus] keyboard: protocolo rejeitado rc=");
      protocol_log_hex((uint64_t)(uint32_t)ret);
      protocol_log("\n");
      return -3;
    }
  }
}
