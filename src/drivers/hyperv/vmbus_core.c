#include "arch/x86_64/framebuffer_console.h"
#include <stddef.h>
#include <stdint.h>

#include "drivers/hyperv/hyperv.h"
#include "kernel/log/klog.h"
#include "vmbus_channel_runtime.h"
#include "vmbus_core.h"
#include "vmbus_offers.h"
#include "vmbus_transport.h"


#define VMBUS_MESSAGE_CONNECTION_ID 1
#define VMBUS_MESSAGE_CONNECTION_ID_4 4
#define VMBUS_MESSAGE_SINT 2
#define HV_STATUS_INVALID_CONNECTION_ID 0x0012u

#define VMBUS_VERSION_WIN10_V53 0x00050003
#define VMBUS_VERSION_WIN10_V52 0x00050002
#define VMBUS_VERSION_WIN10_V51 0x00050001
#define VMBUS_VERSION_WIN10 0x00050000
#define VMBUS_VERSION_WIN8_1 0x00040000
#define VMBUS_VERSION_WIN8 0x00030000
#define VMBUS_VERSION_WIN7 0x00020000

#define VMBUS_PAGE_SIZE 4096u

struct vmbus_initiate_contact {
  struct vmbus_channel_message_header header;
  uint32_t vmbus_version_requested;
  uint32_t target_vcpu;
  union {
    uint64_t interrupt_page;
    struct {
      uint8_t msg_sint;
      uint8_t msg_vtl;
      uint8_t reserved[6];
    };
  };
  uint64_t monitor_page1;
  uint64_t monitor_page2;
} __attribute__((packed));

struct vmbus_version_response {
  struct vmbus_channel_message_header header;
  uint8_t version_supported;
  uint8_t connection_state;
  uint16_t padding;
  uint32_t msg_conn_id;
} __attribute__((packed));

struct vmbus_request_offers {
  struct vmbus_channel_message_header header;
} __attribute__((packed));

struct vmbus_offer_channel {
  struct vmbus_channel_message_header header;
  struct hv_guid if_type;
  struct hv_guid if_instance;
  uint64_t reserved1;
  uint64_t reserved2;
  uint16_t channel_flags;
  uint16_t mmio_megabytes;
  uint8_t user_def[120];
  uint16_t sub_channel_index;
  uint16_t reserved3;
  uint32_t child_relid;
  uint8_t monitor_id;
  uint8_t monitor_allocated;
  uint16_t is_dedicated_interrupt;
  uint32_t connection_id;
} __attribute__((packed));

_Static_assert(sizeof(struct vmbus_channel_message_header) == 8u,
               "VMBus control header must be 8 bytes");
_Static_assert(sizeof(struct vmbus_initiate_contact) == 40u,
               "INITIATE_CONTACT layout drifted");
_Static_assert(sizeof(struct vmbus_version_response) == 16u,
               "VERSION_RESPONSE layout drifted");
_Static_assert(sizeof(struct vmbus_request_offers) == 8u,
               "REQUESTOFFERS layout drifted");
_Static_assert(offsetof(struct vmbus_offer_channel, child_relid) == 184u,
               "OFFERCHANNEL child_relid offset drifted");
_Static_assert(offsetof(struct vmbus_offer_channel, connection_id) == 192u,
               "OFFERCHANNEL connection_id offset drifted");

static int g_vmbus_initialized = 0;
static int g_vmbus_connected = 0;
static uint32_t g_msg_conn_id = VMBUS_MESSAGE_CONNECTION_ID;
static uint8_t g_vmbus_stage = HYPERV_VMBUS_STAGE_OFF;

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx) {
  __asm__ volatile("cpuid"
                   : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                   : "a"(leaf), "c"(0));
}

static uint32_t hyperv_current_vp_index(void) {
  return (uint32_t)rdmsr(HV_X64_MSR_VP_INDEX);
}

