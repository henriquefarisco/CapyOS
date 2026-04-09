#include "vmbus_transport.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "core/klog.h"
#include "drivers/hyperv/hyperv.h"

extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree_aligned(void *ptr);
extern void fbcon_print(const char *s);
extern void fbcon_print_hex(uint64_t val);

#define HV_HYPERCALL_ENABLE 0x00000001UL
#define HV_SCONTROL_ENABLE 0x00000001UL
#define HV_SIEFP_ENABLE 0x00000001UL
#define HV_SIMP_ENABLE 0x00000001UL
#define HV_SINT_MASKED 0x00010000UL
#define HV_SINT_AUTO_EOI 0x00020000UL
#define HV_SINT_VECTOR(x) ((x) << 0)
#define HV_X64_MSR_SINT2 0x40000092

/* IA32 APIC Base MSR and x2APIC EOI MSR (per Intel SDM Vol 3). Using x2APIC
 * EOI lets us acknowledge the local APIC from an ISR via a single wrmsr
 * without any MMIO mapping. We enable x2APIC once at SynIC init when we need
 * the manual-EOI path (i.e., when the host advertises HV_DEPRECATING_AEOI_
 * RECOMMENDED). */
#define IA32_APIC_BASE_MSR 0x0000001BU
#define IA32_APIC_BASE_EN (1ULL << 11)
#define IA32_APIC_BASE_EXTD (1ULL << 10)
#define X2APIC_MSR_EOI 0x0000080BU

#define HV_POST_MESSAGE 0x005c
#define HV_SIGNAL_EVENT 0x005d
#define HV_HYPERCALL_FAST_BIT (1u << 16)

#define HVMSG_VMBUS 1
#define HV_STATUS_SUCCESS 0x0000u
#define HV_STATUS_INSUFFICIENT_MEMORY 0x000Bu
#define HV_STATUS_INVALID_CONNECTION_ID 0x0012u
#define HV_STATUS_INSUFFICIENT_BUFFERS 0x0013u
#define HV_STATUS_NO_RESOURCES 0x001Du
#define VMBUS_PAGE_SIZE 4096u
#define VMBUS_MESSAGE_SINT 2u
#define VMBUS_EVENT_CONNECTION_ID 2u
#define VMBUS_EVENT_IRQ 2
#define HV_EVENT_FLAGS_BYTE_COUNT 256u
#define HV_EVENT_FLAGS32_COUNT (HV_EVENT_FLAGS_BYTE_COUNT / sizeof(uint32_t))

struct hv_post_message {
  uint32_t id;
  uint32_t reserved;
  uint32_t type;
  uint32_t len;
  uint8_t data[240];
} __attribute__((packed));

struct hv_input_signal_event {
  uint32_t connection_id;
  uint16_t flag_number;
  uint16_t reserved;
} __attribute__((packed));

union hv_monitor_trigger_group {
  uint64_t as_uint64;
  struct {
    uint32_t pending;
    uint32_t armed;
  };
} __attribute__((packed));

union hv_monitor_trigger_state {
  uint32_t as_uint32;
  struct {
    uint32_t group_enable : 4;
    uint32_t reserved : 28;
  };
} __attribute__((packed));

struct hv_monitor_parameter {
  uint32_t connection_id;
  uint16_t flag_number;
  uint16_t reserved;
} __attribute__((packed));

struct hv_monitor_page {
  union hv_monitor_trigger_state trigger_state;
  uint32_t reserved1;
  union hv_monitor_trigger_group trigger_group[4];
  uint64_t reserved2[3];
  int32_t next_checktime[4][32];
  uint16_t latency[4][32];
  uint64_t reserved3[32];
  struct hv_monitor_parameter parameter[4][32];
  uint8_t reserved4[1984];
} __attribute__((packed));

struct hv_message {
  uint32_t type;
  uint8_t len;
  uint8_t flags;
  uint16_t reserved;
  uint64_t origin;
  uint8_t data[240];
} __attribute__((packed));

