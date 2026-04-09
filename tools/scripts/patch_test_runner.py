#!/usr/bin/env python3
"""Add new test declarations and calls to test_runner.c."""

path = "/Volumes/CapyOS/tests/test_runner.c"
with open(path, "rb") as f:
    data = f.read()

# Detect line ending
nl = b"\r\n" if b"\r\n" in data else b"\n"

# Add new test declarations before main()
new_decls = (
    b"int test_pmm_run(void);" + nl +
    b"int test_task_run(void);" + nl +
    b"int test_dns_cache_run(void);" + nl +
    b"int test_boot_slot_run(void);" + nl
)

marker_main = b"int main(void) {"
if b"test_pmm_run" not in data and marker_main in data:
    data = data.replace(marker_main, new_decls + nl + marker_main, 1)

# Add new test calls before the if (failures == 0) check
new_calls = (
    b"    failures += test_pmm_run();" + nl +
    b"    failures += test_task_run();" + nl +
    b"    failures += test_dns_cache_run();" + nl +
    b"    failures += test_boot_slot_run();" + nl
)

marker_check = b"    if (failures == 0) {"
if b"test_pmm_run" not in data.split(marker_main, 1)[-1] and marker_check in data:
    data = data.replace(marker_check, new_calls + nl + marker_check, 1)

with open(path, "wb") as f:
    f.write(data)
print("test_runner.c patched OK")
