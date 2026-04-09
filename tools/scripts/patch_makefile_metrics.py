#!/usr/bin/env python3
"""Add boot_metrics.o to CAPYOS64_OBJS in Makefile."""

path = "/Volumes/CapyOS/Makefile"
with open(path, "rb") as f:
    data = f.read()

marker = b"$(BUILD)/x86_64/fs/capyfs/capyfs_journal_integration.o"
new_obj = marker + b" \\\n\t$(BUILD)/x86_64/core/boot_metrics.o"

if b"boot_metrics" not in data and marker in data:
    data = data.replace(marker, new_obj, 1)
    with open(path, "wb") as f:
        f.write(data)
    print("OK")
else:
    print("Already patched or marker missing")
