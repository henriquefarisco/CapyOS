/* NoirOS Hyper-V VMBus Synthetic Keyboard Driver - Complete Protocol
 * Based on iPXE and Linux kernel VMBus implementations
 * Implements full channel negotiation and keyboard event reception
 */
#include <stddef.h>
#include <stdint.h>

/* Forward declarations */
extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree_aligned(void *ptr);
extern void fbcon_print(const char *s);
extern void fbcon_print_hex(uint64_t val);

/* ============================================================================
 * MSR and CPUID Helpers
 * ============================================================================
 */

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

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx) {
  __asm__ volatile("cpuid"
                   : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                   : "a"(leaf), "c"(0));
}

static inline void cpu_relax(void) { __asm__ volatile("pause" ::: "memory"); }

/* ============================================================================
 * Hyper-V Constants and MSRs
 * ============================================================================
 */

#define HV_X64_MSR_GUEST_OS_ID 0x40000000
#define HV_X64_MSR_HYPERCALL 0x40000001
#define HV_X64_MSR_VP_INDEX 0x40000002
#define HV_X64_MSR_SCONTROL 0x40000080
#define HV_X64_MSR_SIEFP 0x40000082
#define HV_X64_MSR_SIMP 0x40000083
#define HV_X64_MSR_SINT0 0x40000090
#define HV_X64_MSR_SINT2 0x40000092

#define HV_HYPERCALL_ENABLE 0x00000001UL
#define HV_SCONTROL_ENABLE 0x00000001UL
#define HV_SIEFP_ENABLE 0x00000001UL
#define HV_SIMP_ENABLE 0x00000001UL
#define HV_SINT_MASKED 0x00010000UL
#define HV_SINT_AUTO_EOI 0x00020000UL
#define HV_SINT_VECTOR(x) ((x) << 0)

/* Hypercall codes */
#define HV_POST_MESSAGE 0x005c
#define HV_SIGNAL_EVENT 0x005d

/* VMBus message connection ID */
#define VMBUS_MESSAGE_CONNECTION_ID 1   /* For VMBus < 5.0 */
#define VMBUS_MESSAGE_CONNECTION_ID_4 4 /* For VMBus >= 5.0 (Windows 10+) */
#define VMBUS_EVENT_CONNECTION_ID 2
#define VMBUS_MESSAGE_SINT 2 /* SynIC interrupt for VMBus 5.0+ */

/* Message types */
#define HVMSG_NONE 0
#define HVMSG_VMBUS 1

/* ============================================================================
 * VMBus Channel Message Types
 * ============================================================================
 */

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
#define CHANNELMSG_UNLOAD 16
#define CHANNELMSG_UNLOAD_RESPONSE 17

/* VMBus versions - Windows 11 uses 5.x with patches or 6.0 */
#define VMBUS_VERSION_WIN11_V6 0x00060000  /* Windows 11 (6.0) */
#define VMBUS_VERSION_WIN10_V53 0x00050003 /* Windows 10+ (5.3) */
#define VMBUS_VERSION_WIN10_V52 0x00050002 /* Windows 10+ (5.2) */
#define VMBUS_VERSION_WIN10_V51 0x00050001 /* Windows 10+ (5.1) */
#define VMBUS_VERSION_WIN10 0x00050000     /* Windows 10 (5.0) */
#define VMBUS_VERSION_WIN8_1 0x00040000    /* Windows 8.1 (4.0) */
#define VMBUS_VERSION_WIN8 0x00030000      /* Windows 8 (3.0) */
#define VMBUS_VERSION_WIN7 0x00020000      /* Windows 7 (2.0) */

/* ============================================================================
 * Data Structures (from iPXE/Linux kernel)
 * ============================================================================
 */

/* Posted message (HvPostMessage input) */
struct hv_post_message {
  uint32_t id; /* Connection ID */
  uint32_t reserved;
  uint32_t type;     /* Message type */
  uint32_t len;      /* Payload length */
  uint8_t data[240]; /* Payload */
} __attribute__((packed));

/* Received message (from SIMP) */
struct hv_message {
  uint32_t type; /* Message type */
  uint8_t len;   /* Payload length */
  uint8_t flags; /* Flags */
  uint16_t reserved;
  uint64_t origin;   /* Origin */
  uint8_t data[240]; /* Payload */
} __attribute__((packed));

