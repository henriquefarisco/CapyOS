#ifndef SHELL_COMMANDS_EXTENDED_H
#define SHELL_COMMANDS_EXTENDED_H

#include <stddef.h>
#include "shell/core.h"

const struct shell_command *shell_commands_extended(size_t *count);
const struct shell_command *shell_commands_extended_early(size_t *count);

#endif
