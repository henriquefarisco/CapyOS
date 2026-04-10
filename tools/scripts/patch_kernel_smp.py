#!/usr/bin/env python3
"""Integrate SMP detection and preemptive scheduler into kernel_main.c."""

path = "/Volumes/CapyOS/src/arch/x86_64/kernel_main.c"
with open(path, "rb") as f:
    data = f.read()

changes = 0

# 1. Add SMP include after scheduler.h
for nl in [b"\r\n", b"\n"]:
    marker = b'#include "kernel/scheduler.h"' + nl
    new_inc = marker + b'#include "arch/x86_64/smp.h"' + nl + b'#include "core/boot_metrics.h"' + nl
    if marker in data and b'"arch/x86_64/smp.h"' not in data:
        data = data.replace(marker, new_inc, 1)
        changes += 1
        break

# 2. Add boot_metrics_init + smp_detect after apic_init block (Stage 3)
for nl in [b"\r\n", b"\n"]:
    marker = b'    klog(KLOG_INFO, "[apic] Local APIC initialized.");' + nl + b'  }' + nl
    if marker in data and b'smp_detect_cpus' not in data:
        new_block = (
            marker +
            b'  boot_metrics_init();' + nl +
            b'  if (apic_available()) {' + nl +
            b'    smp_detect_cpus(h->rsdp);' + nl +
            b'  }' + nl
        )
        data = data.replace(marker, new_block, 1)
        changes += 1
        break

with open(path, "wb") as f:
    f.write(data)
print(f"kernel_main.c SMP patched OK ({changes} changes)")
