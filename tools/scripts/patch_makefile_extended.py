#!/usr/bin/env python3
"""Add extended CLI commands and remaining objects to Makefile."""

path = "/Volumes/CapyOS/Makefile"
with open(path, "rb") as f:
    data = f.read()

marker = b"$(BUILD)/x86_64/apps/settings.o"

new_objs = [
    "shell/commands/extended",
]

if b"shell/commands/extended.o" not in data and marker in data:
    lines = []
    for obj in new_objs:
        lines.append(f"\t$(BUILD)/x86_64/{obj}.o".encode())
    replacement = marker + b" \\\n" + b" \\\n".join(lines)
    data = data.replace(marker, replacement, 1)
    with open(path, "wb") as f:
        f.write(data)
    print("OK")
else:
    print("Already patched or marker missing")
