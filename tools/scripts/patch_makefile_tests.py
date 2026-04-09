#!/usr/bin/env python3
"""Add new test files and their source dependencies to TEST_SRCS in Makefile."""

path = "/Volumes/CapyOS/Makefile"
with open(path, "rb") as f:
    data = f.read()

# Find the end of TEST_SRCS (the line with the last .c file before the next target)
# TEST_SRCS ends with a line that doesn't have a backslash continuation
# We need to add our new test files and their source deps

new_test_srcs = (
    b"               tests/test_pmm.c src/memory/pmm.c \\\r\n"
    b"               tests/test_task.c src/kernel/task.c \\\r\n"
    b"               tests/test_dns_cache.c src/net/dns_cache.c \\\r\n"
    b"               tests/test_boot_slot.c src/core/boot_slot.c"
)

# Find the marker: the last line of TEST_SRCS (localization.c ... update_agent.c)
# It ends without backslash
marker = b"src/core/localization.c src/core/klog.c src/core/service_manager.c src/core/work_queue.c src/core/update_agent.c"

if marker in data and b"test_pmm.c" not in data:
    data = data.replace(
        marker,
        marker + b" \\\r\n" + new_test_srcs,
        1
    )
    with open(path, "wb") as f:
        f.write(data)
    print("Makefile TEST_SRCS patched OK")
else:
    if b"test_pmm.c" in data:
        print("Already patched")
    else:
        print("WARN: marker not found")
