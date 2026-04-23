#include "arch/x86_64/framebuffer_console.h"
#include "drivers/storage/storvsc_vmbus.h"

#include "drivers/hyperv/hyperv.h"

#ifndef UNIT_TEST
#endif

struct storvsc_vmbus_packet_desc {
  uint16_t type;
  uint16_t offset8;
  uint16_t len8;
  uint16_t flags;
  uint64_t trans_id;
} __attribute__((packed));

static void storvsc_vmbus_log(const char *s) {
#ifndef UNIT_TEST
  fbcon_print(s);
#else
  (void)s;
#endif
}

static void storvsc_vmbus_log_hex(uint64_t value) {
#ifndef UNIT_TEST
  fbcon_print_hex(value);
#else
  (void)value;
#endif
}

static void storvsc_vmbus_zero(void *dst, size_t len) {
  uint8_t *bytes = (uint8_t *)dst;
  if (!bytes) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    bytes[i] = 0u;
  }
}

static void storvsc_vmbus_copy(void *dst, const void *src, size_t len) {
  uint8_t *out = (uint8_t *)dst;
  const uint8_t *in = (const uint8_t *)src;
  if (!out || !in) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    out[i] = in[i];
  }
}

static uint32_t storvsc_vmbus_read_u32(const uint8_t *src) {
  return ((uint32_t)src[0]) | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static int storvsc_vmbus_query_offer(struct vmbus_offer_info *out) {
  static const struct hv_guid k_hyperv_storage_guid = {
      .data1 = HV_STORAGE_GUID_DATA1,
      .data2 = HV_STORAGE_GUID_DATA2,
      .data3 = HV_STORAGE_GUID_DATA3,
      .data4 = {HV_STORAGE_GUID_DATA4_0, HV_STORAGE_GUID_DATA4_1,
                HV_STORAGE_GUID_DATA4_2, HV_STORAGE_GUID_DATA4_3,
                HV_STORAGE_GUID_DATA4_4, HV_STORAGE_GUID_DATA4_5,
                HV_STORAGE_GUID_DATA4_6, HV_STORAGE_GUID_DATA4_7}};
  return vmbus_query_cached_offer(&k_hyperv_storage_guid, out);
}

int storvsc_vmbus_offer_cached(struct vmbus_offer_info *out) {
  return storvsc_vmbus_query_offer(out);
}

int storvsc_vmbus_offer_refresh_connected(struct vmbus_offer_info *out) {
  static const struct hv_guid k_hyperv_storage_guid = {
      .data1 = HV_STORAGE_GUID_DATA1,
      .data2 = HV_STORAGE_GUID_DATA2,
      .data3 = HV_STORAGE_GUID_DATA3,
      .data4 = {HV_STORAGE_GUID_DATA4_0, HV_STORAGE_GUID_DATA4_1,
                HV_STORAGE_GUID_DATA4_2, HV_STORAGE_GUID_DATA4_3,
                HV_STORAGE_GUID_DATA4_4, HV_STORAGE_GUID_DATA4_5,
                HV_STORAGE_GUID_DATA4_6, HV_STORAGE_GUID_DATA4_7}};
  return vmbus_refresh_connected_offer(&k_hyperv_storage_guid, out);
}

int storvsc_vmbus_bus_connected(void) { return vmbus_runtime_connected(); }

static int storvsc_vmbus_open_channel(struct vmbus_channel_runtime *channel) {
  return vmbus_channel_runtime_open(channel);
}

static int storvsc_vmbus_send_control(struct vmbus_channel_runtime *channel,
                                      const uint8_t *buf, size_t len) {
  if (!buf || len == 0u) {
    return -1;
  }
  return vmbus_channel_runtime_send_inband(channel, buf, (uint32_t)len,
                                           STORVSC_CONTROL_TRANS_ID);
}

static int storvsc_vmbus_recv_control(struct vmbus_channel_runtime *channel,
                                      uint8_t *buf, size_t cap,
                                      size_t *out_len,
                                      struct storvsc_control_diag *diag) {
  struct storvsc_vmbus_packet_desc desc;
  uint32_t packet_len = 0u;
  uint32_t payload_len = 0u;
  const uint8_t *payload = NULL;
  int rc = 0;

  if (!buf || !out_len || cap == 0u) {
    return -1;
  }
  if (diag) {
    storvsc_vmbus_zero(diag, sizeof(*diag));
  }
  for (uint32_t attempt = 0u; attempt < 8u; ++attempt) {
    rc = vmbus_channel_runtime_read(channel, buf, (uint32_t)cap, &packet_len);
    if (diag) {
      diag->read_rc = rc;
      diag->packet_len = packet_len;
    }
    if (rc <= 0) {
      *out_len = 0u;
      if (rc < 0) {
        storvsc_vmbus_log("[storvsc] recv raw falhou rc=");
        storvsc_vmbus_log_hex((uint64_t)(uint32_t)(-rc));
        if (packet_len != 0u) {
          storvsc_vmbus_log(" packet_len=");
          storvsc_vmbus_log_hex((uint64_t)packet_len);
          storvsc_vmbus_log(" cap=");
          storvsc_vmbus_log_hex((uint64_t)cap);
        }
        storvsc_vmbus_log("\n");
      }
      return rc;
    }
    if (packet_len < (uint32_t)sizeof(desc)) {
      *out_len = 0u;
      if (diag) {
        diag->extract_rc = -3;
      }
      storvsc_vmbus_log("[storvsc] recv raw curto packet_len=");
      storvsc_vmbus_log_hex((uint64_t)packet_len);
      storvsc_vmbus_log("\n");
      return -3;
    }
    storvsc_vmbus_zero(&desc, sizeof(desc));
    storvsc_vmbus_copy(&desc, buf, sizeof(desc));
    if (diag) {
      diag->packet_type = desc.type;
      diag->packet_flags = desc.flags;
      diag->trans_id = desc.trans_id;
    }
    storvsc_vmbus_log("[storvsc] recv raw packet len=");
    storvsc_vmbus_log_hex((uint64_t)packet_len);
    storvsc_vmbus_log(" desc=");
    storvsc_vmbus_log_hex((uint64_t)desc.type);
    storvsc_vmbus_log(" flags=");
    storvsc_vmbus_log_hex((uint64_t)desc.flags);
    storvsc_vmbus_log(" trans=");
    storvsc_vmbus_log_hex(desc.trans_id);
    storvsc_vmbus_log(" off8=");
    storvsc_vmbus_log_hex((uint64_t)desc.offset8);
    storvsc_vmbus_log(" len8=");
    storvsc_vmbus_log_hex((uint64_t)desc.len8);
    storvsc_vmbus_log("\n");
    rc = vmbus_packet_extract_payload(buf, packet_len, &payload, &payload_len);
    if (diag) {
      diag->extract_rc = rc;
      diag->payload_len = payload_len;
    }
    if (rc <= 0) {
      storvsc_vmbus_log("[storvsc] recv raw extract rc=");
      storvsc_vmbus_log_hex((uint64_t)(uint32_t)(rc < 0 ? -rc : rc));
      storvsc_vmbus_log(" desc=");
      storvsc_vmbus_log_hex((uint64_t)desc.type);
      storvsc_vmbus_log("\n");
      if (rc < 0) {
        *out_len = 0u;
        return rc;
      }
      continue;
    }
    if ((size_t)payload_len > cap) {
      *out_len = 0u;
      return -2;
    }
    if (payload != buf) {
      for (uint32_t i = 0; i < payload_len; ++i) {
        buf[i] = payload[i];
      }
    }
    *out_len = payload_len;
    if (diag && payload_len >= 12u) {
      diag->operation = storvsc_vmbus_read_u32(buf);
      diag->status = storvsc_vmbus_read_u32(buf + 8u);
    }
    storvsc_vmbus_log("[storvsc] recv control payload_len=");
    storvsc_vmbus_log_hex((uint64_t)payload_len);
    if (payload_len >= 12u) {
      storvsc_vmbus_log(" op=");
      storvsc_vmbus_log_hex((uint64_t)storvsc_vmbus_read_u32(buf));
      storvsc_vmbus_log(" status=");
      storvsc_vmbus_log_hex((uint64_t)storvsc_vmbus_read_u32(buf + 8u));
    }
    storvsc_vmbus_log("\n");
    return 1;
  }

  *out_len = 0u;
  return 0;
}

void storvsc_vmbus_ops_init(struct storvsc_backend_ops *out) {
  if (!out) {
    return;
  }
  out->query_offer = storvsc_vmbus_query_offer;
  out->open_channel = storvsc_vmbus_open_channel;
  out->send_control = storvsc_vmbus_send_control;
  out->recv_control = storvsc_vmbus_recv_control;
}
