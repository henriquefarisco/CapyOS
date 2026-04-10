#!/usr/bin/env python3
"""Add smp.o and ap_boot.o to CAPYOS64_OBJS in Makefile."""

path = "/Volumes/CapyOS/Makefile"
with open(path, "rb") as f:
    data = f.read()

marker = b"$(BUILD)/x86_64/core/boot_metrics.o"
new_objs = (
    marker +
    b" \\\n\t$(BUILD)/x86_64/arch/x86_64/smp.o"
)

if b"smp.o" not in data and marker in data:
    data = data.replace(marker, new_objs, 1)
    with open(path, "wb") as f:
        f.write(data)
    print("OK")
else:
    print("Already patched or marker missing")
