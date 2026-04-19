#include "arch/x86_64/framebuffer_console.h"
#include "vmbus_channel_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/log/klog.h"
#include "drivers/hyperv/hyperv.h"
#include "vmbus_ring.h"

extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree_aligned(void *ptr);
#ifndef UNIT_TEST
#endif

static void runtime_log(const char *s) {
#ifndef UNIT_TEST
  fbcon_print(s);
#else
  (void)s;
#endif
}

static void runtime_log_hex(uint64_t value) {
#ifndef UNIT_TEST
  fbcon_print_hex(value);
#else
  (void)value;
#endif
}

#define VMBUS_SINGLE_MESSAGE_MAX_PFNS 24u
#define VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED 1u
#define VMBUS_GPADL_WAIT_LOOPS 500000
#define VMBUS_OPEN_WAIT_LOOPS 500000

struct vmbus_open_channel {
  struct vmbus_channel_message_header header;
  uint32_t child_relid;
  uint32_t openid;
  uint32_t ring_buffer_gpadl;
  uint32_t target_vcpu;
  uint32_t downstream_ring_offset;
  uint8_t user_data[120];
} __attribute__((packed));

struct vmbus_open_channel_result {
  struct vmbus_channel_message_header header;
  uint32_t child_relid;
  uint32_t openid;
  uint32_t status;
} __attribute__((packed));

struct vmbus_gpadl_header_msg {
  struct vmbus_channel_message_header header;
  uint32_t child_relid;
  uint32_t gpadl;
  uint16_t range_buflen;
  uint16_t rangecount;
  uint32_t byte_count;
  uint32_t byte_offset;
  uint64_t pfn_array[VMBUS_SINGLE_MESSAGE_MAX_PFNS];
} __attribute__((packed));

struct vmbus_gpadl_created_msg {
  struct vmbus_channel_message_header header;
  uint32_t child_relid;
  uint32_t gpadl;
  uint32_t creation_status;
} __attribute__((packed));

_Static_assert(sizeof(struct vmbus_open_channel) == 148u,
               "OPENCHANNEL layout drifted");
_Static_assert(sizeof(struct vmbus_open_channel_result) == 20u,
               "OPENCHANNEL_RESULT layout drifted");
_Static_assert(offsetof(struct vmbus_gpadl_header_msg, byte_count) == 20u,
               "GPADL header byte_count offset drifted");
_Static_assert(offsetof(struct vmbus_gpadl_header_msg, pfn_array) == 28u,
               "GPADL header PFN offset drifted");
_Static_assert(sizeof(struct vmbus_gpadl_created_msg) == 20u,
               "GPADL_CREATED layout drifted");

static void channel_memzero(void *ptr, uint32_t len) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

void vmbus_channel_runtime_reset_common(struct vmbus_channel_runtime *channel) {
  if (!channel) {
    return;
  }
  if (channel->ring_buffer) {
    kfree_aligned(channel->ring_buffer);
  }
  channel->ring_buffer = NULL;
  channel->ring_size = 0;
  channel->send_ring_size = 0;
  channel->recv_ring_size = 0;
  channel->send_ring = NULL;
  channel->recv_ring = NULL;
  channel->open_id = 0;
  channel->gpadl_handle = 0;
  channel->child_relid = 0;
  channel->connection_id = 0;
  channel->monitor_id = 0;
  channel->monitor_allocated = 0;
  channel->is_dedicated_interrupt = 0;
  channel->last_gpadl_status = 0;
  channel->last_open_status = 0;
  channel->last_open_msgtype = 0;
  channel->last_open_relid = 0;
  channel->last_open_observed_id = 0;
  channel->last_target_vcpu = 0;
  channel->last_downstream_offset = 0;
  channel->last_retry_count = 0;
  channel->initialized = 0;
  channel->opened = 0;
}

