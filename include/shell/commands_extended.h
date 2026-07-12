#ifndef SHELL_COMMANDS_EXTENDED_H
#define SHELL_COMMANDS_EXTENDED_H

#include <stddef.h>
#include "shell/core.h"

const struct shell_command *shell_commands_extended(size_t *count);
const struct shell_command *shell_commands_extended_early(size_t *count);

/* Fill `slot` with the "capyai" command (defined in capyai_command.c so its
 * handler + built-in model stay isolated from the extended registry). */
int capyai_command_register(struct shell_command *slot);

/* Boot self-test used by the `smoke-x64-capyai` target: runs the capyai
 * pipeline over fixed intents and emits debugcon markers. Only defined when
 * the CapyAI sibling core was compiled in (CAPYOS_HAVE_CAPYAI). */
void capyai_selftest_run(void);

#endif
