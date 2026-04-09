#!/usr/bin/env python3
"""Final wiring: preemptive scheduler callback, desktop includes, SMP start."""

path = "/Volumes/CapyOS/src/arch/x86_64/kernel_main.c"
with open(path, "rb") as f:
    data = f.read()

changes = 0

# 1. Add desktop.h include after smp.h
for nl in [b"\r\n", b"\n"]:
    marker = b'#include "arch/x86_64/smp.h"' + nl
    if marker in data and b'"gui/desktop.h"' not in data:
        new_inc = (
            marker +
            b'#include "gui/desktop.h"' + nl +
            b'#include "drivers/input/mouse.h"' + nl +
            b'#include "drivers/rtc/rtc.h"' + nl +
            b'#include "drivers/gpu/gpu_core.h"' + nl +
            b'#include "drivers/usb/usb_core.h"' + nl +
            b'#include "core/auth_policy.h"' + nl
        )
        data = data.replace(marker, new_inc, 1)
        changes += 1
        break

# 2. Wire preemptive scheduler + auth_policy_init + gpu_detect + usb_core_init + mouse_ps2_init
#    after the smp_detect_cpus block
for nl in [b"\r\n", b"\n"]:
    marker = b"    syscall_init();" + nl + b'    klog(KLOG_INFO, "[syscall] Syscall ABI registered.");' + nl + b"  }" + nl
    if marker in data and b"apic_timer_set_callback" not in data:
        new_block = (
            b"    syscall_init();" + nl +
            b'    klog(KLOG_INFO, "[syscall] Syscall ABI registered.");' + nl +
            b"  }" + nl +
            b"  auth_policy_init();" + nl +
            b"  rtc_init();" + nl +
            b"  gpu_init();" + nl +
            b"  gpu_detect();" + nl +
            b"  usb_core_init();" + nl +
            b"  if (apic_available() && !handoff_boot_services_active()) {" + nl +
            b"    apic_timer_set_callback(scheduler_tick);" + nl +
            b"    apic_timer_start(100);" + nl +
            b'    klog(KLOG_INFO, "[scheduler] Preemptive tick armed at 100Hz.");' + nl +
            b"  }" + nl
        )
        data = data.replace(marker, new_block, 1)
        changes += 1
        break

with open(path, "wb") as f:
    f.write(data)
print(f"Final wiring: {changes} changes")