static void vmbus_channel_runtime_reset_buffers(
    struct vmbus_channel_runtime *channel) {
  if (!channel) {
    return;
  }
  if (channel->ring_buffer) {
    kfree_aligned(channel->ring_buffer);
  }
  channel->ring_buffer = NULL;
  channel->ring_size = 0;
  channel->send_ring = NULL;
  channel->recv_ring = NULL;
  channel->initialized = 0;
  channel->opened = 0;
}

static int vmbus_channel_runtime_alloc_ring(
    struct vmbus_channel_runtime *channel) {
  uint32_t page_count = 0u;

  if (!channel || channel->send_ring_size == 0u || channel->recv_ring_size == 0u) {
    return -1;
  }

  channel->ring_size = channel->send_ring_size + channel->recv_ring_size;
  if ((channel->ring_size % VMBUS_PAGE_SIZE) != 0u) {
    return -2;
  }
  page_count = channel->ring_size / VMBUS_PAGE_SIZE;
  if (page_count > VMBUS_SINGLE_MESSAGE_MAX_PFNS) {
    runtime_log("[vmbus] alloc ring excede limite do GPADL header local pages=");
    runtime_log_hex((uint64_t)page_count);
    runtime_log(" max=");
    runtime_log_hex((uint64_t)VMBUS_SINGLE_MESSAGE_MAX_PFNS);
    runtime_log(" sring=");
    runtime_log_hex((uint64_t)channel->send_ring_size);
    runtime_log(" rring=");
    runtime_log_hex((uint64_t)channel->recv_ring_size);
    runtime_log("\n");
    return -3;
  }

  channel->ring_buffer =
      (uint8_t *)kmalloc_aligned(channel->ring_size, VMBUS_PAGE_SIZE);
  if (!channel->ring_buffer) {
    return -4;
  }

  channel_memzero(channel->ring_buffer, channel->ring_size);
  channel->send_ring = (void *)channel->ring_buffer;
  channel->recv_ring = (void *)(channel->ring_buffer + channel->send_ring_size);
  vmbus_ring_init((volatile struct hv_ring_buffer *)channel->send_ring);
  vmbus_ring_init((volatile struct hv_ring_buffer *)channel->recv_ring);
  return 0;
}

static int vmbus_channel_runtime_apply_ring_fallback(
    struct vmbus_channel_runtime *channel) {
  uint32_t send_pages = 0u;
  uint32_t recv_pages = 0u;
  uint32_t original_send_pages = 0u;
  uint32_t original_recv_pages = 0u;

  if (!channel || channel->send_ring_size == 0u || channel->recv_ring_size == 0u ||
      (channel->send_ring_size % VMBUS_PAGE_SIZE) != 0u ||
      (channel->recv_ring_size % VMBUS_PAGE_SIZE) != 0u) {
    return -1;
  }

  original_send_pages = channel->send_ring_size / VMBUS_PAGE_SIZE;
  original_recv_pages = channel->recv_ring_size / VMBUS_PAGE_SIZE;
  if ((original_send_pages + original_recv_pages) <=
      VMBUS_SINGLE_MESSAGE_MAX_PFNS) {
    return 0;
  }

  send_pages = original_send_pages;
  recv_pages = original_recv_pages;
  while ((send_pages + recv_pages) > VMBUS_SINGLE_MESSAGE_MAX_PFNS) {
    if (send_pages >= recv_pages && send_pages > 1u) {
      send_pages -= 1u;
      continue;
    }
    if (recv_pages > 1u) {
      recv_pages -= 1u;
      continue;
    }
    if (send_pages > 1u) {
      send_pages -= 1u;
      continue;
    }
    return -2;
  }

  if (send_pages == original_send_pages && recv_pages == original_recv_pages) {
    return 0;
  }

  runtime_log("[vmbus] ring fallback single-gpadl sring=");
  runtime_log_hex((uint64_t)channel->send_ring_size);
  runtime_log("->");
  runtime_log_hex((uint64_t)(send_pages * VMBUS_PAGE_SIZE));
  runtime_log(" rring=");
  runtime_log_hex((uint64_t)channel->recv_ring_size);
  runtime_log("->");
  runtime_log_hex((uint64_t)(recv_pages * VMBUS_PAGE_SIZE));
  runtime_log(" pages=");
  runtime_log_hex((uint64_t)(original_send_pages + original_recv_pages));
  runtime_log("->");
  runtime_log_hex((uint64_t)(send_pages + recv_pages));
  runtime_log("\n");
  runtime_log(
      "[vmbus] fallback aplicado porque o guest ainda opera apenas com GPADL header unico; GPADL_BODY multi-mensagem continua pendente.\n");

  channel->send_ring_size = send_pages * VMBUS_PAGE_SIZE;
  channel->recv_ring_size = recv_pages * VMBUS_PAGE_SIZE;
  return 1;
}

