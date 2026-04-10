#!/usr/bin/env python3
"""Add task/scheduler/socket/dns init to kernel_main.c Stage 2."""

path = "/Volumes/CapyOS/src/arch/x86_64/kernel_main.c"
with open(path, "rb") as f:
    data = f.read()

# Try both LF and CRLF variants
for nl in [b"\r\n", b"\n"]:
    marker = b"  work_queue_init();" + nl + b"  update_agent_init"
    if marker in data:
        replacement = (
            b"  work_queue_init();" + nl +
            b"  task_system_init();" + nl +
            b"  scheduler_init(SCHED_POLICY_COOPERATIVE);" + nl +
            b"  dns_cache_init();" + nl +
            b"  socket_system_init();" + nl +
            b"  update_agent_init"
        )
        data = data.replace(marker, replacement, 1)
        with open(path, "wb") as f:
            f.write(data)
        print(f"OK - used {'CRLF' if nl == b'\\r\\n' else 'LF'} variant")
        exit(0)

print("WARN: marker not found")