static uint32_t vmbus_sanitize_msg_conn_id(uint32_t version,
                                           uint32_t response_conn_id,
                                           uint32_t fallback_conn_id) {
  if (response_conn_id == 0u) {
    return fallback_conn_id;
  }

  if (version >= VMBUS_VERSION_WIN10 &&
      (response_conn_id == version || response_conn_id > 0xFFu)) {
    fbcon_print("[vmbus] msg_conn_id suspeito, mantendo fallback=");
    fbcon_print_hex((uint64_t)fallback_conn_id);
    fbcon_print(" host=");
    fbcon_print_hex((uint64_t)response_conn_id);
    fbcon_print("\n");
    return fallback_conn_id;
  }

  return response_conn_id;
}

static const struct vmbus_offer_guid_key *
offer_guid_view(const struct hv_guid *guid) {
  return (const struct vmbus_offer_guid_key *)guid;
}

static const struct vmbus_offer_data *
offer_data_view(const struct vmbus_offer_info *offer) {
  return (const struct vmbus_offer_data *)offer;
}

static struct vmbus_offer_data *offer_data_mut(struct vmbus_offer_info *offer) {
  return (struct vmbus_offer_data *)offer;
}

static int guid_matches_public(const struct hv_guid *left,
                               const struct hv_guid *right) {
  return vmbus_guid_matches(offer_guid_view(left), offer_guid_view(right));
}

static const char *offer_kind_label(const struct hv_guid *guid) {
  static const struct hv_guid k_hyperv_kbd_guid = {
      .data1 = HV_KBD_GUID_DATA1,
      .data2 = HV_KBD_GUID_DATA2,
      .data3 = HV_KBD_GUID_DATA3,
      .data4 = {HV_KBD_GUID_DATA4_0, HV_KBD_GUID_DATA4_1, HV_KBD_GUID_DATA4_2,
                HV_KBD_GUID_DATA4_3, HV_KBD_GUID_DATA4_4, HV_KBD_GUID_DATA4_5,
                HV_KBD_GUID_DATA4_6, HV_KBD_GUID_DATA4_7}};
  static const struct hv_guid k_hyperv_nic_guid = {
      .data1 = HV_NIC_GUID_DATA1,
      .data2 = HV_NIC_GUID_DATA2,
      .data3 = HV_NIC_GUID_DATA3,
      .data4 = {HV_NIC_GUID_DATA4_0, HV_NIC_GUID_DATA4_1, HV_NIC_GUID_DATA4_2,
                HV_NIC_GUID_DATA4_3, HV_NIC_GUID_DATA4_4, HV_NIC_GUID_DATA4_5,
                HV_NIC_GUID_DATA4_6, HV_NIC_GUID_DATA4_7}};
  static const struct hv_guid k_hyperv_storage_guid = {
      .data1 = HV_STORAGE_GUID_DATA1,
      .data2 = HV_STORAGE_GUID_DATA2,
      .data3 = HV_STORAGE_GUID_DATA3,
      .data4 = {HV_STORAGE_GUID_DATA4_0, HV_STORAGE_GUID_DATA4_1,
                HV_STORAGE_GUID_DATA4_2, HV_STORAGE_GUID_DATA4_3,
                HV_STORAGE_GUID_DATA4_4, HV_STORAGE_GUID_DATA4_5,
                HV_STORAGE_GUID_DATA4_6, HV_STORAGE_GUID_DATA4_7}};

  if (!guid) {
    return "unknown";
  }
  if (guid_matches_public(guid, &k_hyperv_kbd_guid)) {
    return "keyboard";
  }
  if (guid_matches_public(guid, &k_hyperv_nic_guid)) {
    return "netvsc";
  }
  if (guid_matches_public(guid, &k_hyperv_storage_guid)) {
    return "storvsc";
  }
  return "other";
}

static void offer_info_zero(struct vmbus_offer_info *out) {
  vmbus_offer_info_zero(offer_data_mut(out));
}

static void offer_cache_store_public(const struct hv_guid *guid,
                                     const struct vmbus_offer_info *offer) {
  vmbus_offer_cache_store(offer_guid_view(guid), offer_data_view(offer));
}

static int offer_cache_lookup_public(const struct hv_guid *guid,
                                     struct vmbus_offer_info *out) {
  return vmbus_offer_cache_lookup(offer_guid_view(guid), offer_data_mut(out));
}