/* GUID structure */
struct vmbus_guid {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
} __attribute__((packed));

/* INITIATE_CONTACT message */
/* INITIATE_CONTACT message (updated for VMBus 5.0+) */
struct vmbus_initiate_contact {
  uint32_t msgtype;
  uint32_t version;
  uint32_t target_vcpu;
  union {
    /* For VMBus < 5.0: GPA of interrupt page */
    uint64_t interrupt_page;
    /* For VMBus 5.0+: use these fields instead */
    struct {
      uint8_t msg_sint; /* SynIC interrupt number (use 2) */
      uint8_t msg_vtl;  /* Virtual Trust Level */
      uint8_t reserved1[6];
    };
  };
  uint64_t monitor_page1;
  uint64_t monitor_page2;
} __attribute__((packed));

/* VERSION_RESPONSE message (updated for VMBus 5.0+) */
struct vmbus_version_response {
  uint32_t msgtype;
  uint8_t version_supported;
  uint8_t status;
  uint16_t padding;
  uint32_t msg_conn_id; /* Connection ID to use for subsequent messages */
} __attribute__((packed));

/* REQUEST_OFFERS message */
struct vmbus_request_offers {
  uint32_t msgtype;
} __attribute__((packed));

/* OFFER_CHANNEL message */
struct vmbus_offer_channel {
  uint32_t msgtype;
  struct vmbus_guid if_type;
  struct vmbus_guid if_instance;
  uint64_t reserved1;
  uint64_t reserved2;
  uint16_t channel_flags;
  uint16_t mmio_megabytes;
  uint8_t user_def[120];
  uint16_t sub_channel_index;
  uint16_t reserved3;
  uint32_t connection_id;
  uint32_t child_relid;
  uint8_t monitor_id;
  uint8_t monitor_allocated;
  uint16_t is_dedicated_interrupt;
} __attribute__((packed));

/* OPEN_CHANNEL message */
struct vmbus_open_channel {
  uint32_t msgtype;
  uint32_t child_relid;
  uint32_t openid;
  uint32_t ring_buffer_gpadl;
  uint32_t target_vcpu;
  uint32_t downstream_ring_offset;
  uint8_t user_data[120];
} __attribute__((packed));

/* VMBus keyboard structure */
struct vmbus_keyboard {
  int initialized;
  int connected;
  uint32_t child_relid;
  uint32_t connection_id;
  void *ring_buffer;
  uint32_t ring_size;
};

/* Synthetic keyboard GUID: {F912AD6D-2B17-48EA-BD65-F927A61C7684} */
static const struct vmbus_guid kbd_guid = {
    .data1 = 0xF912AD6D,
    .data2 = 0x2B17,
    .data3 = 0x48EA,
    .data4 = {0xBD, 0x65, 0xF9, 0x27, 0xA6, 0x1C, 0x76, 0x84}};

/* ============================================================================
 * Global State
 * ============================================================================
 */

static uint8_t *g_hypercall_page = NULL;
static uint8_t *g_simp_page = NULL;
static uint8_t *g_siefp_page = NULL;
static uint8_t *g_int_page = NULL;
static uint8_t *g_monitor1 = NULL;
static uint8_t *g_monitor2 = NULL;
static int g_vmbus_initialized = 0;
static int g_vmbus_connected = 0;
static uint32_t g_msg_conn_id =
    VMBUS_MESSAGE_CONNECTION_ID; /* Connection ID for messages */
static struct vmbus_keyboard g_kbd = {0};

/* ============================================================================
 * Hypercall Interface
 * ============================================================================
 */

/* Execute hypercall via hypercall page */
static uint64_t hv_hypercall(uint64_t control, uint64_t input_addr,
                             uint64_t output_addr) {
  uint64_t result;
  uint64_t hc_addr = (uint64_t)(uintptr_t)g_hypercall_page;

  if (!g_hypercall_page)
    return (uint64_t)-1;

  /* Hyper-V x64 hypercall ABI: RCX=control, RDX=input, R8=output */
  register uint64_t r8 __asm__("r8") = output_addr;
  __asm__ volatile("call *%[page]"
                   : "=a"(result)
                   : "c"(control), "d"(input_addr), "r"(r8), [page] "r"(hc_addr)
                   : "memory", "r9", "r10", "r11");

  return result;
}