static int vmbus_channel_runtime_establish_gpadl(
    struct vmbus_channel_runtime *channel,
    const struct vmbus_channel_runtime_ops *ops) {
  struct vmbus_gpadl_header_msg msg;
  uint64_t first_pfn = 0u;
  uint64_t last_pfn = 0u;
  uint32_t page_count = 0;
  uint32_t msg_len = 0;

  if (!channel || !ops || !ops->post_msg || !ops->wait_message ||
      !channel->ring_buffer || (channel->ring_size % VMBUS_PAGE_SIZE) != 0u) {
    return -1;
  }

  page_count = channel->ring_size / VMBUS_PAGE_SIZE;
  if (page_count > VMBUS_SINGLE_MESSAGE_MAX_PFNS) {
    channel->last_gpadl_status = 0xFFFFFFFFu;
    return -2;
  }

  channel_memzero(&msg, (uint32_t)sizeof(msg));
  msg.header.msgtype = CHANNELMSG_GPADL_HEADER;
  msg.child_relid = channel->child_relid;
  msg.gpadl = channel->gpadl_handle;
  msg.range_buflen = (uint16_t)(8u + (page_count * sizeof(uint64_t)));
  msg.rangecount = 1;
  msg.byte_count = channel->ring_size;
  msg.byte_offset = 0;
  for (uint32_t i = 0; i < page_count; ++i) {
    msg.pfn_array[i] =
        (((uint64_t)(uintptr_t)channel->ring_buffer) >> 12) + (uint64_t)i;
  }
  first_pfn = msg.pfn_array[0];
  last_pfn = msg.pfn_array[page_count - 1u];
  msg_len = 28u + (page_count * (uint32_t)sizeof(uint64_t));

  runtime_log("[vmbus] GPADL relid=");
  runtime_log_hex((uint64_t)channel->child_relid);
  runtime_log(" gpadl=");
  runtime_log_hex((uint64_t)channel->gpadl_handle);
  runtime_log(" pages=");
  runtime_log_hex((uint64_t)page_count);
  runtime_log(" first_pfn=");
  runtime_log_hex(first_pfn);
  runtime_log(" last_pfn=");
  runtime_log_hex(last_pfn);
  runtime_log(" msglen=");
  runtime_log_hex((uint64_t)msg_len);
  runtime_log("\n");
  channel->last_gpadl_status = 0xFFFFFFFEu;

  if (ops->post_msg(&msg, msg_len) != 0) {
    runtime_log("[vmbus] GPADL post falhou.\n");
    channel->last_gpadl_status = 0xFFFFFFFDu;
    return -3;
  }

  for (int i = 0; i < 8; ++i) {
    struct vmbus_gpadl_created_msg created_msg;
    int ret = 0;
    channel_memzero(&created_msg, (uint32_t)sizeof(created_msg));
    ret = ops->wait_message(&created_msg, sizeof(created_msg), VMBUS_GPADL_WAIT_LOOPS);
    if (ret <= 0) {
      continue;
    }
    runtime_log("[vmbus] GPADL recv len=");
    runtime_log_hex((uint64_t)(uint32_t)ret);
    runtime_log(" msgtype=");
    runtime_log_hex((uint64_t)created_msg.header.msgtype);
    runtime_log(" relid=");
    runtime_log_hex((uint64_t)created_msg.child_relid);
    runtime_log(" gpadl=");
    runtime_log_hex((uint64_t)created_msg.gpadl);
    runtime_log(" status=");
    runtime_log_hex((uint64_t)created_msg.creation_status);
    runtime_log("\n");
    if (created_msg.header.msgtype == CHANNELMSG_GPADL_CREATED &&
        created_msg.child_relid == channel->child_relid &&
        created_msg.gpadl == channel->gpadl_handle) {
      runtime_log("[vmbus] GPADL created status=");
      runtime_log_hex((uint64_t)created_msg.creation_status);
      runtime_log("\n");
      channel->last_gpadl_status = created_msg.creation_status;
      if (created_msg.creation_status != 0u) {
        runtime_log("[vmbus] GPADL host rejeitou relid=");
        runtime_log_hex((uint64_t)channel->child_relid);
        runtime_log(" gpadl=");
        runtime_log_hex((uint64_t)channel->gpadl_handle);
        runtime_log(" pages=");
        runtime_log_hex((uint64_t)page_count);
        runtime_log(" first_pfn=");
        runtime_log_hex(first_pfn);
        runtime_log(" last_pfn=");
        runtime_log_hex(last_pfn);
        runtime_log("\n");
      }
      return created_msg.creation_status == 0 ? 0 : -4;
    }
  }

  runtime_log("[vmbus] GPADL timeout.\n");
  klog(KLOG_ERROR, "[vmbus] GPADL CREATED timeout: host did not respond.");
  klog_hex(KLOG_ERROR, "[vmbus] GPADL relid=", (uint64_t)channel->child_relid);
  klog_hex(KLOG_ERROR, "[vmbus] GPADL handle=", (uint64_t)channel->gpadl_handle);
  channel->last_gpadl_status = 0xFFFFFFFCu;
  return -5;
}