static void vmbus_reset_connection_state(void) {
  g_vmbus_connected = 0;
  g_msg_conn_id = VMBUS_MESSAGE_CONNECTION_ID;
  vmbus_offer_cache_reset();
  if (vmbus_transport_synic_ready()) {
    g_vmbus_stage = HYPERV_VMBUS_STAGE_SYNIC;
  } else if (vmbus_transport_hypercall_prepared()) {
    g_vmbus_stage = HYPERV_VMBUS_STAGE_HYPERCALL;
  } else {
    g_vmbus_stage = HYPERV_VMBUS_STAGE_OFF;
  }
}

int vmbus_post_msg(void *msg, uint32_t len) {
  static const uint32_t fallback_ids[] = {
      VMBUS_MESSAGE_CONNECTION_ID,
      VMBUS_MESSAGE_CONNECTION_ID_4,
  };
  int rc = vmbus_transport_post_msg(msg, len, g_msg_conn_id);

  if (rc != -(int)HV_STATUS_INVALID_CONNECTION_ID) {
    return rc;
  }

  for (uint32_t i = 0;
       i < (uint32_t)(sizeof(fallback_ids) / sizeof(fallback_ids[0])); ++i) {
    uint32_t candidate = fallback_ids[i];

    if (candidate == g_msg_conn_id) {
      continue;
    }

    fbcon_print("[vmbus] msg_conn_id invalido; tentando fallback=");
    fbcon_print_hex((uint64_t)candidate);
    fbcon_print("\n");

    rc = vmbus_transport_post_msg(msg, len, candidate);
    if (rc == 0) {
      g_msg_conn_id = candidate;
      fbcon_print("[vmbus] msg_conn_id ajustado para ");
      fbcon_print_hex((uint64_t)g_msg_conn_id);
      fbcon_print("\n");
      return 0;
    }
    if (rc != -(int)HV_STATUS_INVALID_CONNECTION_ID) {
      return rc;
    }
  }

  return rc;
}

void vmbus_signal_relid(uint32_t relid) {
  vmbus_transport_signal_relid(relid);
}

void vmbus_signal_monitor(uint8_t monitor_id) {
  vmbus_transport_signal_monitor(monitor_id);
}

int vmbus_signal_event(uint32_t connection_id) {
  return vmbus_transport_signal_event(connection_id);
}

int vmbus_wait_message(void *buf, uint32_t maxlen, int timeout_loops) {
  return vmbus_transport_wait_message(buf, maxlen, timeout_loops);
}

int hyperv_detect(void) {
  uint32_t eax, ebx, ecx, edx;

  cpuid(0x40000000, &eax, &ebx, &ecx, &edx);
  return (ebx == 0x7263694D && ecx == 0x666F736F && edx == 0x76482074) ? 1 : 0;
}

int vmbus_init(void) {
  if (!hyperv_detect()) {
    return -1;
  }
  if (g_vmbus_initialized) {
    return 0;
  }

  if (vmbus_transport_init() != 0) {
    return -3;
  }

  g_vmbus_initialized = 1;
  g_vmbus_stage = vmbus_transport_synic_ready() ? HYPERV_VMBUS_STAGE_SYNIC
                                                : HYPERV_VMBUS_STAGE_HYPERCALL;
  return 0;
}

int vmbus_runtime_prepare(void) {
  return vmbus_runtime_prepare_hypercall();
}

int vmbus_runtime_prepare_hypercall(void) {
  if (!hyperv_detect()) {
    return -1;
  }
  if (vmbus_transport_prepare_hypercall() != 0) {
    return -2;
  }
  if (g_vmbus_stage < HYPERV_VMBUS_STAGE_HYPERCALL) {
    g_vmbus_stage = HYPERV_VMBUS_STAGE_HYPERCALL;
  }
  return 0;
}

int vmbus_runtime_prepared(void) { return vmbus_runtime_hypercall_prepared(); }

int vmbus_runtime_hypercall_prepared(void) {
  return vmbus_transport_hypercall_prepared();
}