static uint8_t *g_hypercall_page = NULL;
static uint8_t *g_simp_page = NULL;
static uint8_t *g_siefp_page = NULL;
static uint8_t *g_int_page = NULL;
static uint8_t *g_monitor1 = NULL;
static uint8_t *g_monitor2 = NULL;
static int g_vmbus_hypercall_ready = 0;
static int g_vmbus_synic_ready = 0;
static int g_vmbus_transport_initialized = 0;
static int g_vmbus_synic_irq_registered = 0;
static int g_vmbus_non_vmbus_msg_logged = 0;
static volatile uint32_t g_vmbus_isr_count = 0;
static volatile int g_vmbus_eoi_pending = 0;
/* Windows 10 20H1+/Server 2019+ set HV_DEPRECATING_AEOI_RECOMMENDED in
 * CPUID 0x40000004.EAX[9]. On those hosts HV_SINT_AUTO_EOI is ignored by the
 * hypervisor and the guest MUST manually write an EOI to the local APIC at
 * the end of the SINT ISR (mirroring Linux's sysvec_hyperv_callback path).
 *
 * NOTE: The end-of-message MSR (HV_X64_MSR_EOM) is NOT a substitute for LAPIC
 * EOI. EOM only advances the per-SINT SIMP message queue and must only be
 * issued when a SIMP slot has just been cleared. Issuing EOM from the top of
 * the ISR (before any SIMP drain) is undefined behavior on modern Hyper-V and
 * will produce an immediate fault cascade culminating in a triple fault. We
 * therefore drive LAPIC EOI via x2APIC from the ISR and leave the existing
 * SIMP drain paths untouched — they keep calling vmbus_signal_eom() only when
 * the relevant SIMP slot was actually in-service. */
static int g_vmbus_aeoi_deprecated = 0;
static int g_vmbus_manual_eoi = 0;

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx) {
  __asm__ volatile("cpuid"
                   : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                   : "a"(leaf), "c"(0));
}

static inline void cpu_relax(void) { __asm__ volatile("pause" ::: "memory"); }

static inline void vmbus_signal_eom(void) {
  wrmsr(HV_X64_MSR_EOM, 0);
}

/* x2APIC EOI — valid only after x2APIC has been enabled (see
 * vmbus_enable_x2apic_for_manual_eoi). Writing 0 to MSR 0x80B clears the
 * highest in-service bit in the LAPIC, which is the vector we are currently
 * servicing (0x22 for VMBus events). This is the exact counterpart of the
 * "ack_APIC_irq()" call Linux performs at the end of
 * sysvec_hyperv_callback when HV_DEPRECATING_AEOI_RECOMMENDED is set. */
static inline void vmbus_lapic_eoi_manual(void) {
  wrmsr(X2APIC_MSR_EOI, 0);
}

/* Called by the main thread after consuming a SIMP message (or on timeout)
 * to clear the LAPIC ISR bit and allow the next SINT to be delivered.
 *
 * IMPORTANT: We must only issue the wrmsr when the ISR has actually fired
 * and left ISR[0x22] set (indicated by g_vmbus_eoi_pending).  Writing EOI
 * with no in-service bit triggers #GP on Hyper-V's virtual LAPIC, which
 * cascades into a triple-fault reboot.
 *
 * The flag is cleared BEFORE the wrmsr so that if a new SINT fires
 * immediately after the EOI (ISR bit now clear → deliverable), the ISR
 * re-sets the flag and the next vmbus_transport_eoi() call will handle it. */
static void vmbus_transport_eoi(void) {
  if (g_vmbus_eoi_pending) {
    g_vmbus_eoi_pending = 0;
    __asm__ volatile("" ::: "memory");
    vmbus_lapic_eoi_manual();
  }
}

