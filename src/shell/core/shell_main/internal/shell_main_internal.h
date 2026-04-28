#ifndef SHELL_MAIN_INTERNAL_H
#define SHELL_MAIN_INTERNAL_H

#include "shell/shell.h"
#include "shell/core.h"
#include "shell/commands.h"
#include "shell/commands_extended.h"

#include "lang/localization.h"
#include "kernel/log/klog.h"
#include "memory/kmem.h"
#include "drivers/console/tty.h"
#include "drivers/input/keyboard.h"
#include "auth/user.h"
#include "fs/vfs.h"
#include "drivers/video/vga.h"

#include <stddef.h>
#include <stdint.h>

extern shell_output_write_fn g_shell_output_write;
extern shell_output_putc_fn g_shell_output_putc;

void shell_hotkey_help_docs(void);
void shell_append_text(char *dst, size_t dst_size, const char *src);
const struct shell_command *shell_find_command(const char *name);
int shell_self_test(struct shell_context *ctx);
int shell_run_diagnostics(struct shell_context *ctx);
void cli_log_dependency_wait(const char *dependency, const char *target);

#endif
