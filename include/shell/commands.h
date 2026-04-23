#ifndef SHELL_COMMANDS_H
#define SHELL_COMMANDS_H

#include <stddef.h>

#include "shell/core.h"

const struct shell_command *shell_commands_filesystem_navigation(size_t *count);
const struct shell_command *shell_commands_filesystem_content(size_t *count);
const struct shell_command *shell_commands_filesystem_manage(size_t *count);
const struct shell_command *shell_commands_filesystem_search(size_t *count);

const struct shell_command *shell_commands_help(size_t *count);
const struct shell_command *shell_commands_session(size_t *count);
const struct shell_command *shell_commands_system_info(size_t *count);
const struct shell_command *shell_commands_system_control(size_t *count);
const struct shell_command *shell_commands_network(size_t *count);
const struct shell_command *shell_commands_user_manage(size_t *count);

#endif