static int vmbus_enable_x2apic_for_manual_eoi(void) {
  uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
  uint64_t apic_base = 0;

  /* Require CPUID.1:ECX[21] (x2APIC) so we can safely touch MSR 0x80B. */
  cpuid(1u, &eax, &ebx, &ecx, &edx);
  if (!(ecx & (1u << 21))) {
    fbcon_print("[vmbus] x2APIC indisponivel; mantendo caminho AUTO_EOI.\n");
    return -1;
  }

  apic_base = rdmsr(IA32_APIC_BASE_MSR);
  if (!(apic_base & IA32_APIC_BASE_EN)) {
    fbcon_print("[vmbus] LAPIC desativado; nao e possivel usar EOI manual.\n");
    return -2;
  }

  /* Per Intel SDM Vol 3 10.12.5 the xAPIC->x2APIC transition on modern CPUs
   * (Sandy Bridge+) is a single-step: set EXTD while EN stays set. Hyper-V
   * Gen2 exposes this direct transition on every supported SKU. */
  if (!(apic_base & IA32_APIC_BASE_EXTD)) {
    wrmsr(IA32_APIC_BASE_MSR, apic_base | IA32_APIC_BASE_EXTD);
    apic_base = rdmsr(IA32_APIC_BASE_MSR);
    if (!(apic_base & IA32_APIC_BASE_EXTD)) {
      fbcon_print("[vmbus] Falha ao habilitar x2APIC; revertendo para AUTO_EOI.\n");
      return -3;
    }
  }

  /* Verify LAPIC software-enable bit (SVR bit 8) is set. In x2APIC mode,
   * the SVR is accessed via MSR 0x80F. If bit 8 is clear the LAPIC will
   * not deliver any interrupt, including SINTs. */
  {
    uint64_t svr = rdmsr(0x80Fu);
    fbcon_print("[vmbus] x2APIC SVR=");
    fbcon_print_hex(svr);
    if (!(svr & (1u << 8))) {
      fbcon_print(" (LAPIC DISABLED, enabling)");
      wrmsr(0x80Fu, svr | (1u << 8));
      svr = rdmsr(0x80Fu);
      fbcon_print(" new SVR=");
      fbcon_print_hex(svr);
    }
    fbcon_print("\n");
  }

  return 0;
}

static inline uint32_t vmbus_atomic_xchg_u32(volatile uint32_t *ptr,
                                             uint32_t value) {
  __asm__ volatile("xchgl %0, %1"
                   : "+r"(value), "+m"(*ptr)
                   :
                   : "memory");
  return value;
}

static inline void vmbus_atomic_or_u32(volatile uint32_t *ptr, uint32_t value) {
  __asm__ volatile("lock orl %1, %0" : "+m"(*ptr) : "ir"(value) : "memory");
}

static inline volatile struct hv_message *vmbus_simp_slot(uint32_t sint) {
  return (volatile struct hv_message *)(g_simp_page + ((sint & 0xFu) * 256u));
}

static int vmbus_consume_simp_slot(uint32_t sint, void *buf, uint32_t maxlen) {
  volatile struct hv_message *msg;
  uint32_t mtype;
  uint32_t len;
  uint8_t flags;

  if (!g_simp_page) {
    return -1;
  }

  msg = vmbus_simp_slot(sint);
  mtype = msg->type;
  if (mtype == 0u) {
    return 0;
  }

  len = msg->len;
  flags = msg->flags;
  if (mtype != HVMSG_VMBUS) {
    if (!g_vmbus_non_vmbus_msg_logged) {
      fbcon_print("[vmbus] Ignorando SIMP msg nao-VMBus no SINT=");
      fbcon_print_hex((uint64_t)sint);
      fbcon_print(" type=");
      fbcon_print_hex((uint64_t)mtype);
      fbcon_print(" len=");
      fbcon_print_hex((uint64_t)len);
      fbcon_print("\n");
      g_vmbus_non_vmbus_msg_logged = 1;
    }
    msg->type = 0;
    __asm__ volatile("" ::: "memory");
    if ((flags & 0x1u) != 0u) {
      vmbus_signal_eom();
    }
    vmbus_transport_eoi();
    return -2;
  }

  /* Log HVMSG_VMBUS message details for diagnostics. */
  fbcon_print("[vmbus] SIMP SINT=");
  fbcon_print_hex((uint64_t)sint);
  fbcon_print(" hvtype=");
  fbcon_print_hex((uint64_t)mtype);
  fbcon_print(" len=");
  fbcon_print_hex((uint64_t)len);
  fbcon_print(" flags=");
  fbcon_print_hex((uint64_t)flags);
  fbcon_print("\n");

  if (!buf) {
    msg->type = 0;
    __asm__ volatile("" ::: "memory");
    if ((flags & 0x1u) != 0u) {
      vmbus_signal_eom();
    }
    vmbus_transport_eoi();
    return 1;
  }

  if (len > maxlen) {
    len = maxlen;
  }
  if (len > 240u) {
    len = 240u;
  }
  for (uint32_t j = 0; j < len; ++j) {
    ((uint8_t *)buf)[j] = msg->data[j];
  }
  msg->type = 0;
  __asm__ volatile("" ::: "memory");
  if ((flags & 0x1u) != 0u) {
    vmbus_signal_eom();
  }
  vmbus_transport_eoi();
  return (int)len;
}