int vmbus_runtime_prepare_synic(void) {
  if (!hyperv_detect()) {
    return -1;
  }
  if (vmbus_transport_prepare_hypercall() != 0) {
    return -2;
  }
  if (vmbus_transport_prepare_synic() != 0) {
    return -3;
  }
  if (g_vmbus_stage < HYPERV_VMBUS_STAGE_SYNIC) {
    g_vmbus_stage = HYPERV_VMBUS_STAGE_SYNIC;
  }
  return 0;
}

int vmbus_runtime_synic_ready(void) { return vmbus_transport_synic_ready(); }

static int vmbus_negotiate_version(void) {
  struct vmbus_initiate_contact contact;
  struct vmbus_version_response response;
  uint32_t versions[] = {VMBUS_VERSION_WIN10_V53, VMBUS_VERSION_WIN10_V52,
                         VMBUS_VERSION_WIN10_V51, VMBUS_VERSION_WIN10,
                         VMBUS_VERSION_WIN8_1,    VMBUS_VERSION_WIN8,
                         VMBUS_VERSION_WIN7};
  int num_versions = (int)(sizeof(versions) / sizeof(versions[0]));

  if (g_vmbus_connected) {
    return 0;
  }

  vmbus_reset_connection_state();
  for (int v = 0; v < num_versions; ++v) {
    uint32_t version = versions[v];
    uint32_t conn_id;
    uint32_t target_vcpu;
    int ret;

    fbcon_print("[vmbus] Inicio iter v=");
    fbcon_print_hex((uint64_t)v);
    fbcon_print(" ver=");
    fbcon_print_hex((uint64_t)version);
    fbcon_print("\n");

    for (int i = 0; i < (int)sizeof(contact); ++i) {
      ((uint8_t *)&contact)[i] = 0;
    }

    target_vcpu = hyperv_current_vp_index();
    contact.header.msgtype = CHANNELMSG_INITIATE_CONTACT;
    contact.vmbus_version_requested = version;
    contact.target_vcpu = target_vcpu;
    if (version >= VMBUS_VERSION_WIN10) {
      contact.msg_sint = VMBUS_MESSAGE_SINT;
      contact.msg_vtl = 0;
      conn_id = VMBUS_MESSAGE_CONNECTION_ID_4;
    } else {
      contact.interrupt_page = vmbus_transport_interrupt_page();
      conn_id = VMBUS_MESSAGE_CONNECTION_ID;
    }
    contact.monitor_page1 = vmbus_transport_monitor_page1();
    contact.monitor_page2 = vmbus_transport_monitor_page2();

    fbcon_print("[vmbus] INITIATE_CONTACT v");
    fbcon_print_hex(version);
    fbcon_print(" conn_id=");
    fbcon_print_hex(conn_id);
    fbcon_print(" vp=");
    fbcon_print_hex(target_vcpu);
    fbcon_print("...\n");

    ret = vmbus_transport_post_msg(&contact, (uint32_t)sizeof(contact), conn_id);
    if (ret != 0) {
      fbcon_print("[vmbus] Post falhou: ");
      fbcon_print_hex((uint64_t)(-ret));
      fbcon_print("\n");
      continue;
    }
    fbcon_print("[vmbus] Post OK, aguardando resposta...\n");
    fbcon_print("[vmbus] >>> pre-wait canary <<<\n");

    for (int i = 0; i < (int)sizeof(response); ++i) {
      ((uint8_t *)&response)[i] = 0;
    }
    ret = vmbus_wait_message(&response, (uint32_t)sizeof(response), 300000);
    fbcon_print("[vmbus] wait_message retornou: ");
    fbcon_print_hex((uint64_t)ret);
    fbcon_print("\n");
    if (ret > 0) {
      fbcon_print("[vmbus] Resp: tipo=");
      fbcon_print_hex(response.header.msgtype);
      fbcon_print(" sup=");
      fbcon_print_hex((uint64_t)response.version_supported);
      fbcon_print(" st=");
      fbcon_print_hex((uint64_t)response.connection_state);
      fbcon_print(" mcid=");
      fbcon_print_hex((uint64_t)response.msg_conn_id);
      fbcon_print("\n");
      if (response.header.msgtype == CHANNELMSG_VERSION_RESPONSE) {
        if (response.version_supported) {
          uint32_t negotiated_conn_id =
              vmbus_sanitize_msg_conn_id(version, response.msg_conn_id, conn_id);

          fbcon_print("[vmbus] Versao aceita! conn_id=");
          fbcon_print_hex(response.msg_conn_id);
          fbcon_print(" state=");
          fbcon_print_hex((uint64_t)response.connection_state);
          fbcon_print("\n");
          g_vmbus_connected = 1;
          g_msg_conn_id = negotiated_conn_id;
          g_vmbus_stage = HYPERV_VMBUS_STAGE_CONTACT;
          return 0;
        }
        fbcon_print("[vmbus] Versao rejeitada.\n");
      } else {
        fbcon_print("[vmbus] Msg ignorada (tipo!=VERSION_RESPONSE), proximo.\n");
      }
    } else {
      fbcon_print("[vmbus] Timeout esperando resposta.\n");
    }
    fbcon_print("[vmbus] Fim iteracao v=");
    fbcon_print_hex((uint64_t)v);
    fbcon_print("/");
    fbcon_print_hex((uint64_t)num_versions);
    fbcon_print("\n");
  }

  fbcon_print("[vmbus] Negociacao falhou, retornando -1.\n");
  klog(KLOG_ERROR, "[vmbus] Version negotiation FAILED across all versions.");
  return -1;
}

