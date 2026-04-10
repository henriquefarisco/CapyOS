#!/usr/bin/env python3
"""Add all Phase 3-8 object files to CAPYOS64_OBJS in Makefile."""

path = "/Volumes/CapyOS/Makefile"
with open(path, "rb") as f:
    data = f.read()

marker = b"$(BUILD)/x86_64/arch/x86_64/smp.o"

new_objs = [
    "core/auth_policy",
    "kernel/pipe",
    "drivers/usb/usb_core",
    "drivers/gpu/gpu_core",
    "drivers/rtc/rtc",
    "gui/taskbar",
]

if b"auth_policy" not in data and marker in data:
    lines = []
    for obj in new_objs:
        lines.append(f"\t$(BUILD)/x86_64/{obj}.o".encode())
    replacement = marker + b" \\\n" + b" \\\n".join(lines)
    data = data.replace(marker, replacement, 1)
    with open(path, "wb") as f:
        f.write(data)
    print(f"OK - added {len(new_objs)} objects")
else:
    print("Already patched or marker missing")
