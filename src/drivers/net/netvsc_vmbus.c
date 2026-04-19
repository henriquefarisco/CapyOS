#include "drivers/net/netvsc_vmbus.h"

#include "drivers/hyperv/hyperv.h"

static int netvsc_vmbus_query_offer(struct vmbus_offer_info *out) {
  static const struct hv_guid k_hyperv_nic_guid = {
      .data1 = HV_NIC_GUID_DATA1,
      .data2 = HV_NIC_GUID_DATA2,
      .data3 = HV_NIC_GUID_DATA3,
      .data4 = {HV_NIC_GUID_DATA4_0, HV_NIC_GUID_DATA4_1, HV_NIC_GUID_DATA4_2,
                HV_NIC_GUID_DATA4_3, HV_NIC_GUID_DATA4_4, HV_NIC_GUID_DATA4_5,
                HV_NIC_GUID_DATA4_6, HV_NIC_GUID_DATA4_7}};
  return vmbus_query_cached_offer(&k_hyperv_nic_guid, out);
}

int netvsc_vmbus_offer_cached(struct vmbus_offer_info *out) {
  return netvsc_vmbus_query_offer(out);
}

int netvsc_vmbus_offer_refresh_connected(struct vmbus_offer_info *out) {
  static const struct hv_guid k_hyperv_nic_guid = {
      .data1 = HV_NIC_GUID_DATA1,
      .data2 = HV_NIC_GUID_DATA2,
      .data3 = HV_NIC_GUID_DATA3,
      .data4 = {HV_NIC_GUID_DATA4_0, HV_NIC_GUID_DATA4_1, HV_NIC_GUID_DATA4_2,
                HV_NIC_GUID_DATA4_3, HV_NIC_GUID_DATA4_4, HV_NIC_GUID_DATA4_5,
                HV_NIC_GUID_DATA4_6, HV_NIC_GUID_DATA4_7}};
  return vmbus_refresh_connected_offer(&k_hyperv_nic_guid, out);
}

static int netvsc_vmbus_open_channel(struct vmbus_channel_runtime *channel) {
  return vmbus_channel_runtime_open(channel);
}

static int netvsc_vmbus_send_control(struct vmbus_channel_runtime *channel,
                                     const uint8_t *buf, size_t len) {
  if (!buf || len == 0u) {
    return -1;
  }
  return vmbus_channel_runtime_send_inband(channel, buf, (uint32_t)len, 1u);
}

static int netvsc_vmbus_recv_control(struct vmbus_channel_runtime *channel,
                                     uint8_t *buf, size_t cap,
                                     size_t *out_len) {
  uint32_t packet_len = 0u;
  uint32_t payload_len = 0u;
  const uint8_t *payload = NULL;
  int rc = 0;

  if (!buf || !out_len || cap == 0u) {
    return -1;
  }
  rc = vmbus_channel_runtime_read(channel, buf, (uint32_t)cap, &packet_len);
  if (rc <= 0) {
    *out_len = 0u;
    return rc;
  }
  rc = vmbus_packet_extract_payload(buf, packet_len, &payload, &payload_len);
  if (rc <= 0) {
    *out_len = 0u;
    return rc < 0 ? rc : 0;
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
  return 1;
}

void netvsc_vmbus_ops_init(struct netvsc_backend_ops *out) {
  if (!out) {
    return;
  }
  out->query_offer = netvsc_vmbus_query_offer;
  out->open_channel = netvsc_vmbus_open_channel;
  out->send_control = netvsc_vmbus_send_control;
  out->recv_control = netvsc_vmbus_recv_control;
}