int vmbus_runtime_connect(void) {
  if (!hyperv_detect()) {
    return -1;
  }
  if (!g_vmbus_initialized && vmbus_init() != 0) {
    return -2;
  }
  if (!g_vmbus_connected && vmbus_negotiate_version() != 0) {
    return -3;
  }
  return 0;
}

int vmbus_runtime_connected(void) { return g_vmbus_connected ? 1 : 0; }

uint8_t vmbus_runtime_stage(void) { return g_vmbus_stage; }

static int vmbus_request_matching_offer(const struct hv_guid *guid,
                                        struct vmbus_offer_info *out) {
  struct vmbus_request_offers req;
  uint8_t msgbuf[256];
  struct vmbus_offer_info matched_offer;
  int found_match = 0;

  if (!guid || !out) {
    return -1;
  }

  offer_info_zero(out);
  offer_info_zero(&matched_offer);
  req.header.msgtype = CHANNELMSG_REQUESTOFFERS;

  fbcon_print("[vmbus] REQUESTOFFERS...\n");

  {
    int post_rc = vmbus_post_msg(&req, (uint32_t)sizeof(req));
    if (post_rc != 0) {
      fbcon_print("[vmbus] REQUESTOFFERS post rc=");
      fbcon_print_hex((uint64_t)(uint32_t)(-post_rc));
      fbcon_print("\n");
      fbcon_print("[vmbus] REQUESTOFFERS post falhou.\n");
      return -2;
    }
  }

  for (int i = 0; i < 20; ++i) {
    int ret = vmbus_wait_message(msgbuf, (uint32_t)sizeof(msgbuf), 100000);
    if (ret <= 0) {
      if (i > 0) {
        break;
      }
      continue;
    }

    if (((const struct vmbus_channel_message_header *)msgbuf)->msgtype ==
        CHANNELMSG_OFFERCHANNEL) {
      struct vmbus_offer_channel *offer = (struct vmbus_offer_channel *)msgbuf;
      struct vmbus_offer_info cached_offer;
      const char *kind = offer_kind_label(&offer->if_type);

      fbcon_print("[vmbus] OFFER relid=");
      fbcon_print_hex((uint64_t)offer->child_relid);
      fbcon_print(" conn=");
      fbcon_print_hex((uint64_t)offer->connection_id);
      fbcon_print(" mon=");
      fbcon_print_hex((uint64_t)offer->monitor_id);
      fbcon_print(" alloc=");
      fbcon_print_hex((uint64_t)offer->monitor_allocated);
      fbcon_print(" kind=");
      fbcon_print(kind);
      fbcon_print(" if=");
      fbcon_print_hex((uint64_t)offer->if_type.data1);
      fbcon_print("\n");

      cached_offer.child_relid = offer->child_relid;
      cached_offer.connection_id = offer->connection_id;
      cached_offer.monitor_id = offer->monitor_id;
      cached_offer.monitor_allocated = offer->monitor_allocated;
      cached_offer.is_dedicated_interrupt = offer->is_dedicated_interrupt;
      offer_cache_store_public(&offer->if_type, &cached_offer);
      if (g_vmbus_stage < HYPERV_VMBUS_STAGE_OFFERS) {
        g_vmbus_stage = HYPERV_VMBUS_STAGE_OFFERS;
      }

      if (guid_matches_public(&offer->if_type, guid)) {
        matched_offer = cached_offer;
        found_match = 1;
      }
    } else if (((const struct vmbus_channel_message_header *)msgbuf)->msgtype ==
               CHANNELMSG_ALLOFFERS_DELIVERED) {
      fbcon_print("[vmbus] ALLOFFERS_DELIVERED.\n");
      if (g_vmbus_stage < HYPERV_VMBUS_STAGE_OFFERS) {
        g_vmbus_stage = HYPERV_VMBUS_STAGE_OFFERS;
      }
      break;
    }
  }

  if (!found_match) {
    fbcon_print("[vmbus] Offer alvo nao encontrada.\n");
    return -3;
  }

  *out = matched_offer;
  return 0;
}

