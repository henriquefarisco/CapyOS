#ifndef SHELL_CORE_H
#define SHELL_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "core/session.h"
#include "core/system_init.h"
#include "fs/vfs.h"

#define SHELL_MAX_ARGS      16
#define SHELL_PATH_BUFFER   128
#define SHELL_READ_CHUNK    128
#define SHELL_HUNT_MAX_DEPTH 16

struct shell_context {
    struct session_context *session;
    const struct system_settings *settings;
    int running;
    int logout;
};

typedef int (*shell_command_handler)(struct shell_context *ctx, int argc, char **argv);

struct shell_command {
    const char *name;
    shell_command_handler handler;
};

struct shell_command_set {
    const struct shell_command *commands;
    size_t count;
};

void shell_context_init(struct shell_context *ctx,
                        struct session_context *session,
                        const struct system_settings *settings);
struct session_context *shell_context_session(struct shell_context *ctx);
const struct system_settings *shell_context_settings(const struct shell_context *ctx);
int shell_context_running(const struct shell_context *ctx);
int shell_context_should_logout(const struct shell_context *ctx);
void shell_request_exit(struct shell_context *ctx);
void shell_request_logout(struct shell_context *ctx);
const char *shell_current_language(void);

void shell_update_prompt(struct shell_context *ctx);
int shell_parse_line(char *line, char **argv, int max_args);

/* Output helpers */
void shell_print(const char *text);
void shell_newline(void);
void shell_print_ok(const char *msg);
void shell_print_error(const char *msg);
void shell_suggest_help(const char *cmd);
void shell_paginate_content(const char *content);
void shell_print_number(uint32_t value);

/* String helpers */
int shell_string_equal(const char *a, const char *b);
size_t shell_cstring_length(const char *s);
void shell_copy(char *dst, size_t dst_len, const char *src);
int shell_string_contains(const char *haystack, const char *needle);

/* Path helpers */
void shell_trim_trailing_slash(char *path);
int shell_resolve_path(struct shell_context *ctx, const char *input, char *out, size_t out_len);
int shell_path_is_dir(const char *path);
int shell_path_is_file(const char *path);
int shell_join_path(const char *dir, const char *name, char *out, size_t out_len);
const char *shell_basename(const char *path);
void shell_format_prompt_path(const struct user_record *user, const char *cwd,
                              char *out, size_t out_len);
void shell_build_prompt(const struct user_record *user,
                        const struct system_settings *settings,
                        const char *cwd, char *out, size_t out_len);
void shell_format_perm(uint16_t perm, char out[5]);
void shell_fill_metadata(struct shell_context *ctx, uint16_t mode, struct vfs_metadata *meta);

/* File helpers */
struct file *shell_open_file_read(const char *path);
struct file *shell_open_file_write(const char *path);
int shell_stream_file(struct file *file);
int shell_copy_stream(struct file *src, struct file *dst);
int shell_read_file(const char *path, char **out_buf, size_t *out_len);

/* Help helpers */
int shell_help_requested(int argc, char **argv);
int shell_handle_help(int argc, char **argv, const char *usage, const char *details);

const struct shell_command_set *shell_command_sets(size_t *count);

#endif