static int vmbus_channel_runtime_open_internal(
    struct vmbus_channel_runtime *channel,
    const struct vmbus_channel_runtime_ops *ops) {
  struct vmbus_open_channel open_msg;
  uint32_t original_openid = 0u;
  uint32_t original_target_vcpu = 0u;
  uint32_t original_downstream_offset = 0u;
  uint32_t relid_match_openid = 0u;
  uint32_t relid_match_status = 0u;
  uint32_t success_fallback_relid = 0u;
  uint32_t success_fallback_openid = 0u;
  int saw_relid_match = 0;
  int saw_success_fallback = 0;
  int phase = 0;

  if (!channel || !ops || !ops->post_msg || !ops->wait_message) {
    return -1;
  }

  channel_memzero(&open_msg, (uint32_t)sizeof(open_msg));
  open_msg.header.msgtype = CHANNELMSG_OPENCHANNEL;
  open_msg.child_relid = channel->child_relid;
  open_msg.openid = channel->open_id;
  open_msg.ring_buffer_gpadl = channel->gpadl_handle;
  open_msg.target_vcpu = (uint32_t)rdmsr(HV_X64_MSR_VP_INDEX);
  open_msg.downstream_ring_offset = channel->send_ring_size / VMBUS_PAGE_SIZE;
  original_openid = open_msg.openid;
  original_target_vcpu = open_msg.target_vcpu;
  original_downstream_offset = open_msg.downstream_ring_offset;
  channel->last_open_status = 0xFFFFFFFEu;
  channel->last_open_msgtype = 0u;
  channel->last_open_relid = 0u;
  channel->last_open_observed_id = 0u;
  channel->last_target_vcpu = open_msg.target_vcpu;
  channel->last_downstream_offset = open_msg.downstream_ring_offset;
  channel->last_retry_count = 0u;

  runtime_log("[vmbus] OPENCHANNEL relid=");
  runtime_log_hex((uint64_t)channel->child_relid);
  runtime_log(" conn=");
  runtime_log_hex((uint64_t)channel->connection_id);
  runtime_log(" openid=");
  runtime_log_hex((uint64_t)channel->open_id);
  runtime_log(" gpadl=");
  runtime_log_hex((uint64_t)channel->gpadl_handle);
  runtime_log(" target_vcpu=");
  runtime_log_hex((uint64_t)open_msg.target_vcpu);
  runtime_log(" downstream=");
  runtime_log_hex((uint64_t)open_msg.downstream_ring_offset);
  runtime_log("\n");

  for (phase = 0; phase < 2; ++phase) {
    if (phase == 1) {
      if (original_openid == channel->child_relid && original_target_vcpu == 0u) {
        break;
      }
      channel_memzero(&open_msg, (uint32_t)sizeof(open_msg));
      open_msg.header.msgtype = CHANNELMSG_OPENCHANNEL;
      open_msg.child_relid = channel->child_relid;
      open_msg.openid = channel->child_relid;
      open_msg.ring_buffer_gpadl = channel->gpadl_handle;
      open_msg.target_vcpu = 0u;
      open_msg.downstream_ring_offset = original_downstream_offset;
      runtime_log(
          "[vmbus] OPENCHANNEL retry linux-style openid=relid target_vcpu=0.\n");
      runtime_log("[vmbus] OPENCHANNEL retry relid=");
      runtime_log_hex((uint64_t)open_msg.child_relid);
      runtime_log(" openid=");
      runtime_log_hex((uint64_t)open_msg.openid);
      runtime_log(" gpadl=");
      runtime_log_hex((uint64_t)open_msg.ring_buffer_gpadl);
      runtime_log(" downstream=");
      runtime_log_hex((uint64_t)open_msg.downstream_ring_offset);
      runtime_log("\n");
      channel->last_retry_count = 1u;
      channel->last_target_vcpu = open_msg.target_vcpu;
      channel->last_downstream_offset = open_msg.downstream_ring_offset;
    }
    if (ops->post_msg(&open_msg, (uint32_t)sizeof(open_msg)) != 0) {
      runtime_log("[vmbus] OPENCHANNEL post falhou.\n");
      channel->last_open_status = 0xFFFFFFFDu;
      return -2;
    }

    saw_relid_match = 0;
    relid_match_openid = 0u;
    relid_match_status = 0u;

    for (int i = 0; i < 8; ++i) {
      struct vmbus_open_channel_result open_result;
      int ret = 0;
      channel_memzero(&open_result, (uint32_t)sizeof(open_result));
      ret = ops->wait_message(&open_result, sizeof(open_result), VMBUS_OPEN_WAIT_LOOPS);
      if (ret <= 0) {
        continue;
      }
      runtime_log("[vmbus] OPENCHANNEL recv len=");
      runtime_log_hex((uint64_t)(uint32_t)ret);
      runtime_log(" ");
      runtime_log("[vmbus] OPENCHANNEL recv msgtype=");
      runtime_log_hex((uint64_t)open_result.header.msgtype);
      runtime_log(" relid=");
      runtime_log_hex((uint64_t)open_result.child_relid);
      runtime_log(" openid=");
      runtime_log_hex((uint64_t)open_result.openid);
      runtime_log(" status=");
      runtime_log_hex((uint64_t)open_result.status);
      runtime_log("\n");
      channel->last_open_msgtype = open_result.header.msgtype;
      channel->last_open_relid = open_result.child_relid;
      channel->last_open_observed_id = open_result.openid;
      channel->last_open_status = open_result.status;
      if (open_result.header.msgtype != CHANNELMSG_OPENCHANNEL_RESULT) {
        continue;
      }
      if (open_result.child_relid == channel->child_relid &&
          open_result.openid == open_msg.openid) {
        runtime_log("[vmbus] OPENCHANNEL result=");
        runtime_log_hex((uint64_t)open_result.status);
        runtime_log("\n");
        channel->open_id = open_msg.openid;
        return open_result.status == 0 ? 0 : -3;
      }
      if (open_result.status == 0u) {
        saw_success_fallback = 1;
        success_fallback_relid = open_result.child_relid;
        success_fallback_openid = open_result.openid;
      }
      if (open_result.child_relid == channel->child_relid) {
        saw_relid_match = 1;
        relid_match_openid = open_result.openid;
        relid_match_status = open_result.status;
      }
    }

    if (saw_relid_match) {
      runtime_log(
          "[vmbus] OPENCHANNEL ack veio com openid divergente; aceitando por relid para evitar falso timeout.\n");
      runtime_log("[vmbus] OPENCHANNEL relid-match openid=");
      runtime_log_hex((uint64_t)relid_match_openid);
      runtime_log(" status=");
      runtime_log_hex((uint64_t)relid_match_status);
      runtime_log("\n");
      channel->open_id = relid_match_openid;
      channel->last_open_msgtype = CHANNELMSG_OPENCHANNEL_RESULT;
      channel->last_open_relid = channel->child_relid;
      channel->last_open_observed_id = relid_match_openid;
      channel->last_open_status = relid_match_status;
      return relid_match_status == 0u ? 0 : -3;
    }
    if (saw_success_fallback) {
      runtime_log(
          "[vmbus] OPENCHANNEL ack de sucesso veio sem ids coerentes; aceitando fallback porque ha apenas uma abertura sincrona em voo nesta implementacao.\n");
      runtime_log("[vmbus] OPENCHANNEL success-fallback relid=");
      runtime_log_hex((uint64_t)success_fallback_relid);
      runtime_log(" openid=");
      runtime_log_hex((uint64_t)success_fallback_openid);
      runtime_log("\n");
      if (success_fallback_openid != 0u) {
        channel->open_id = success_fallback_openid;
        channel->last_open_observed_id = success_fallback_openid;
      }
      if (success_fallback_relid != 0u) {
        channel->last_open_relid = success_fallback_relid;
      }
      channel->last_open_msgtype = CHANNELMSG_OPENCHANNEL_RESULT;
      channel->last_open_status = 0u;
      return 0;
    }
  }

  runtime_log("[vmbus] OPENCHANNEL timeout.\n");
  channel->last_open_status = 0xFFFFFFFCu;
  return -4;
}