int vmbus_query_offer(const struct hv_guid *guid, struct vmbus_offer_info *out) {
  if (!guid || !out) {
    return -1;
  }
  if (offer_cache_lookup_public(guid, out) == 0) {
    return 0;
  }
  if (!hyperv_detect()) {
    return -2;
  }
  if (!g_vmbus_initialized && vmbus_init() != 0) {
    return -3;
  }
  if (!g_vmbus_connected && vmbus_negotiate_version() != 0) {
    return -4;
  }
  return vmbus_request_matching_offer(guid, out);
}

int vmbus_refresh_connected_offer(const struct hv_guid *guid,
                                  struct vmbus_offer_info *out) {
  if (!guid || !out) {
    return -1;
  }
  if (offer_cache_lookup_public(guid, out) == 0) {
    return 0;
  }
  if (!g_vmbus_initialized || !g_vmbus_connected) {
    return -2;
  }
  return vmbus_request_matching_offer(guid, out);
}

int vmbus_query_cached_offer(const struct hv_guid *guid,
                             struct vmbus_offer_info *out) {
  return offer_cache_lookup_public(guid, out);
}

void vmbus_channel_runtime_reset(struct vmbus_channel_runtime *channel) {
  vmbus_channel_runtime_reset_common(channel);
}

static void vmbus_channel_runtime_fill_ops(
    struct vmbus_channel_runtime_ops *ops) {
  if (!ops) {
    return;
  }
  ops->is_connected = vmbus_runtime_connected;
  ops->post_msg = vmbus_post_msg;
  ops->wait_message = vmbus_wait_message;
  ops->signal_relid = vmbus_signal_relid;
  ops->signal_monitor = vmbus_signal_monitor;
  ops->signal_event = vmbus_signal_event;
}

int vmbus_channel_runtime_open(struct vmbus_channel_runtime *channel) {
  struct vmbus_channel_runtime_ops ops;

  fbcon_print("[vmbus] core-open relid=");
  fbcon_print_hex(channel ? (uint64_t)channel->child_relid : 0u);
  fbcon_print(" conn=");
  fbcon_print_hex(channel ? (uint64_t)channel->connection_id : 0u);
  fbcon_print("\n");
  vmbus_channel_runtime_fill_ops(&ops);
  return vmbus_channel_runtime_open_common(channel, &ops);
}

int vmbus_channel_runtime_send_inband(struct vmbus_channel_runtime *channel,
                                      const void *payload,
                                      uint32_t payload_len,
                                      uint64_t trans_id) {
  struct vmbus_channel_runtime_ops ops;

  vmbus_channel_runtime_fill_ops(&ops);
  return vmbus_channel_runtime_send_inband_common(channel, payload, payload_len,
                                                  trans_id, &ops);
}

int vmbus_channel_runtime_read(struct vmbus_channel_runtime *channel,
                               void *buffer, uint32_t buffer_size,
                               uint32_t *out_packet_len) {
  return vmbus_channel_runtime_read_common(channel, buffer, buffer_size,
                                           out_packet_len);
}
