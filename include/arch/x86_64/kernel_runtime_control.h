#ifndef ARCH_X86_64_KERNEL_RUNTIME_CONTROL_H
#define ARCH_X86_64_KERNEL_RUNTIME_CONTROL_H

int x64_kernel_manual_prepare_hyperv_input(void);
int x64_kernel_manual_prepare_hyperv_storage(void);
int x64_kernel_manual_prepare_native_bridge(void);
int x64_kernel_manual_prepare_hyperv_synic(void);
int x64_kernel_manual_try_exit_boot_services(void);
int x64_kernel_manual_native_runtime_step(void);

#endif /* ARCH_X86_64_KERNEL_RUNTIME_CONTROL_H */