static void vmbus_transport_synic_irq(void) {
  volatile uint32_t *flags32 = NULL;

  /* NOTE: We must NOT call fbcon_print (or any non-reentrant function) from
   * this ISR. The main thread may already be inside fbcon_print when the
   * SINT fires; reentering it corrupts internal cursor/state and can cascade
   * into a triple-fault reboot. All diagnostics are deferred to the main
   * thread which reads g_vmbus_isr_count. */

  /* Drain the per-SINT event-flag block. This has to run regardless of the
   * EOI path because the host expects the guest to clear the flag bits it
   * just raised; leaving them set would mask subsequent edge events. */
  if (g_siefp_page) {
    flags32 = (volatile uint32_t *)(void *)(g_siefp_page +
                                            (VMBUS_MESSAGE_SINT *
                                             HV_EVENT_FLAGS_BYTE_COUNT));
    for (uint32_t i = 0; i < HV_EVENT_FLAGS32_COUNT; ++i) {
      if (flags32[i] != 0u) {
        (void)vmbus_atomic_xchg_u32(&flags32[i], 0u);
      }
    }
  }

  ++g_vmbus_isr_count;

  /* LAPIC EOI strategy (HV_DEPRECATING_AEOI_RECOMMENDED path):
   *
   * We deliberately do NOT issue LAPIC EOI here.  On Hyper-V the SynIC
   * re-asserts the SINT as long as the SIMP slot is non-empty.  If we EOI
   * in the ISR before the main thread drains the SIMP slot, the interrupt
   * fires again immediately after iretq, creating an infinite storm that
   * starves the main thread and prevents it from ever consuming the message.
   *
   * Instead we leave the LAPIC ISR[0x22] bit SET and signal the main thread
   * via g_vmbus_eoi_pending.  The main thread calls vmbus_transport_eoi()
   * which only performs the actual wrmsr when the flag is set, avoiding a
   * #GP from writing EOI with no in-service bit. */
  if (g_vmbus_manual_eoi) {
    g_vmbus_eoi_pending = 1;
  }
}

static uint64_t hv_hypercall(uint64_t control, uint64_t input_addr,
                             uint64_t output_addr) {
  uint64_t result;
  uint64_t hc_addr = (uint64_t)(uintptr_t)g_hypercall_page;
  register uint64_t r8 __asm__("r8") = output_addr;

  if (!g_hypercall_page) {
    return (uint64_t)-1;
  }

  __asm__ volatile("call *%[page]"
                   : "=a"(result)
                   : "c"(control), "d"(input_addr), "r"(r8), [page] "r"(hc_addr)
                   : "memory", "r9", "r10", "r11");
  return result;
}

static uint64_t hv_fast_hypercall8(uint64_t control, uint64_t input8) {
  uint64_t result;
  uint64_t hc_addr = (uint64_t)(uintptr_t)g_hypercall_page;
  register uint64_t r8 __asm__("r8") = 0;

  if (!g_hypercall_page) {
    return (uint64_t)-1;
  }

  control |= HV_HYPERCALL_FAST_BIT;
  __asm__ volatile("call *%[page]"
                   : "=a"(result)
                   : "c"(control), "d"(input8), "r"(r8), [page] "r"(hc_addr)
                   : "memory", "r9", "r10", "r11");
  return result;
}

