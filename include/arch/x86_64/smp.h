#ifndef ARCH_X86_64_SMP_H
#define ARCH_X86_64_SMP_H

#include <stdint.h>
#include <stddef.h>

#define SMP_MAX_CPUS 16

enum cpu_state {
  CPU_STATE_OFFLINE = 0,
  CPU_STATE_STARTING,
  CPU_STATE_ONLINE,
  CPU_STATE_IDLE,
  CPU_STATE_HALTED
};

struct cpu_info {
  uint32_t apic_id;
  uint32_t cpu_index;
  enum cpu_state state;
  int is_bsp;
  uint64_t *kernel_stack;
  uint64_t ticks;
  uint32_t current_task_pid;
};

struct smp_info {
  uint32_t cpu_count;
  uint32_t bsp_apic_id;
  uint32_t online_count;
  struct cpu_info cpus[SMP_MAX_CPUS];
};

void smp_init(void);
int smp_detect_cpus(uint64_t rsdp_addr);
int smp_start_aps(void);
uint32_t smp_cpu_count(void);
uint32_t smp_online_count(void);
struct cpu_info *smp_current_cpu(void);
struct cpu_info *smp_cpu_at(uint32_t index);
void smp_get_info(struct smp_info *out);
int smp_available(void);

#endif /* ARCH_X86_64_SMP_H */
