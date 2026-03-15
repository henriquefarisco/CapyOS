/* CAPYOS Hyper-V VMBus Interface
 * Implements VMBus communication for synthetic devices on Hyper-V
 */
#ifndef HYPERV_H
#define HYPERV_H

#include <stdint.h>

/* Hyper-V synthetic MSRs */
#define HV_X64_MSR_GUEST_OS_ID 0x40000000
#define HV_X64_MSR_HYPERCALL 0x40000001
#define HV_X64_MSR_VP_INDEX 0x40000002
#define HV_X64_MSR_TIME_REF_COUNT 0x40000020
#define HV_X64_MSR_SIMP 0x40000083
#define HV_X64_MSR_SIEFP 0x40000082
#define HV_X64_MSR_SINT0 0x40000090
#define HV_X64_MSR_SCONTROL 0x40000080
#define HV_X64_MSR_SVERSION 0x40000081

/* Hyper-V Synthetic Keyboard GUID: {F912AD6D-2B17-48EA-BD65-F927A61C7684} */
#define HV_KBD_GUID_DATA1 0xF912AD6D
#define HV_KBD_GUID_DATA2 0x2B17
#define HV_KBD_GUID_DATA3 0x48EA
#define HV_KBD_GUID_DATA4_0 0xBD
#define HV_KBD_GUID_DATA4_1 0x65
#define HV_KBD_GUID_DATA4_2 0xF9
#define HV_KBD_GUID_DATA4_3 0x27
#define HV_KBD_GUID_DATA4_4 0xA6
#define HV_KBD_GUID_DATA4_5 0x1C
#define HV_KBD_GUID_DATA4_6 0x76
#define HV_KBD_GUID_DATA4_7 0x84

/* VMBus connection states */
#define VMBUS_CONNECT_STATE_INIT 0
#define VMBUS_CONNECT_STATE_CONNECTED 1
#define VMBUS_CONNECT_STATE_OPEN 2

/* VMBus message types */
#define VMBUS_MSG_INVALID 0
#define VMBUS_MSG_CHANNELMSG 1
#define VMBUS_MSG_TIMER_EXPIRED 2

/* VMBus channel message types */
#define CHANNELMSG_INVALID 0
#define CHANNELMSG_OFFERCHANNEL 1
#define CHANNELMSG_RESCIND_CHANNELOFFER 2
#define CHANNELMSG_REQUESTOFFERS 3
#define CHANNELMSG_ALLOFFERS_DELIVERED 4
#define CHANNELMSG_OPENCHANNEL 5
#define CHANNELMSG_OPENCHANNEL_RESULT 6
#define CHANNELMSG_CLOSECHANNEL 7
#define CHANNELMSG_GPADL_HEADER 8
#define CHANNELMSG_GPADL_BODY 9
#define CHANNELMSG_GPADL_CREATED 10
#define CHANNELMSG_GPADL_TEARDOWN 11
#define CHANNELMSG_GPADL_TORNDOWN 12
#define CHANNELMSG_RELID_RELEASED 13
#define CHANNELMSG_INITIATE_CONTACT 14
#define CHANNELMSG_VERSION_RESPONSE 15

/* Synthetic keyboard protocol messages */
#define SYNTH_KBD_PROTOCOL_REQUEST 1
#define SYNTH_KBD_PROTOCOL_RESPONSE 2
#define SYNTH_KBD_EVENT 3
#define SYNTH_KBD_LED_INDICATORS 4

/* Synthetic keyboard protocol version */
#define SYNTH_KBD_VERSION_MAJOR 1
#define SYNTH_KBD_VERSION_MINOR 0

/* GUID structure for VMBus device identification */
struct hv_guid {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
} __attribute__((packed));

/* VMBus ring buffer header */
struct vmbus_ring_buffer {
  uint32_t write_index;
  uint32_t read_index;
  uint32_t interrupt_mask;
  uint32_t pending_send_size;
  uint32_t reserved[12];
  /* Data follows */
} __attribute__((packed));

/* Channel offer message */
struct vmbus_channel_offer {
  struct hv_guid if_type;     /* Device type GUID */
  struct hv_guid if_instance; /* Instance GUID */
  uint64_t interrupt_latency_in_100ns;
  uint32_t if_revision;
  uint32_t server_ctx_area_size;
  uint16_t channel_flags;
  uint16_t mmio_megabytes;
  uint8_t user_defined[120];
  uint16_t sub_channel_index;
  uint16_t reserved3;
  uint32_t connection_id;
} __attribute__((packed));

/* Synthetic keyboard event */
struct synth_kbd_event {
  uint32_t type; /* SYNTH_KBD_EVENT */
  uint32_t reserved;
  uint64_t info; /* Key info (make/break + scancode) */
} __attribute__((packed));

/* Synthetic keyboard keystroke info bits */
#define KBD_FLAG_BREAK 0x80000000 /* Key release */
#define KBD_FLAG_E0 0x100         /* Extended scancode */
#define KBD_FLAG_E1 0x200         /* Extended scancode */
#define KBD_FLAG_UNICODE 0x4      /* Unicode key */
#define KBD_SCANCODE_MASK 0xFF    /* Scancode bits */

/* O estado real do teclado VMBus vive no driver; consumidores usam apenas ponteiro opaco. */
struct vmbus_keyboard;

/* Function prototypes */
int hyperv_detect(void);
int vmbus_init(void);
int hyperv_keyboard_init(void);
int vmbus_keyboard_init(struct vmbus_keyboard *kbd);
int vmbus_keyboard_poll(struct vmbus_keyboard *kbd, uint8_t *scancode,
                        int *is_break);
struct vmbus_keyboard *vmbus_get_keyboard(void);

/* MSR helpers */
static inline void wrmsr(uint32_t msr, uint64_t value) {
  uint32_t lo = (uint32_t)value;
  uint32_t hi = (uint32_t)(value >> 32);
  __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static inline uint64_t rdmsr(uint32_t msr) {
  uint32_t lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((uint64_t)hi << 32) | lo;
}

#endif /* HYPERV_H */