static int hyperv_init_hypercall(void) {
  uint32_t eax, ebx, ecx, edx;
  uint64_t guest_os_id;
  uint64_t hc_msr;

  cpuid(0x40000003, &eax, &ebx, &ecx, &edx);
  if (!(eax & (1u << 5))) {
    fbcon_print("[vmbus] Hypercall nao suportado.\n");
    return -1;
  }

  g_hypercall_page = (uint8_t *)kmalloc_aligned(4096, 4096);
  if (!g_hypercall_page) {
    return -2;
  }
  for (int i = 0; i < 4096; ++i) {
    g_hypercall_page[i] = 0;
  }

  guest_os_id = (1ULL << 63) | (0x18aeULL << 48) | 0x1;
  wrmsr(HV_X64_MSR_GUEST_OS_ID, guest_os_id);

  hc_msr = ((uint64_t)(uintptr_t)g_hypercall_page & ~0xFFFULL) |
           HV_HYPERCALL_ENABLE;
  wrmsr(HV_X64_MSR_HYPERCALL, hc_msr);
  if (!(rdmsr(HV_X64_MSR_HYPERCALL) & HV_HYPERCALL_ENABLE)) {
    fbcon_print("[vmbus] Falha ao habilitar hypercall.\n");
    return -3;
  }

  return 0;
}

