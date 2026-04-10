#!/usr/bin/env python3
"""Integrate panic handler, APIC timer, task system and boot metrics into kernel_main.c."""

path = "/Volumes/CapyOS/src/arch/x86_64/kernel_main.c"
with open(path, "rb") as f:
    data = f.read()

changes = 0

# 1. Add includes for new subsystems after the last existing include
marker = b'#include "shell/core.h"\r\n'
new_includes = (
    b'#include "shell/core.h"\r\n'
    b'#include "arch/x86_64/panic.h"\r\n'
    b'#include "arch/x86_64/apic.h"\r\n'
    b'#include "kernel/task.h"\r\n'
    b'#include "kernel/scheduler.h"\r\n'
    b'#include "memory/pmm.h"\r\n'
    b'#include "net/dns_cache.h"\r\n'
    b'#include "net/socket.h"\r\n'
)
if marker in data and b'#include "arch/x86_64/panic.h"' not in data:
    data = data.replace(marker, new_includes, 1)
    changes += 1

# 2. Add panic_set_framebuffer call right after framebuffer setup (after g_con.row = 0)
fb_marker = b"  g_con.row = 0;\r\n\r\n  acpi_set_rsdp"
fb_new = (
    b"  g_con.row = 0;\r\n"
    b"\r\n"
    b"  /* Register framebuffer with panic handler for blue-screen on fault */\r\n"
    b"  panic_set_framebuffer(g_con.fb, g_con.width, g_con.height,\r\n"
    b"                        g_con.stride * 4);\r\n"
    b"\r\n"
    b"  acpi_set_rsdp"
)
if fb_marker in data:
    data = data.replace(fb_marker, fb_new, 1)
    changes += 1

# 3. Add task_system_init + scheduler_init + dns_cache_init + socket_system_init
#    after work_queue_init in Stage 2
wq_marker = b"  work_queue_init();\r\n  update_agent_init"
wq_new = (
    b"  work_queue_init();\r\n"
    b"  task_system_init();\r\n"
    b"  scheduler_init(SCHED_POLICY_COOPERATIVE);\r\n"
    b"  dns_cache_init();\r\n"
    b"  socket_system_init();\r\n"
    b"  update_agent_init"
)
if wq_marker in data:
    data = data.replace(wq_marker, wq_new, 1)
    changes += 1

# 4. Add APIC init after platform timer init (Stage 3), guarded by native runtime check
timer_marker = b"  x64_platform_timer_init(!handoff_boot_services_active());\r\n"
timer_new = (
    b"  x64_platform_timer_init(!handoff_boot_services_active());\r\n"
    b"  if (!handoff_boot_services_active() && apic_available()) {\r\n"
    b"    apic_init();\r\n"
    b"    klog(KLOG_INFO, \"[apic] Local APIC initialized.\");\r\n"
    b"  }\r\n"
)
if timer_marker in data and b"apic_init();" not in data:
    data = data.replace(timer_marker, timer_new, 1)
    changes += 1

with open(path, "wb") as f:
    f.write(data)
print(f"kernel_main.c patched OK ({changes} changes)")
