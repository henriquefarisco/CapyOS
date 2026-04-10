#!/usr/bin/env python3
"""Register extended commands in shell_main.c."""

path = "/Volumes/CapyOS/src/shell/core/shell_main.c"
with open(path, "rb") as f:
    data = f.read()

if b"shell_commands_extended" in data:
    print("Already patched")
    exit(0)

# 1. Add include for extended commands header
for nl in [b"\r\n", b"\n"]:
    marker = b'#include "shell/commands.h"' + nl
    if marker in data:
        data = data.replace(marker, marker + b'#include "shell/commands_extended.h"' + nl, 1)
        break

# 2. Expand g_command_sets array from 10 to 12
data = data.replace(b"g_command_sets[10]", b"g_command_sets[12]", 1)

# 3. Add extended command set registration before #undef
for nl in [b"\r\n", b"\n"]:
    marker = b"    ADD_COMMAND_SET(shell_commands_user_manage);" + nl + nl + b"#undef ADD_COMMAND_SET"
    if marker in data:
        new_block = (
            b"    ADD_COMMAND_SET(shell_commands_user_manage);" + nl +
            b"    ADD_COMMAND_SET(shell_commands_extended);" + nl +
            nl + b"#undef ADD_COMMAND_SET"
        )
        data = data.replace(marker, new_block, 1)
        break

with open(path, "wb") as f:
    f.write(data)
print("shell_main.c patched OK")
