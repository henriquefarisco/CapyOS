#!/usr/bin/env python3
"""Patches Makefile to include new subsystem object files."""
import sys

NEW_OBJS = [
    "arch/x86_64/panic", "arch/x86_64/apic", "arch/x86_64/context_switch",
    "arch/x86_64/syscall_entry", "kernel/task", "kernel/scheduler",
    "kernel/spinlock", "kernel/worker", "kernel/syscall", "kernel/elf_loader",
    "kernel/process", "memory/pmm", "memory/vmm", "fs/journal/journal",
    "fs/fsck/fsck", "net/socket", "net/tcp", "net/dns_cache", "net/http",
    "security/ed25519", "core/boot_slot", "core/package_manager",
    "drivers/input/mouse", "gui/event", "gui/font", "gui/compositor",
    "gui/widget", "gui/terminal", "lang/capylang",
]

mf = "/Volumes/CapyOS/Makefile"
with open(mf, "rb") as f:
    data = f.read()

anchor = b"$(BUILD)/x86_64/shell/commands/filesystem_search.o\r\n"
lines = []
for obj in NEW_OBJS:
    lines.append(f"\t$(BUILD)/x86_64/{obj}.o".encode())
replacement = b"$(BUILD)/x86_64/shell/commands/filesystem_search.o \\\r\n"
replacement += b" \\\r\n".join(lines) + b"\r\n"
data = data.replace(anchor, replacement, 1)

with open(mf, "wb") as f:
    f.write(data)
print("Makefile patched OK")
