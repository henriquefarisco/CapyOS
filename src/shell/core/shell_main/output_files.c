#include "internal/shell_main_internal.h"

void shell_print(const char *text)
{
    if (text) {
        if (g_shell_output_write) {
            g_shell_output_write(text);
        } else {
            vga_write(text);
        }
    }
}

void shell_newline(void)
{
    if (g_shell_output_putc) {
        g_shell_output_putc('\n');
    } else {
        vga_newline();
    }
}

void shell_print_error(const char *msg)
{
    shell_print("[erro] ");
    if (msg) shell_print(msg);
    shell_newline();
}

void shell_print_ok(const char *msg)
{
    shell_print("[ok] ");
    if (msg) shell_print(msg);
    shell_newline();
}

void shell_print_number(uint32_t value)
{
    char buffer[12];
    size_t len = 0;
    if (value == 0) {
        buffer[len++] = '0';
    } else {
        char tmp[10];
        size_t idx = 0;
        while (value && idx < sizeof(tmp)) {
            tmp[idx++] = (char)('0' + (value % 10));
            value /= 10;
        }
        while (idx) {
            buffer[len++] = tmp[--idx];
        }
    }
    buffer[len] = '\0';
    shell_print(buffer);
}

void shell_paginate_content(const char *content)
{
    if (!content) {
        return;
    }
    const int lines_per_page = 20;
    int line_count = 0;
    const char *p = content;

    tty_set_echo(0);

    while (*p) {
        while (*p && *p != '\n') {
            vga_putc(*p++);
        }
        vga_newline();
        if (*p == '\n') {
            ++p;
        }
        if (++line_count >= lines_per_page && *p) {
            shell_print(localization_select(shell_current_language(), "-- mais --",
                                            "-- more --", "-- mas --"));
            char c = tty_getc();
            vga_backspace_multiple(10);
            if (c == 'q' || c == 'Q') {
                break;
            }
            line_count = 0;
        }
    }

    tty_set_echo(1);
}

void shell_update_prompt(struct shell_context *ctx)
{
    const struct user_record *user = session_user(ctx->session);
    char prompt[TTY_PROMPT_MAX];
    const char *cwd = ctx && ctx->session ? session_cwd(ctx->session) : "/";
    shell_build_prompt(user, ctx ? ctx->settings : NULL, cwd, prompt, sizeof(prompt));
    tty_set_prompt(prompt);
}

int shell_parse_line(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        if (argc >= max_args) {
            break;
        }

        char quote = 0;
        if (*p == '"' || *p == '\'') {
            quote = *p;
            ++p;
        }
        char *start = p;
        char *dest = p;
        while (*p) {
            char c = *p;
            if (quote) {
                if (c == '\\' && quote == '"' && p[1] != '\0') {
                    ++p;
                    c = *p;
                } else if (c == quote) {
                    ++p;
                    break;
                }
            } else if (c == ' ' || c == '\t') {
                ++p;
                break;
            }
            *dest++ = c;
            ++p;
        }
        *dest = '\0';
        argv[argc++] = start;
    }
    return argc;
}

struct file *shell_open_file_read(const char *path)
{
    return vfs_open(path, 0);
}

struct file *shell_open_file_write(const char *path)
{
    return vfs_open(path, VFS_OPEN_WRITE);
}

int shell_stream_file(struct file *file)
{
    if (!file) {
        return -1;
    }
    char buffer[SHELL_READ_CHUNK + 1];
    long read;
    while ((read = vfs_read(file, buffer, SHELL_READ_CHUNK)) > 0) {
        buffer[read] = '\0';
        shell_print(buffer);
    }
    shell_newline();
    return 0;
}

void shell_set_output_callbacks(shell_output_write_fn write_cb,
                                shell_output_putc_fn putc_cb)
{
    g_shell_output_write = write_cb;
    g_shell_output_putc = putc_cb;
}

void shell_set_clear_callback(shell_output_clear_fn clear_cb)
{
    g_shell_output_clear = clear_cb;
}

void shell_clear_screen(void)
{
    if (g_shell_output_clear) {
        g_shell_output_clear();
        return;
    }
    /* Post-M5 W1: no widget-aware sink installed (CLI or early
     * boot path); fall back to framebuffer console clear. */
    vga_clear();
}

int shell_copy_stream(struct file *src, struct file *dst)
{
    if (!src || !dst) {
        return -1;
    }
    char buffer[SHELL_READ_CHUNK];
    long read_bytes;
    while ((read_bytes = vfs_read(src, buffer, sizeof(buffer))) > 0) {
        const char *ptr = buffer;
        long remaining = read_bytes;
        while (remaining > 0) {
            long written = vfs_write(dst, ptr, (size_t)remaining);
            if (written <= 0) {
                return -1;
            }
            remaining -= written;
            ptr += written;
        }
    }
    return 0;
}

int shell_read_file(const char *path, char **out_buf, size_t *out_len)
{
    struct file *f = vfs_open(path, 0);
    if (!f) {
        return -1;
    }
    size_t size = 0;
    if (f->dentry && f->dentry->inode) {
        size = f->dentry->inode->size;
    }
    char *buffer = (char *)kalloc(size + 1);
    if (!buffer) {
        vfs_close(f);
        return -1;
    }
    long read = vfs_read(f, buffer, size);
    vfs_close(f);
    if (read < 0) {
        kfree(buffer);
        return -1;
    }
    buffer[read] = '\0';
    if (out_buf) {
        *out_buf = buffer;
    }
    if (out_len) {
        *out_len = (size_t)read;
    }
    return 0;
}