static int vmbus_init_synic(void) {
  uint64_t simp;
  uint64_t siefp;
  uint64_t sint2;
  uint64_t sint0;

  g_simp_page = (uint8_t *)kmalloc_aligned(4096, 4096);
  if (!g_simp_page) {
    return -1;
  }
  for (int i = 0; i < 4096; ++i) {
    g_simp_page[i] = 0;
  }

  g_siefp_page = (uint8_t *)kmalloc_aligned(4096, 4096);
  if (!g_siefp_page) {
    return -2;
  }
  for (int i = 0; i < 4096; ++i) {
    g_siefp_page[i] = 0;
  }

  g_int_page = (uint8_t *)kmalloc_aligned(4096, 4096);
  if (!g_int_page) {
    return -3;
  }
  for (int i = 0; i < 4096; ++i) {
    g_int_page[i] = 0;
  }

  g_monitor1 = (uint8_t *)kmalloc_aligned(4096, 4096);
  g_monitor2 = (uint8_t *)kmalloc_aligned(4096, 4096);
  if (g_monitor1) {
    for (int i = 0; i < 4096; ++i) {
      g_monitor1[i] = 0;
    }
  }
  if (g_monitor2) {
    for (int i = 0; i < 4096; ++i) {
      g_monitor2[i] = 0;
    }
  }

  /* Per the Hyper-V TLFS, SCONTROL must be enabled FIRST so that
   * subsequent SIMP, SIEFP, and SINT MSR writes are processed by the
   * hypervisor.  Configuring SINTs before enabling SCONTROL causes
   * registrations to be silently discarded. */
  wrmsr(HV_X64_MSR_SCONTROL, HV_SCONTROL_ENABLE);

  simp = ((uint64_t)(uintptr_t)g_simp_page & ~0xFFFULL) | HV_SIMP_ENABLE;
  wrmsr(HV_X64_MSR_SIMP, simp);

  siefp = ((uint64_t)(uintptr_t)g_siefp_page & ~0xFFFULL) | HV_SIEFP_ENABLE;
  wrmsr(HV_X64_MSR_SIEFP, siefp);

  /* Diagnostic: compare VA (what the kernel reads) with the GPA written to
   * the MSR (what the hypervisor writes to). If VA != GPA the guest will
   * never see SIMP messages because identity mapping is broken. */
  fbcon_print("[vmbus] SIMP VA=");
  fbcon_print_hex((uint64_t)(uintptr_t)g_simp_page);
  fbcon_print(" GPA=");
  fbcon_print_hex(simp & ~0xFFFULL);
  fbcon_print(" SIEFP VA=");
  fbcon_print_hex((uint64_t)(uintptr_t)g_siefp_page);
  fbcon_print(" GPA=");
  fbcon_print_hex(siefp & ~0xFFFULL);
  fbcon_print("\n");

  /* Detect HV_DEPRECATING_AEOI_RECOMMENDED (CPUID 0x40000004.EAX[9]). Modern
   * Hyper-V (Win 10 20H1+, Server 2019+, Win 11) silently ignores the
   * HV_SINT_AUTO_EOI bit; programming it leaves the LAPIC ISR[0x22] bit set
   * forever and blocks every subsequent SINT, which is exactly the freeze
   * observed right after HvCallSignalEvent. On those hosts we have to
   * acknowledge the LAPIC ourselves. */
  {
    uint32_t rec_eax = 0, rec_ebx = 0, rec_ecx = 0, rec_edx = 0;
    cpuid(0x40000004u, &rec_eax, &rec_ebx, &rec_ecx, &rec_edx);
    g_vmbus_aeoi_deprecated = (rec_eax & (1u << 9)) ? 1 : 0;
  }

  /* If AUTO_EOI is deprecated, try to enable x2APIC so the ISR can issue
   * LAPIC EOI via a plain wrmsr on MSR 0x80B. If that fails for any reason
   * (no x2APIC support, LAPIC disabled, host rejects the transition) we
   * fall back to the legacy AUTO_EOI path — it may still work on hosts that
   * only set the recommendation bit without actively ignoring AUTO_EOI. */
  g_vmbus_manual_eoi = 0;
  if (g_vmbus_aeoi_deprecated) {
    if (vmbus_enable_x2apic_for_manual_eoi() == 0) {
      g_vmbus_manual_eoi = 1;
    }
  }

  sint2 = HV_SINT_VECTOR(0x22);
  if (!g_vmbus_manual_eoi) {
    sint2 |= HV_SINT_AUTO_EOI;
  }
  wrmsr(HV_X64_MSR_SINT2, sint2);

  /* Readback SINT2 to verify the hypervisor accepted our vector/flags. */
  {
    uint64_t sint2_rb = rdmsr(HV_X64_MSR_SINT2);
    fbcon_print("[vmbus] SINT2 rb=");
    fbcon_print_hex(sint2_rb);
    fbcon_print(" (vec=");
    fbcon_print_hex(sint2_rb & 0xFFu);
    fbcon_print(" masked=");
    fbcon_print_hex((sint2_rb >> 16) & 1u);
    fbcon_print(" aeoi=");
    fbcon_print_hex((sint2_rb >> 17) & 1u);
    fbcon_print(")\n");
  }

  /* VMBus usa apenas o SINT2. Deixar o SINT0 ativo em 0x20 conflita com o
   * vetor do PIT/IRQ0 e abre espaco para o guest tratar mensagens nao-VMBus
   * como se fossem respostas do barramento. */
  sint0 = HV_SINT_MASKED;
  wrmsr(HV_X64_MSR_SINT0, sint0);

  /* Readback SIMP/SIEFP to validate the hypervisor accepted our pages.
   * This pattern mirrors what Linux does after SynIC MSR writes. */
  {
    uint64_t simp_rb = rdmsr(HV_X64_MSR_SIMP);
    uint64_t siefp_rb = rdmsr(HV_X64_MSR_SIEFP);
    if (!(simp_rb & HV_SIMP_ENABLE)) {
      klog(KLOG_ERROR, "[vmbus] SIMP readback: hypervisor did NOT accept SIMP page!");
      klog_hex(KLOG_ERROR, "[vmbus] SIMP readback=", simp_rb);
      return -4;
    }
    if (!(siefp_rb & HV_SIEFP_ENABLE)) {
      klog(KLOG_ERROR, "[vmbus] SIEFP readback: hypervisor did NOT accept SIEFP page!");
      klog_hex(KLOG_ERROR, "[vmbus] SIEFP readback=", siefp_rb);
      return -5;
    }
    klog(KLOG_INFO, "[vmbus] SynIC MSR readback OK: SIMP and SIEFP accepted.");
  }

  if (g_vmbus_manual_eoi) {
    klog(KLOG_INFO,
         "[vmbus] AUTO_EOI deprecated; x2APIC habilitado, ISR fara LAPIC EOI manual (sem EOM).");
    fbcon_print(
        "[vmbus] AUTO_EOI deprecated: usando x2APIC EOI manual na ISR.\n");
  } else if (g_vmbus_aeoi_deprecated) {
    klog(KLOG_INFO,
         "[vmbus] AUTO_EOI deprecated mas x2APIC indisponivel; usando AUTO_EOI como fallback.");
    fbcon_print(
        "[vmbus] AUTO_EOI deprecated e x2APIC indisponivel; fallback AUTO_EOI.\n");
  } else {
    klog(KLOG_INFO,
         "[vmbus] Host permite AUTO_EOI; SINTs com bit AEOI, ISR sem EOI manual.");
    fbcon_print("[vmbus] Host permite AUTO_EOI; mantendo bit AEOI.\n");
  }

  if (!g_vmbus_synic_irq_registered) {
    irq_install_handler(VMBUS_EVENT_IRQ, vmbus_transport_synic_irq);
    g_vmbus_synic_irq_registered = 1;
  }

  fbcon_print("[vmbus] SINT0 mascarado; SINT2 dedicado ao VMBus.\n");

  return 0;
}