/* Post a message to VMBus */
static int hv_post_msg(void *msg, uint32_t len) {
  struct hv_post_message *pm;
  uint64_t status;

  if (len > 240)
    return -1;

  pm = (struct hv_post_message *)kmalloc_aligned(sizeof(*pm), 8);
  if (!pm)
    return -2;

  pm->id = VMBUS_MESSAGE_CONNECTION_ID;
  pm->reserved = 0;
  pm->type = HVMSG_VMBUS;
  pm->len = len;

  for (uint32_t i = 0; i < len; i++)
    pm->data[i] = ((uint8_t *)msg)[i];
  for (uint32_t i = len; i < 240; i++)
    pm->data[i] = 0;

  /* Execute HV_POST_MESSAGE hypercall */
  status = hv_hypercall(HV_POST_MESSAGE, (uint64_t)(uintptr_t)pm, 0);

  kfree_aligned(pm);

  /* Status in low 16 bits, 0 = success */
  return (status & 0xFFFF) == 0 ? 0 : -(int)(status & 0xFFFF);
}

/* Post a message to VMBus with specific connection ID */
static int hv_post_msg_with_id(void *msg, uint32_t len, uint32_t conn_id) {
  struct hv_post_message *pm;
  uint64_t status;

  if (len > 240)
    return -1;

  pm = (struct hv_post_message *)kmalloc_aligned(sizeof(*pm), 8);
  if (!pm)
    return -2;

  pm->id = conn_id; /* Use specified connection ID */
  pm->reserved = 0;
  pm->type = HVMSG_VMBUS;
  pm->len = len;

  for (uint32_t i = 0; i < len; i++)
    pm->data[i] = ((uint8_t *)msg)[i];
  for (uint32_t i = len; i < 240; i++)
    pm->data[i] = 0;

  /* Execute HV_POST_MESSAGE hypercall */
  status = hv_hypercall(HV_POST_MESSAGE, (uint64_t)(uintptr_t)pm, 0);

  kfree_aligned(pm);

  /* Status in low 16 bits, 0 = success */
  return (status & 0xFFFF) == 0 ? 0 : -(int)(status & 0xFFFF);
}

/* Wait for and read message from SIMP (checks all 16 slots) */
static int hv_wait_message(void *buf, uint32_t maxlen, int timeout_loops) {
  if (!g_simp_page || !buf)
    return -1;

  /* Debug: print SIMP page address */
  fbcon_print("[vmbus] wait_msg: SIMP=");
  fbcon_print_hex((uint64_t)(uintptr_t)g_simp_page);
  fbcon_print(" loops=");
  fbcon_print_hex(timeout_loops);
  fbcon_print("\n");

  /* Compiler barrier only - mfence can cause faults in early boot */
  __asm__ volatile("" ::: "memory");

  /* SIMP has 16 message slots, each 256 bytes */
  for (int i = 0; i < timeout_loops; i++) {
    /* Print progress every 10000 iterations */
    if (i == 0 || i == 100 || i == 1000 || i == 10000) {
      fbcon_print("[vmbus] loop i=");
      fbcon_print_hex(i);
      fbcon_print("\n");
    }

    for (int slot = 0; slot < 16; slot++) {
      volatile struct hv_message *msg =
          (volatile struct hv_message *)(g_simp_page + slot * 256);

      /* Volatile read to see host updates */
      uint32_t mtype = msg->type;
      if (mtype != 0) {
        /* Message received in this slot */
        uint32_t len = msg->len;
        if (len > maxlen)
          len = maxlen;
        if (len > 240)
          len = 240; /* Safety */

        for (uint32_t j = 0; j < len; j++)
          ((uint8_t *)buf)[j] = msg->data[j];

        /* Clear message (acknowledge) */
        msg->type = 0;
        __asm__ volatile("" ::: "memory");

        fbcon_print("[vmbus] msg received!\n");
        return (int)len;
      }
    }

    /* Short delay */
    for (volatile int d = 0; d < 500; d++)
      cpu_relax();
  }

  fbcon_print("[vmbus] loop done, no msg\n");
  return 0; /* Timeout */
}

/* ============================================================================
 * Hyper-V Detection
 * ============================================================================
 */

