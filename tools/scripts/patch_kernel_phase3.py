#!/usr/bin/env python3
"""Integrate PMM, VMM, syscall_init into kernel_main.c boot path."""

path = "/Volumes/CapyOS/src/arch/x86_64/kernel_main.c"
with open(path, "rb") as f:
    data = f.read()

changes = 0

# 1. Add VMM/syscall/process includes
for nl in [b"\r\n", b"\n"]:
    marker = b'#include "core/boot_metrics.h"' + nl
    if marker in data and b'"memory/vmm.h"' not in data:
        new_inc = (
            marker +
            b'#include "memory/vmm.h"' + nl +
            b'#include "kernel/syscall.h"' + nl +
            b'#include "kernel/process.h"' + nl +
            b'#include "fs/capyfs_journal_integration.h"' + nl
        )
        data = data.replace(marker, new_inc, 1)
        changes += 1
        break

# 2. Add vmm_init + syscall_init + process_system_init after smp_detect_cpus
for nl in [b"\r\n", b"\n"]:
    marker = b"    smp_detect_cpus(h->rsdp);" + nl + b"  }" + nl
    if marker in data and b"vmm_init()" not in data:
        new_block = (
            marker +
            b"  vmm_init();" + nl +
            b"  klog(KLOG_INFO, \"[vmm] Virtual memory manager initialized.\");" + nl +
            b"  process_system_init();" + nl +
            b"  if (!handoff_boot_services_active()) {" + nl +
            b"    syscall_init();" + nl +
            b"    klog(KLOG_INFO, \"[syscall] Syscall ABI registered.\");" + nl +
            b"  }" + nl
        )
        data = data.replace(marker, new_block, 1)
        changes += 1
        break

with open(path, "wb") as f:
    f.write(data)
print(f"Phase 3 kernel patch: {changes} changes")