int vmbus_transport_init(void) {
  if (g_vmbus_transport_initialized) {
    return 0;
  }
  if (vmbus_transport_prepare_hypercall() != 0) {
    fbcon_print("[vmbus] Falha no hypercall.\n");
    return -1;
  }
  fbcon_print("[vmbus] Hypercall OK.\n");

  if (vmbus_transport_prepare_synic() != 0) {
    fbcon_print("[vmbus] Falha no SynIC.\n");
    return -2;
  }
  fbcon_print("[vmbus] SynIC OK.\n");

  g_vmbus_transport_initialized = 1;
  return 0;
}

int vmbus_transport_prepare_hypercall(void) {
  if (g_vmbus_hypercall_ready) {
    return 0;
  }
  if (hyperv_init_hypercall() != 0) {
    return -1;
  }
  g_vmbus_hypercall_ready = 1;
  return 0;
}

int vmbus_transport_hypercall_prepared(void) {
  return g_vmbus_hypercall_ready ? 1 : 0;
}

int vmbus_transport_prepare_synic(void) {
  if (!g_vmbus_hypercall_ready) {
    return -1;
  }
  if (g_vmbus_synic_ready) {
    return 0;
  }
  if (vmbus_init_synic() != 0) {
    return -2;
  }
  g_vmbus_synic_ready = 1;
  return 0;
}

int vmbus_transport_synic_ready(void) { return g_vmbus_synic_ready ? 1 : 0; }

int vmbus_transport_post_msg(void *msg, uint32_t len, uint32_t conn_id) {
  struct hv_post_message *pm;
  const struct vmbus_channel_message_header *hdr =
      (const struct vmbus_channel_message_header *)msg;
  uint64_t status;
  uint32_t code;
  uint32_t wait_loops = 64u;

  if (len > 240u) {
    return -1;
  }

  /* Per the Hyper-V TLFS, the HvPostMessage input buffer must not cross a
   * page boundary.  Align to page size to guarantee the 256-byte struct
   * stays within a single 4 KiB page. */
  pm = (struct hv_post_message *)kmalloc_aligned(sizeof(*pm), VMBUS_PAGE_SIZE);
  if (!pm) {
    return -2;
  }

  pm->id = conn_id;
  pm->reserved = 0;
  pm->type = HVMSG_VMBUS;
  pm->len = len;
  for (uint32_t i = 0; i < len; ++i) {
    pm->data[i] = ((const uint8_t *)msg)[i];
  }
  for (uint32_t i = len; i < 240u; ++i) {
    pm->data[i] = 0;
  }

  for (uint32_t attempt = 0; attempt < 200u; ++attempt) {
    status = hv_hypercall(HV_POST_MESSAGE, (uint64_t)(uintptr_t)pm, 0);
    code = (uint32_t)(status & 0xFFFFu);
    if (code == HV_STATUS_SUCCESS) {
      kfree_aligned(pm);
      return 0;
    }

    if (code != HV_STATUS_INVALID_CONNECTION_ID &&
        code != HV_STATUS_INSUFFICIENT_MEMORY &&
        code != HV_STATUS_INSUFFICIENT_BUFFERS &&
        code != HV_STATUS_NO_RESOURCES) {
      break;
    }

    if (code == HV_STATUS_INVALID_CONNECTION_ID &&
        hdr && hdr->msgtype == CHANNELMSG_INITIATE_CONTACT) {
      break;
    }

    for (uint32_t spin = 0; spin < wait_loops; ++spin) {
      cpu_relax();
    }
    if (wait_loops < 65536u) {
      wait_loops <<= 1;
    }
  }

  code = (uint32_t)(status & 0xFFFFu);
  fbcon_print("[vmbus] post_msg falhou status=");
  fbcon_print_hex((uint64_t)code);
  fbcon_print(" conn_id=");
  fbcon_print_hex((uint64_t)conn_id);
  fbcon_print(" msgtype=");
  fbcon_print_hex((uint64_t)(hdr ? hdr->msgtype : 0u));
  fbcon_print("\n");
  kfree_aligned(pm);
  return -(int)code;
}

