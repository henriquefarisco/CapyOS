#include "shell/commands.h"
#include "shell/core.h"

#include "memory/kmem.h"

static void buffer_append_local(char *dst, size_t dst_size, const char *src)
{
    if (!dst || !src || dst_size == 0) {
        return;
    }
    size_t len = shell_cstring_length(dst);
    size_t i = 0;
    while (src[i] && len + 1 < dst_size) {
        dst[len++] = src[i++];
    }
    dst[len] = '\0';
}

static int cmd_help_any(struct shell_context *ctx, int argc, char **argv)
{
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: help-any\nLista todos os comandos registrados no CapyCLI.\n");
        return 0;
    }
    (void)ctx;

    size_t set_count = 0;
    const struct shell_command_set *sets = shell_command_sets(&set_count);
    if (set_count == 0) {
        shell_print("Nenhum comando registrado.\n");
        return 0;
    }

    size_t total_len = shell_cstring_length("Comandos disponiveis:\n") +
                      shell_cstring_length("Use help-docs para resumo rapido.\n") + 1;
    size_t cmd_total = 0;
    for (size_t i = 0; i < set_count; ++i) {
        cmd_total += sets[i].count;
        for (size_t j = 0; j < sets[i].count; ++j) {
            total_len += 4 + shell_cstring_length(sets[i].commands[j].name);
        }
    }

    char *buffer = (char *)kalloc(total_len);
    if (!buffer) {
        shell_print_error("memoria insuficiente");
        return -1;
    }
    buffer[0] = '\0';
    buffer_append_local(buffer, total_len, "Comandos disponiveis:\n");
    for (size_t i = 0; i < set_count; ++i) {
        for (size_t j = 0; j < sets[i].count; ++j) {
            buffer_append_local(buffer, total_len, " - ");
            buffer_append_local(buffer, total_len, sets[i].commands[j].name);
            buffer_append_local(buffer, total_len, "\n");
        }
    }
    buffer_append_local(buffer, total_len, "Use help-docs para resumo rapido.\n");

    shell_paginate_content(buffer);
    kfree(buffer);
    (void)cmd_total;
    return 0;
}

static int cmd_help_docs(struct shell_context *ctx, int argc, char **argv)
{
    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: help-docs\nExibe a referencia de comandos documentada em /docs/noiros-cli-reference.txt.\n");
        return 0;
    }
    (void)ctx;
    (void)argv;

    char *buffer = NULL;
    size_t len = 0;
    if (shell_read_file("/docs/noiros-cli-reference.txt", &buffer, &len) != 0) {
        shell_print("Documentacao nao encontrada em /docs/noiros-cli-reference.txt\n");
        shell_print("Verifique instalacao ou use help-any.\n");
        return 0;
    }

    shell_paginate_content(buffer);
    kfree(buffer);
    return 0;
}

static struct shell_command g_help_commands[2];
static int g_help_commands_initialized = 0;

static void init_help_commands(void) {
    if (g_help_commands_initialized) {
        return;
    }
    g_help_commands[0].name = "help-any";
    g_help_commands[0].handler = cmd_help_any;
    g_help_commands[1].name = "help-docs";
    g_help_commands[1].handler = cmd_help_docs;
    g_help_commands_initialized = 1;
}

const struct shell_command *shell_commands_help(size_t *count)
{
    init_help_commands();
    if (count) {
        *count = 2;
    }
    return g_help_commands;
}