int vmbus_channel_runtime_open_common(
    struct vmbus_channel_runtime *channel,
    const struct vmbus_channel_runtime_ops *ops) {
  runtime_log("[vmbus] open-common entry channel=");
  runtime_log_hex((uint64_t)(uintptr_t)channel);
  runtime_log(" ops=");
  runtime_log_hex((uint64_t)(uintptr_t)ops);
  runtime_log("\n");

  if (!channel) {
    runtime_log("[vmbus] open-common falhou: channel nulo.\n");
    return -1;
  }
  if (!ops) {
    runtime_log("[vmbus] open-common falhou: ops nulo.\n");
    return -1;
  }

  runtime_log("[vmbus] open-common checking bus...\n");
  if (!ops->is_connected) {
    runtime_log("[vmbus] open-common falhou: is_connected ausente.\n");
    return -1;
  }
  if (!ops->is_connected()) {
    runtime_log("[vmbus] channel-open bloqueado: barramento desconectado.\n");
    return -1;
  }
  runtime_log("[vmbus] open-common bus ok.\n");

  if (channel->opened) {
    return 0;
  }

  runtime_log("[vmbus] open-common validating params...\n");
  if (channel->child_relid == 0u || channel->connection_id == 0u ||
      channel->open_id == 0u || channel->gpadl_handle == 0u ||
      channel->send_ring_size == 0u || channel->recv_ring_size == 0u) {
    runtime_log("[vmbus] channel-open invalido relid=");
    runtime_log_hex((uint64_t)channel->child_relid);
    runtime_log(" conn=");
    runtime_log_hex((uint64_t)channel->connection_id);
    runtime_log(" openid=");
    runtime_log_hex((uint64_t)channel->open_id);
    runtime_log(" gpadl=");
    runtime_log_hex((uint64_t)channel->gpadl_handle);
    runtime_log("\n");
    return -2;
  }

  runtime_log("[vmbus] channel-setup relid=");
  runtime_log_hex((uint64_t)channel->child_relid);
  runtime_log(" conn=");
  runtime_log_hex((uint64_t)channel->connection_id);
  runtime_log(" mon=");
  runtime_log_hex((uint64_t)channel->monitor_id);
  runtime_log(" alloc=");
  runtime_log_hex((uint64_t)channel->monitor_allocated);
  runtime_log(" sring=");
  runtime_log_hex((uint64_t)channel->send_ring_size);
  runtime_log(" rring=");
  runtime_log_hex((uint64_t)channel->recv_ring_size);
  runtime_log("\n");

  vmbus_channel_runtime_reset_buffers(channel);
  {
    int rc = vmbus_channel_runtime_apply_ring_fallback(channel);
    if (rc < 0) {
      runtime_log("[vmbus] ring fallback falhou rc=");
      runtime_log_hex((uint64_t)(uint32_t)(-rc));
      runtime_log("\n");
      return -3;
    }
  }
  {
    int rc = vmbus_channel_runtime_alloc_ring(channel);
    if (rc != 0) {
      runtime_log("[vmbus] alloc ring falhou rc=");
      runtime_log_hex((uint64_t)(uint32_t)(-rc));
      runtime_log("\n");
      return -3;
    }
  }
  {
    int rc = vmbus_channel_runtime_establish_gpadl(channel, ops);
    if (rc != 0) {
      runtime_log("[vmbus] GPADL setup falhou rc=");
      runtime_log_hex((uint64_t)(uint32_t)(-rc));
      runtime_log("\n");
      vmbus_channel_runtime_reset_buffers(channel);
      return -4;
    }
  }
  {
    int rc = vmbus_channel_runtime_open_internal(channel, ops);
    if (rc != 0) {
      runtime_log("[vmbus] OPENCHANNEL setup falhou rc=");
      runtime_log_hex((uint64_t)(uint32_t)(-rc));
      runtime_log("\n");
      vmbus_channel_runtime_reset_buffers(channel);
      return -5;
    }
  }

  channel->initialized = 1;
  channel->opened = 1;
  return 0;
}

int vmbus_channel_runtime_send_inband_common(
    struct vmbus_channel_runtime *channel, const void *payload,
    uint32_t payload_len, uint64_t trans_id,
    const struct vmbus_channel_runtime_ops *ops) {
  if (!channel || !channel->opened || !channel->send_ring || !ops ||
      !ops->signal_event) {
    return -1;
  }
  return vmbus_write_inband_packet_runtime(
      channel->child_relid, channel->connection_id, channel->monitor_id,
      channel->monitor_allocated, channel->is_dedicated_interrupt,
      (volatile struct hv_ring_buffer *)channel->send_ring,
      channel->send_ring_size, payload, payload_len,
      VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED, trans_id, ops->signal_relid,
      ops->signal_monitor, ops->signal_event);
}

int vmbus_channel_runtime_read_common(struct vmbus_channel_runtime *channel,
                                      void *buffer, uint32_t buffer_size,
                                      uint32_t *out_packet_len) {
  if (!channel || !channel->opened || !channel->recv_ring) {
    return -1;
  }
  return vmbus_read_raw_packet_runtime(
      (volatile struct hv_ring_buffer *)channel->recv_ring,
      channel->recv_ring_size, buffer, buffer_size, out_packet_len);
}