void vmbus_transport_signal_relid(uint32_t relid) {
  volatile uint32_t *send_int_page =
      (volatile uint32_t *)(void *)(g_int_page + (VMBUS_PAGE_SIZE / 2u));
  uint32_t word = relid / 32u;
  uint32_t bit = relid % 32u;

  send_int_page[word] |= (uint32_t)(1u << bit);
}

void vmbus_transport_signal_monitor(uint8_t monitor_id) {
  volatile uint32_t *pending = NULL;
  uint32_t monitor_group = 0u;
  uint32_t monitor_bit = 0u;

  if (!g_monitor2) {
    return;
  }

  monitor_group = (uint32_t)monitor_id / 32u;
  monitor_bit = (uint32_t)monitor_id % 32u;
  if (monitor_group >= 4u) {
    return;
  }

  pending = (volatile uint32_t *)(void *)(g_monitor2 + 8u +
                                          (monitor_group * sizeof(uint64_t)));
  vmbus_atomic_or_u32(pending, (uint32_t)(1u << monitor_bit));
}

int vmbus_transport_signal_event(uint32_t connection_id) {
  uint64_t status = 0;
  uint32_t code = 0;

  /* HvCallSignalEvent is a simple hypercall whose interface is just
   * (ConnectionId, FlagNumber). For the normal Hyper-V guest path Linux uses
   * the fast 8-byte form with the channel/offer connection_id, which avoids
   * any extra GPA dependency for the hypercall input block itself.
   */
  status = hv_fast_hypercall8(HV_SIGNAL_EVENT, (uint64_t)connection_id);
  code = (uint32_t)(status & 0xFFFFu);
  return code == 0u ? 0 : -(int)code;
}

int vmbus_transport_wait_message(void *buf, uint32_t maxlen, int timeout_loops) {
  if (!g_simp_page || !buf) {
    return -1;
  }

  fbcon_print("[vmbus] wait_msg enter isr=");
  fbcon_print_hex((uint64_t)g_vmbus_isr_count);
  fbcon_print("\n");

  __asm__ volatile("" ::: "memory");
  for (int i = 0; i < timeout_loops; ++i) {
    int rc = vmbus_consume_simp_slot(VMBUS_MESSAGE_SINT, buf, maxlen);
    if (rc > 0) {
      return rc;
    }

    /* Progress diagnostic every 100000 iterations. */
    if (i != 0 && (i % 100000) == 0) {
      volatile struct hv_message *dbg = vmbus_simp_slot(VMBUS_MESSAGE_SINT);
      fbcon_print("[vmbus] poll i=");
      fbcon_print_hex((uint64_t)i);
      fbcon_print(" isr=");
      fbcon_print_hex((uint64_t)g_vmbus_isr_count);
      fbcon_print(" t=");
      fbcon_print_hex((uint64_t)dbg->type);
      fbcon_print("\n");
    }

    for (volatile int d = 0; d < 500; ++d) {
      cpu_relax();
    }
  }

  /* Timeout — release LAPIC ISR bit so future SINTs can still deliver. */
  vmbus_transport_eoi();
  fbcon_print("[vmbus] wait_msg timeout isr=");
  fbcon_print_hex((uint64_t)g_vmbus_isr_count);
  fbcon_print("\n");
  return 0;
}

uint64_t vmbus_transport_interrupt_page(void) {
  return g_int_page ? ((uint64_t)(uintptr_t)g_int_page >> 12) : 0;
}

uint64_t vmbus_transport_monitor_page1(void) {
  return g_monitor1 ? ((uint64_t)(uintptr_t)g_monitor1 >> 12) : 0;
}

uint64_t vmbus_transport_monitor_page2(void) {
  return g_monitor2 ? ((uint64_t)(uintptr_t)g_monitor2 >> 12) : 0;
}

void vmbus_transport_drain_simp(void) {
  if (!g_simp_page) {
    return;
  }

  __asm__ volatile("" ::: "memory");
  while (vmbus_consume_simp_slot(VMBUS_MESSAGE_SINT, NULL, 0u) != 0) {
  }
}