int hyperv_detect(void) {
  uint32_t eax, ebx, ecx, edx;
  cpuid(0x40000000, &eax, &ebx, &ecx, &edx);
  /* "Micr" "osof" "t Hv" */
  if (ebx == 0x7263694D && ecx == 0x666F736F && edx == 0x76482074)
    return 1;
  return 0;
}

/* ============================================================================
 * VMBus Initialization
 * ============================================================================
 */

static int hyperv_init_hypercall(void) {
  uint32_t eax, ebx, ecx, edx;

  /* Check hypercall available (bit 5 of features) */
  cpuid(0x40000003, &eax, &ebx, &ecx, &edx);
  if (!(eax & (1 << 5))) {
    fbcon_print("[vmbus] Hypercall nao suportado.\n");
    return -1;
  }

  /* Allocate hypercall page */
  g_hypercall_page = (uint8_t *)kmalloc_aligned(4096, 4096);
  if (!g_hypercall_page)
    return -2;
  for (int i = 0; i < 4096; i++)
    g_hypercall_page[i] = 0;

  /* Set Guest OS ID first (required) */
  /* Use open source ID format: bit 63=1, vendor=0x18ae (iPXE-style) */
  uint64_t guest_os_id = (1ULL << 63) | (0x18aeULL << 48) | 0x1;
  wrmsr(HV_X64_MSR_GUEST_OS_ID, guest_os_id);

  /* Enable hypercall page */
  uint64_t hc_msr =
      ((uint64_t)(uintptr_t)g_hypercall_page & ~0xFFFULL) | HV_HYPERCALL_ENABLE;
  wrmsr(HV_X64_MSR_HYPERCALL, hc_msr);

  /* Verify enabled */
  if (!(rdmsr(HV_X64_MSR_HYPERCALL) & HV_HYPERCALL_ENABLE)) {
    fbcon_print("[vmbus] Falha ao habilitar hypercall.\n");
    return -3;
  }

  return 0;
}

static int vmbus_init_synic(void) {
  /* Allocate SIMP (Synthetic Interrupt Message Page) */
  g_simp_page = (uint8_t *)kmalloc_aligned(4096, 4096);
  if (!g_simp_page)
    return -1;
  for (int i = 0; i < 4096; i++)
    g_simp_page[i] = 0;

  /* Allocate SIEFP (Synthetic Interrupt Event Flags Page) */
  g_siefp_page = (uint8_t *)kmalloc_aligned(4096, 4096);
  if (!g_siefp_page)
    return -2;
  for (int i = 0; i < 4096; i++)
    g_siefp_page[i] = 0;

  /* Allocate interrupt page (for VMBus) */
  g_int_page = (uint8_t *)kmalloc_aligned(4096, 4096);
  if (!g_int_page)
    return -3;
  for (int i = 0; i < 4096; i++)
    g_int_page[i] = 0;

  /* Allocate monitor pages */
  g_monitor1 = (uint8_t *)kmalloc_aligned(4096, 4096);
  g_monitor2 = (uint8_t *)kmalloc_aligned(4096, 4096);
  if (g_monitor1)
    for (int i = 0; i < 4096; i++)
      g_monitor1[i] = 0;
  if (g_monitor2)
    for (int i = 0; i < 4096; i++)
      g_monitor2[i] = 0;

  /* Enable SIMP */
  uint64_t simp =
      ((uint64_t)(uintptr_t)g_simp_page & ~0xFFFULL) | HV_SIMP_ENABLE;
  wrmsr(HV_X64_MSR_SIMP, simp);

  /* Enable SIEFP */
  uint64_t siefp =
      ((uint64_t)(uintptr_t)g_siefp_page & ~0xFFFULL) | HV_SIEFP_ENABLE;
  wrmsr(HV_X64_MSR_SIEFP, siefp);

  /* Configure SINT2 for VMBus (vector 0x22, unmasked, auto-EOI) */
  /* VMBus uses SINT2 by convention, vector 0x22 = 34 decimal */
  /* For VMBus 5.0+, we need SINT unmasked for message delivery */
  uint64_t sint2 = HV_SINT_AUTO_EOI | HV_SINT_VECTOR(0x22); /* NO MASK! */
  wrmsr(HV_X64_MSR_SINT2, sint2);

  /* Also configure SINT0 as backup for older VMBus versions */
  uint64_t sint0 = HV_SINT_AUTO_EOI | HV_SINT_VECTOR(0x20);
  wrmsr(HV_X64_MSR_SINT0, sint0);

  /* Enable SynIC */
  wrmsr(HV_X64_MSR_SCONTROL, HV_SCONTROL_ENABLE);

  return 0;
}

