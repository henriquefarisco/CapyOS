#!/usr/bin/env python3
"""Add all remaining Phase objects to CAPYOS64_OBJS in Makefile."""

path = "/Volumes/CapyOS/Makefile"
with open(path, "rb") as f:
    data = f.read()

marker = b"$(BUILD)/x86_64/gui/taskbar.o"

new_objs = [
    "security/sha512",
    "gui/desktop",
    "apps/calculator",
    "apps/file_manager",
    "apps/text_editor",
    "apps/task_manager",
    "apps/html_viewer",
    "apps/settings",
]

if b"sha512" not in data and marker in data:
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
