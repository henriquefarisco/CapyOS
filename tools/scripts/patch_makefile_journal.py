#!/usr/bin/env python3
"""Add capyfs_journal_integration.o to CAPYOS64_OBJS in Makefile."""

path = "/Volumes/CapyOS/Makefile"
with open(path, "rb") as f:
    data = f.read()

marker = b"$(BUILD)/x86_64/lang/capylang.o"
new_obj = b"$(BUILD)/x86_64/lang/capylang.o \\\n\t$(BUILD)/x86_64/fs/capyfs/capyfs_journal_integration.o"

if b"capyfs_journal_integration" not in data and marker in data:
    data = data.replace(marker, new_obj, 1)
    with open(path, "wb") as f:
        f.write(data)
    print("OK")
else:
    print("Already patched or marker missing")