int vmbus_init(void) {
  if (!hyperv_detect())
    return -1;

  if (hyperv_init_hypercall() != 0) {
    fbcon_print("[vmbus] Falha no hypercall.\n");
    return -2;
  }
  fbcon_print("[vmbus] Hypercall OK.\n");

  if (vmbus_init_synic() != 0) {
    fbcon_print("[vmbus] Falha no SynIC.\n");
    return -3;
  }
  fbcon_print("[vmbus] SynIC OK.\n");

  g_vmbus_initialized = 1;
  return 0;
}

/* ============================================================================
 * VMBus Channel Negotiation
 * ============================================================================
 */

static int vmbus_negotiate_version(void) {
  struct vmbus_initiate_contact contact;
  struct vmbus_version_response response;
  /* Try newest versions first - Windows 11 may need 5.3+ or 6.0 */
  uint32_t versions[] = {
      VMBUS_VERSION_WIN11_V6,  /* 6.0 - Windows 11 */
      VMBUS_VERSION_WIN10_V53, /* 5.3 - Windows 10/11 */
      VMBUS_VERSION_WIN10_V52, /* 5.2 - Windows 10/11 */
      VMBUS_VERSION_WIN10_V51, /* 5.1 - Windows 10/11 */
      VMBUS_VERSION_WIN10,     /* 5.0 - Windows 10 */
      VMBUS_VERSION_WIN8_1,    /* 4.0 - Windows 8.1 */
      VMBUS_VERSION_WIN8,      /* 3.0 - Windows 8 */
      VMBUS_VERSION_WIN7       /* 2.0 - Windows 7 */
  };
  int num_versions = sizeof(versions) / sizeof(versions[0]);
  int ret;

  for (int v = 0; v < num_versions; v++) {
    uint32_t version = versions[v];
    uint32_t conn_id;

    /* Zero-initialize the contact message */
    for (int i = 0; i < (int)sizeof(contact); i++)
      ((uint8_t *)&contact)[i] = 0;

    contact.msgtype = CHANNELMSG_INITIATE_CONTACT;
    contact.version = version;
    contact.target_vcpu = 0;

    /* VMBus 5.0 (VERSION_WIN10_V5) and higher use different protocol */
    if (version >= VMBUS_VERSION_WIN10) {
      /* Use new format: msg_sint instead of interrupt_page */
      contact.msg_sint = VMBUS_MESSAGE_SINT;   /* SINT number (2) */
      contact.msg_vtl = 0;                     /* VTL 0 */
      conn_id = VMBUS_MESSAGE_CONNECTION_ID_4; /* Use connection ID 4 */
    } else {
      /* Use old format: interrupt_page GPA */
      contact.interrupt_page = (uint64_t)(uintptr_t)g_int_page;
      conn_id = VMBUS_MESSAGE_CONNECTION_ID; /* Use connection ID 1 */
    }

    contact.monitor_page1 = g_monitor1 ? (uint64_t)(uintptr_t)g_monitor1 : 0;
    contact.monitor_page2 = g_monitor2 ? (uint64_t)(uintptr_t)g_monitor2 : 0;

    fbcon_print("[vmbus] INITIATE_CONTACT v");
    fbcon_print_hex(version);
    fbcon_print(" conn_id=");
    fbcon_print_hex(conn_id);
    fbcon_print("...\n");

    ret = hv_post_msg_with_id(&contact, sizeof(contact), conn_id);
    if (ret != 0) {
      fbcon_print("[vmbus] Post falhou: ");
      fbcon_print_hex(-ret);
      fbcon_print("\n");
      continue;
    }
    fbcon_print("[vmbus] Post OK, aguardando resposta...\n");

    /* Wait for VERSION_RESPONSE - very short timeout to avoid Hyper-V watchdog
     */
    ret = hv_wait_message(&response, sizeof(response), 500);
    fbcon_print("[vmbus] wait_message retornou: ");
    fbcon_print_hex(ret);
    fbcon_print("\n");
    if (ret > 0) {
      fbcon_print("[vmbus] Msg recebida! tipo=");
      fbcon_print_hex(response.msgtype);
      fbcon_print("\n");
      if (response.msgtype == CHANNELMSG_VERSION_RESPONSE) {
        if (response.version_supported) {
          fbcon_print("[vmbus] Versao aceita! conn_id=");
          fbcon_print_hex(response.msg_conn_id);
          fbcon_print("\n");
          g_vmbus_connected = 1;
          g_msg_conn_id = response.msg_conn_id ? response.msg_conn_id : conn_id;
          return 0;
        }
        fbcon_print("[vmbus] Versao rejeitada.\n");
      }
    } else {
      fbcon_print("[vmbus] Timeout esperando resposta.\n");
    }
  }

  fbcon_print("[vmbus] Negociacao falhou, retornando -1.\n");
  return -1;
}

