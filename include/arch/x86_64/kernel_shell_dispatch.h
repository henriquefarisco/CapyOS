#ifndef ARCH_X86_64_KERNEL_SHELL_DISPATCH_H
#define ARCH_X86_64_KERNEL_SHELL_DISPATCH_H

#include "shell/core.h"

int x64_kernel_try_shell_command(struct shell_context *ctx,
                                 int shell_initialized, char *line);
int x64_kernel_run_shell_alias(struct shell_context *ctx,
                               int shell_initialized,
                               const char *alias_line);

#endif /* ARCH_X86_64_KERNEL_SHELL_DISPATCH_H */
