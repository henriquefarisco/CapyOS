#include "arch/x86_64/kernel_shell_dispatch.h"

#include <stddef.h>

#include "shell/commands.h"

int x64_kernel_try_shell_command(struct shell_context *ctx,
                                 int shell_initialized, char *line) {
  if (!shell_initialized || !ctx || !line || !line[0]) {
    return 0;
  }

  char *argv[16];
  int argc = shell_parse_line(line, argv, 16);
  if (argc == 0) {
    return 0;
  }

  size_t set_count = 0;
  const struct shell_command_set *sets = shell_command_sets(&set_count);
  for (size_t i = 0; i < set_count; ++i) {
    for (size_t j = 0; j < sets[i].count; ++j) {
      if (shell_string_equal(argv[0], sets[i].commands[j].name)) {
        (void)sets[i].commands[j].handler(ctx, argc, argv);
        return 1;
      }
    }
  }

  return 0;
}

int x64_kernel_run_shell_alias(struct shell_context *ctx,
                               int shell_initialized,
                               const char *alias_line) {
  char tmp[64];
  size_t i = 0;

  if (!alias_line) {
    return 0;
  }
  while (alias_line[i] && i + 1 < sizeof(tmp)) {
    tmp[i] = alias_line[i];
    ++i;
  }
  tmp[i] = '\0';
  return x64_kernel_try_shell_command(ctx, shell_initialized, tmp);
}
