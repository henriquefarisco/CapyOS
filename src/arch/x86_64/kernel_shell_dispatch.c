#include "arch/x86_64/framebuffer_console.h"
#include "arch/x86_64/kernel_shell_dispatch.h"

#include <stddef.h>

#include "shell/commands.h"
#include "shell/commands_extended.h"


static int command_name_matches(const char *input, const char *registered) {
  if (!input || !registered) {
    return 0;
  }
  if (shell_string_equal(input, registered)) {
    return 1;
  }

  while (*input || *registered) {
    while (*input == '-' || *input == '_') {
      ++input;
    }
    while (*registered == '-' || *registered == '_') {
      ++registered;
    }

    if (*input == '\0' || *registered == '\0') {
      break;
    }
    if (*input != *registered) {
      return 0;
    }
    ++input;
    ++registered;
  }

  while (*input == '-' || *input == '_') {
    ++input;
  }
  while (*registered == '-' || *registered == '_') {
    ++registered;
  }

  return *input == '\0' && *registered == '\0';
}

int x64_kernel_try_shell_command_result(struct shell_context *ctx,
                                        int shell_initialized, char *line,
                                        int *out_rc) {
  if (!ctx || !line || !line[0]) {
    return 0;
  }

  char *argv[16];
  int argc = shell_parse_line(line, argv, 16);
  if (argc == 0) {
    return 0;
  }

  /* 1. Early commands: always available, even before shell bootstrap */
  {
    size_t early_count = 0;
    const struct shell_command *early = shell_commands_extended_early(&early_count);
    for (size_t i = 0; i < early_count; ++i) {
      if (command_name_matches(argv[0], early[i].name)) {
        int rc = early[i].handler(ctx, argc, argv);
        if (out_rc) *out_rc = rc;
        return 1;
      }
    }
  }

  /* 2. If shell not initialized, check if command is known but unavailable */
  if (!shell_initialized) {
    size_t ext_count = 0;
    const struct shell_command *ext = shell_commands_extended(&ext_count);
    for (size_t i = 0; i < ext_count; ++i) {
      if (command_name_matches(argv[0], ext[i].name)) {
        fbcon_print("Command unavailable until shell runtime is initialized.\n");
        if (out_rc) *out_rc = -1;
        return 1;
      }
    }
    size_t set_count = 0;
    const struct shell_command_set *sets = shell_command_sets(&set_count);
    for (size_t i = 0; i < set_count; ++i) {
      for (size_t j = 0; j < sets[i].count; ++j) {
        if (command_name_matches(argv[0], sets[i].commands[j].name)) {
          fbcon_print("Command unavailable until shell runtime is initialized.\n");
          if (out_rc) *out_rc = -1;
          return 1;
        }
      }
    }
    return 0;
  }

  /* 3. Normal dispatch: standard command sets */
  {
    size_t set_count = 0;
    const struct shell_command_set *sets = shell_command_sets(&set_count);
    for (size_t i = 0; i < set_count; ++i) {
      for (size_t j = 0; j < sets[i].count; ++j) {
        if (command_name_matches(argv[0], sets[i].commands[j].name)) {
          int rc = sets[i].commands[j].handler(ctx, argc, argv);
          if (out_rc) *out_rc = rc;
          return 1;
        }
      }
    }
  }

  /* 4. Extended commands (full set) */
  {
    size_t ext_count = 0;
    const struct shell_command *ext = shell_commands_extended(&ext_count);
    for (size_t i = 0; i < ext_count; ++i) {
      if (command_name_matches(argv[0], ext[i].name)) {
        int rc = ext[i].handler(ctx, argc, argv);
        if (out_rc) *out_rc = rc;
        return 1;
      }
    }
  }

  return 0;
}

int x64_kernel_try_shell_command(struct shell_context *ctx,
                                 int shell_initialized, char *line) {
  return x64_kernel_try_shell_command_result(ctx, shell_initialized, line,
                                             (int *)0);
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