static int vmbus_request_offers(void) {
  struct vmbus_request_offers req;
  uint8_t msgbuf[256];
  int found_kbd = 0;
  int ret;

  req.msgtype = CHANNELMSG_REQUESTOFFERS;

  fbcon_print("[vmbus] REQUEST_OFFERS...\n");
  ret = hv_post_msg(&req, sizeof(req));
  if (ret != 0) {
    fbcon_print("[vmbus] Request falhou.\n");
    return -1;
  }

  /* Process offers */
  for (int i = 0; i < 20; i++) {
    ret = hv_wait_message(msgbuf, sizeof(msgbuf), 50000);
    if (ret <= 0) {
      if (i > 0)
        break; /* Timeout after receiving some */
      continue;
    }

    uint32_t msgtype = *(uint32_t *)msgbuf;

    if (msgtype == CHANNELMSG_OFFERCHANNEL) {
      struct vmbus_offer_channel *offer = (struct vmbus_offer_channel *)msgbuf;

      /* Check for keyboard GUID */
      if (offer->if_type.data1 == kbd_guid.data1 &&
          offer->if_type.data2 == kbd_guid.data2 &&
          offer->if_type.data3 == kbd_guid.data3) {
        g_kbd.child_relid = offer->child_relid;
        g_kbd.connection_id = offer->connection_id;
        fbcon_print("[vmbus] TECLADO encontrado! relid=");
        fbcon_print_hex(g_kbd.child_relid);
        fbcon_print("\n");
        found_kbd = 1;
      }
    } else if (msgtype == CHANNELMSG_ALLOFFERS_DELIVERED) {
      fbcon_print("[vmbus] Todas ofertas recebidas.\n");
      break;
    }
  }

  return found_kbd ? 0 : -2;
}

/* ============================================================================
 * Keyboard Interface
 * ============================================================================
 */

int vmbus_keyboard_init(struct vmbus_keyboard *kbd) {
  if (!kbd || !g_vmbus_connected)
    return -1;

  /* TODO: Open channel and negotiate keyboard protocol */
  /* For now, mark as initialized */
  kbd->initialized = 1;
  return 0;
}

int vmbus_keyboard_poll(struct vmbus_keyboard *kbd, uint8_t *scancode,
                        int *is_break) {
  (void)kbd;
  (void)scancode;
  (void)is_break;
  /* TODO: Poll ring buffer for keyboard events */
  return 0;
}

struct vmbus_keyboard *vmbus_get_keyboard(void) {
  return g_kbd.initialized ? &g_kbd : NULL;
}

/* ============================================================================
 * High-Level Init
 * ============================================================================
 */

int hyperv_keyboard_init(void) {
  if (!hyperv_detect())
    return -1;

  fbcon_print("[vmbus] === Driver VMBus Hyper-V ===\n");

  if (vmbus_init() != 0) {
    fbcon_print("[vmbus] Falha na inicializacao.\n");
    return -2;
  }

  if (vmbus_negotiate_version() != 0) {
    fbcon_print("[vmbus] Falha na negociacao de versao.\n");
    return -3;
  }

  if (vmbus_request_offers() != 0) {
    fbcon_print("[vmbus] Teclado nao encontrado.\n");
    return -4;
  }

  if (vmbus_keyboard_init(&g_kbd) != 0) {
    return -5;
  }

  fbcon_print("[vmbus] Teclado VMBus configurado!\n");
  return 0;
}
