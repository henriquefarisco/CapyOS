/* Host regression tests for the CapyAI typed native-file adapter.
 *
 * This test deliberately supplies a tiny in-memory VFS and shell boundary so
 * capyai_native_files.c can be compiled and exercised without the kernel.  It
 * covers nested file creation, verified append, atomic VFS rename semantics,
 * fail-closed path/extension validation and injected I/O failures.
 *
 * Standalone build from the CapyOS repository root:
 *   cc -std=c11 -Wall -Wextra -Werror -Iinclude \
 *      -I../CapyAI/src/core \
 *      tests/services/test_capyai_native_files.c \
 *      src/services/capyai/capyai_native_files.c \
 *      -o build/test_capyai_native_files
 */
#include <stdio.h>
#include <string.h>

#include "fs/vfs.h"
#include "services/capyai.h"
#include "shell/core.h"

#define MEM_NODE_COUNT 24u
#define MEM_HANDLE_COUNT 8u
#define MEM_DATA_CAPACITY 4096u

struct mem_node {
    int used;
    char path[SHELL_PATH_BUFFER];
    uint16_t mode;
    struct vfs_metadata metadata;
    unsigned char data[MEM_DATA_CAPACITY];
    size_t size;
};

struct mem_handle {
    int used;
    struct file file;
    struct mem_node *node;
};

static struct mem_node nodes[MEM_NODE_COUNT];
static struct mem_handle handles[MEM_HANDLE_COUNT];
static int failures;
static int fail_create;
static int fail_open_write;
static int fail_next_stat;
static int fail_rename;
static int fail_rename_call;
static int rename_calls;
static int fail_write_call;
static int write_calls;
static size_t max_write_chunk;
static int last_vfs_error;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        if (condition) printf("  ok   %s\n", label);                          \
        else {                                                                 \
            printf("  FAIL %s\n", label);                                    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static size_t bounded_length(const char *value, size_t limit) {
    size_t size = 0u;
    if (!value) return limit;
    while (size < limit && value[size]) ++size;
    return size;
}

static int copy_string(char *out, size_t out_size, const char *value) {
    size_t size = bounded_length(value, out_size);
    if (!out || out_size == 0u || size >= out_size) return -1;
    memcpy(out, value, size + 1u);
    return 0;
}

static struct mem_node *find_node(const char *path) {
    size_t i;
    for (i = 0u; i < MEM_NODE_COUNT; ++i) {
        if (nodes[i].used && strcmp(nodes[i].path, path) == 0) return &nodes[i];
    }
    return NULL;
}

static struct mem_node *add_node(const char *path, uint16_t mode,
                                 const char *content) {
    size_t i;
    size_t content_size = content ? strlen(content) : 0u;
    if (find_node(path) || content_size > MEM_DATA_CAPACITY) return NULL;
    for (i = 0u; i < MEM_NODE_COUNT; ++i) {
        struct mem_node *node = &nodes[i];
        if (node->used) continue;
        memset(node, 0, sizeof(*node));
        if (copy_string(node->path, sizeof(node->path), path) != 0) return NULL;
        node->used = 1;
        node->mode = mode;
        node->metadata.uid = 1000u;
        node->metadata.gid = 1000u;
        node->metadata.perm = mode == VFS_MODE_DIR ? 0755u : 0644u;
        if (content_size > 0u) memcpy(node->data, content, content_size);
        node->size = content_size;
        return node;
    }
    return NULL;
}

static void reset_vfs(void) {
    memset(nodes, 0, sizeof(nodes));
    memset(handles, 0, sizeof(handles));
    fail_create = 0;
    fail_open_write = 0;
    fail_next_stat = 0;
    fail_rename = 0;
    fail_rename_call = 0;
    rename_calls = 0;
    fail_write_call = 0;
    write_calls = 0;
    max_write_chunk = 0u;
    last_vfs_error = VFS_OK;
    (void)add_node("/", VFS_MODE_DIR, NULL);
    (void)add_node("/workspace", VFS_MODE_DIR, NULL);
    (void)add_node("/workspace-other", VFS_MODE_DIR, NULL);
    (void)add_node("/system", VFS_MODE_DIR, NULL);
    (void)add_node("/workspace/docs", VFS_MODE_DIR, NULL);
    (void)add_node("/workspace/archive", VFS_MODE_DIR, NULL);
}

static int parent_is_directory(const char *path) {
    char parent[SHELL_PATH_BUFFER];
    char *slash;
    struct mem_node *node;
    if (copy_string(parent, sizeof(parent), path) != 0) return 0;
    slash = strrchr(parent, '/');
    if (!slash) return 0;
    if (slash == parent) slash[1] = '\0';
    else *slash = '\0';
    node = find_node(parent);
    return node && (node->mode & VFS_MODE_DIR) != 0u;
}

static struct mem_handle *find_handle(struct file *file) {
    size_t i;
    for (i = 0u; i < MEM_HANDLE_COUNT; ++i) {
        if (handles[i].used && &handles[i].file == file) return &handles[i];
    }
    return NULL;
}

static const char *node_text(const char *path) {
    static char text[MEM_DATA_CAPACITY + 1u];
    struct mem_node *node = find_node(path);
    size_t size;
    if (!node || (node->mode & VFS_MODE_FILE) == 0u) return NULL;
    size = node->size;
    if (size > MEM_DATA_CAPACITY) return NULL;
    memcpy(text, node->data, size);
    text[size] = '\0';
    return text;
}

/* ---- Minimal typed-schema boundary used by capyai_native_file_dispatch. ---- */

static int safe_path(const char *path) {
    size_t i;
    size_t segment = 0u;
    if (!path || path[0] == '\0' || path[0] == '-') return 0;
    for (i = 0u; i < CAPY_AI_STR_MAX; ++i) {
        unsigned char c = (unsigned char)path[i];
        if (c == 0u) {
            return !(i - segment == 2u && path[segment] == '.' &&
                     path[segment + 1u] == '.');
        }
        if (c == '/') {
            if (i - segment == 2u && path[segment] == '.' &&
                path[segment + 1u] == '.') return 0;
            segment = i + 1u;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
              c == ':')) return 0;
    }
    return 0;
}

static int safe_text(const char *text, int required) {
    size_t i;
    if (!text) return 0;
    for (i = 0u; i < CAPY_AI_STR_MAX; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == 0u) return required ? i > 0u : 1;
        if ((c < 0x20u && c != '\n' && c != '\r' && c != '\t') || c == 0x7fu)
            return 0;
    }
    return 0;
}

int capyai_typed_dispatch_valid(const struct capy_ai_output *out) {
    if (!out || out->risk != CAPY_AI_RISK_WRITE_FILE || !safe_path(out->path))
        return 0;
    if (strcmp(out->action, "file_create") == 0 &&
        strcmp(out->command, "touch") == 0)
        return safe_text(out->content, 0);
    if (strcmp(out->action, "file_edit_text") == 0 &&
        strcmp(out->command, "file-edit") == 0)
        return safe_text(out->content, 1);
    if (strcmp(out->action, "file_move") == 0 &&
        strcmp(out->command, "move") == 0)
        return safe_path(out->content);
    return 0;
}

/* ----------------------- In-memory VFS stubs. ----------------------------- */

int vfs_create(const char *path, uint16_t mode,
               const struct vfs_metadata *metadata) {
    struct mem_node *node;
    if (fail_create || !path || find_node(path) || !parent_is_directory(path)) {
        last_vfs_error = fail_create ? VFS_ERR_IO :
                         find_node(path) ? VFS_ERR_ALREADY_EXISTS :
                         VFS_ERR_NOT_DIRECTORY;
        return -1;
    }
    node = add_node(path, mode, NULL);
    if (!node) return -1;
    if (metadata) node->metadata = *metadata;
    last_vfs_error = VFS_OK;
    return 0;
}

struct file *vfs_open(const char *path, uint32_t flags) {
    struct mem_node *node = find_node(path);
    size_t i;
    if (!node || (node->mode & VFS_MODE_FILE) == 0u ||
        ((flags & VFS_OPEN_WRITE) != 0u && fail_open_write)) return NULL;
    for (i = 0u; i < MEM_HANDLE_COUNT; ++i) {
        struct mem_handle *handle = &handles[i];
        if (handle->used) continue;
        memset(handle, 0, sizeof(*handle));
        handle->used = 1;
        handle->node = node;
        handle->file.flags = flags;
        return &handle->file;
    }
    return NULL;
}

int vfs_close(struct file *file) {
    struct mem_handle *handle = find_handle(file);
    if (!handle) return -1;
    memset(handle, 0, sizeof(*handle));
    return 0;
}

long vfs_read(struct file *file, void *buffer, size_t size) {
    struct mem_handle *handle = find_handle(file);
    size_t available;
    if (!handle || !buffer) return -1;
    if ((size_t)file->position >= handle->node->size) return 0;
    available = handle->node->size - (size_t)file->position;
    if (size > available) size = available;
    memcpy(buffer, handle->node->data + file->position, size);
    file->position += (uint32_t)size;
    return (long)size;
}

long vfs_write(struct file *file, const void *buffer, size_t size) {
    struct mem_handle *handle = find_handle(file);
    size_t end;
    ++write_calls;
    if (!handle || !buffer || (file->flags & VFS_OPEN_WRITE) == 0u ||
        (fail_write_call > 0 && write_calls == fail_write_call)) return -1;
    if (max_write_chunk > 0u && size > max_write_chunk) size = max_write_chunk;
    end = (size_t)file->position + size;
    if (end > MEM_DATA_CAPACITY) return -1;
    memcpy(handle->node->data + file->position, buffer, size);
    file->position = (uint32_t)end;
    if (end > handle->node->size) handle->node->size = end;
    return (long)size;
}

int vfs_unlink(const char *path) {
    struct mem_node *node = find_node(path);
    if (!node || (node->mode & VFS_MODE_FILE) == 0u) return -1;
    memset(node, 0, sizeof(*node));
    return 0;
}

int vfs_rename(const char *source, const char *destination) {
    struct mem_node *node = find_node(source);
    rename_calls++;
    if (fail_rename || (fail_rename_call > 0 &&
                        rename_calls == fail_rename_call) ||
        !node || find_node(destination) ||
        !parent_is_directory(destination) ||
        copy_string(node->path, sizeof(node->path), destination) != 0) return -1;
    return 0;
}

int vfs_stat_path(const char *path, struct vfs_stat *out) {
    struct mem_node *node;
    if (fail_next_stat) {
        fail_next_stat = 0;
        last_vfs_error = VFS_ERR_IO;
        return -1;
    }
    node = find_node(path);
    if (!node || !out) {
        last_vfs_error = !node ? VFS_ERR_NOT_FOUND : VFS_ERR_INVALID_ARGUMENT;
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->mode = node->mode;
    out->size = (uint32_t)node->size;
    out->uid = node->metadata.uid;
    out->gid = node->metadata.gid;
    out->perm = node->metadata.perm;
    last_vfs_error = VFS_OK;
    return 0;
}

int vfs_last_error(void) { return last_vfs_error; }

const char *vfs_error_string(int error) {
    switch (error) {
    case VFS_ERR_NOT_FOUND: return "not found";
    case VFS_ERR_PERMISSION_DENIED: return "permission denied";
    case VFS_ERR_IO: return "io error";
    case VFS_ERR_ALREADY_EXISTS: return "already exists";
    default: return error == VFS_OK ? "ok" : "validation failed";
    }
}

/* ----------------------- Shell helper stubs. ------------------------------ */

size_t shell_cstring_length(const char *text) {
    return text ? strlen(text) : 0u;
}

int shell_resolve_path(struct shell_context *ctx, const char *input,
                       char *out, size_t out_size) {
    const char *cwd;
    size_t cwd_size;
    size_t input_size;
    if (!ctx || !ctx->session || !input || !input[0] || !out || out_size == 0u)
        return -1;
    input_size = bounded_length(input, CAPY_AI_STR_MAX);
    if (input_size >= CAPY_AI_STR_MAX) return -1;
    if (input[0] == '/') return copy_string(out, out_size, input);
    cwd = ctx->session->cwd[0] ? ctx->session->cwd : "/";
    cwd_size = strlen(cwd);
    if (cwd_size + (cwd_size > 1u ? 1u : 0u) + input_size >= out_size) return -1;
    memcpy(out, cwd, cwd_size);
    if (cwd_size > 1u) out[cwd_size++] = '/';
    memcpy(out + cwd_size, input, input_size + 1u);
    return 0;
}

void shell_trim_trailing_slash(char *path) {
    size_t size;
    if (!path) return;
    size = strlen(path);
    while (size > 1u && path[size - 1u] == '/') path[--size] = '\0';
}

int shell_path_is_dir(const char *path) {
    struct mem_node *node = find_node(path);
    return node && (node->mode & VFS_MODE_DIR) != 0u;
}

int shell_path_is_file(const char *path) {
    struct mem_node *node = find_node(path);
    return node && (node->mode & VFS_MODE_FILE) != 0u;
}

int shell_join_path(const char *directory, const char *name, char *out,
                    size_t out_size) {
    size_t directory_size;
    size_t name_size;
    int separator;
    if (!directory || !name || !out || out_size == 0u) return -1;
    directory_size = strlen(directory);
    name_size = strlen(name);
    separator = directory_size > 1u && directory[directory_size - 1u] != '/';
    if (directory_size + (size_t)separator + name_size >= out_size) return -1;
    memcpy(out, directory, directory_size);
    if (separator) out[directory_size++] = '/';
    memcpy(out + directory_size, name, name_size + 1u);
    return 0;
}

const char *shell_basename(const char *path) {
    const char *slash;
    if (!path) return "";
    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void shell_fill_metadata(struct shell_context *ctx, uint16_t mode,
                         struct vfs_metadata *metadata) {
    (void)ctx;
    if (!metadata) return;
    metadata->uid = 1000u;
    metadata->gid = 1000u;
    metadata->perm = mode == VFS_MODE_DIR ? 0755u : 0644u;
}

struct file *shell_open_file_read(const char *path) {
    return vfs_open(path, VFS_OPEN_READ);
}

struct file *shell_open_file_write(const char *path) {
    return vfs_open(path, VFS_OPEN_WRITE);
}

static struct capy_ai_output call_for(const char *action, const char *command,
                                      const char *path, const char *content) {
    struct capy_ai_output call;
    memset(&call, 0, sizeof(call));
    (void)copy_string(call.action, sizeof(call.action), action);
    (void)copy_string(call.command, sizeof(call.command), command);
    (void)copy_string(call.path, sizeof(call.path), path);
    (void)copy_string(call.content, sizeof(call.content), content);
    call.risk = CAPY_AI_RISK_WRITE_FILE;
    return call;
}

static int dispatch(struct shell_context *ctx, struct capy_ai_output *call,
                    char detail[CAPYAI_SUMMARY_MAX]) {
    memset(detail, 0, CAPYAI_SUMMARY_MAX);
    return capyai_native_file_dispatch(ctx, call, detail, CAPYAI_SUMMARY_MAX);
}

static void test_create_and_append(struct shell_context *ctx) {
    struct capy_ai_output call;
    char detail[CAPYAI_SUMMARY_MAX];
    const char *text;

    reset_vfs();
    max_write_chunk = 3u;
    call = call_for("file_create", "touch", "docs/notes.txt",
                    "primeira linha");
    CHECK(dispatch(ctx, &call, detail) == 0,
          "create writes content inside an existing folder");
    text = node_text("/workspace/docs/notes.txt");
    CHECK(text && strcmp(text, "primeira linha") == 0,
          "create retries partial VFS writes until content is complete");
    CHECK(strcmp(detail, "arquivo criado: /workspace/docs/notes.txt") == 0,
          "create reports the resolved destination");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              strcmp(node_text("/workspace/docs/notes.txt"),
                     "primeira linha") == 0,
          "duplicate create fails without overwriting content");

    max_write_chunk = 2u;
    call = call_for("file_edit_text", "file-edit", "docs/notes.txt",
                    "segunda linha");
    CHECK(dispatch(ctx, &call, detail) == 0,
          "append succeeds through bounded partial writes");
    text = node_text("/workspace/docs/notes.txt");
    CHECK(text && strcmp(text, "primeira linha\nsegunda linha\n") == 0,
          "append verifies separator, payload and trailing newline");
    CHECK(strcmp(detail, "arquivo atualizado: /workspace/docs/notes.txt") == 0,
          "append reports the resolved target");

    max_write_chunk = 0u;
    call = call_for("file_edit_text", "file-edit", "docs/notes.txt",
                    "terceira linha");
    CHECK(dispatch(ctx, &call, detail) == 0 &&
              strcmp(node_text("/workspace/docs/notes.txt"),
                     "primeira linha\nsegunda linha\nterceira linha\n") == 0,
          "append does not duplicate a newline already present");
}

static void test_move_and_rename(struct shell_context *ctx) {
    struct capy_ai_output call;
    char detail[CAPYAI_SUMMARY_MAX];

    reset_vfs();
    (void)add_node("/workspace/docs/move.txt", VFS_MODE_FILE, "payload");
    call = call_for("file_move", "move", "docs/move.txt", "archive");
    CHECK(dispatch(ctx, &call, detail) == 0 &&
              !find_node("/workspace/docs/move.txt") &&
              strcmp(node_text("/workspace/archive/move.txt"), "payload") == 0,
          "move into an existing folder preserves name and content");
    CHECK(strcmp(detail, "arquivo movido: /workspace/archive/move.txt") == 0,
          "folder move reports the final path");

    (void)add_node("/workspace/docs/old.txt", VFS_MODE_FILE, "rename-data");
    call = call_for("file_move", "move", "docs/old.txt", "docs/new.md");
    CHECK(dispatch(ctx, &call, detail) == 0 &&
              !find_node("/workspace/docs/old.txt") &&
              strcmp(node_text("/workspace/docs/new.md"), "rename-data") == 0,
          "typed move also performs a text-file rename");

    (void)add_node("/workspace/docs/source.txt", VFS_MODE_FILE, "source");
    (void)add_node("/workspace/docs/existing.txt", VFS_MODE_FILE, "existing");
    call = call_for("file_move", "move", "docs/source.txt",
                    "docs/existing.txt");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              strcmp(node_text("/workspace/docs/source.txt"), "source") == 0 &&
              strcmp(node_text("/workspace/docs/existing.txt"), "existing") == 0,
          "move never overwrites an existing file");

    call = call_for("file_move", "move", "docs/source.txt", "docs/output.bin");
    CHECK(dispatch(ctx, &call, detail) == 0 &&
              find_node("/workspace/docs/source.txt") == NULL &&
              strcmp(node_text("/workspace/docs/output.bin"), "source") == 0,
          "move accepts an exact non-text destination without guessing");

    (void)add_node("/workspace/docs/readme-source.txt", VFS_MODE_FILE, "readme");
    call = call_for("file_move", "move", "docs/readme-source.txt", "README");
    CHECK(dispatch(ctx, &call, detail) == 0 &&
              strcmp(node_text("/workspace/README"), "readme") == 0,
          "move accepts an exact extensionless destination");

    (void)add_node("/workspace/docs/folder-source.txt", VFS_MODE_FILE, "folder");
    call = call_for("file_move", "move", "docs/folder-source.txt", "missing-folder/");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              find_node("/workspace/docs/folder-source.txt") != NULL,
          "move rejects an explicit destination folder that is missing");

    (void)add_node("/workspace/docs/fail-source.txt", VFS_MODE_FILE, "stable");
    fail_rename = 1;
    call = call_for("file_move", "move", "docs/fail-source.txt", "docs/final.txt");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              find_node("/workspace/docs/fail-source.txt") != NULL &&
              find_node("/workspace/docs/final.txt") == NULL,
          "VFS rename failure leaves the source in place");
}

static void test_invalid_inputs_and_failures(struct shell_context *ctx) {
    struct capy_ai_output call;
    char detail[CAPYAI_SUMMARY_MAX];
    char long_path[CAPY_AI_STR_MAX];

    reset_vfs();
    call = call_for("file_create", "touch", "docs/program.bin", "binary");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              !find_node("/workspace/docs/program.bin"),
          "create rejects unsupported file extensions");

    call = call_for("file_create", "touch", "docs/../escape.txt", "blocked");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              !find_node("/workspace/escape.txt"),
          "parent traversal is blocked before reaching the VFS");

    call = call_for("file_create", "touch", "/system/escape.txt", "blocked");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              !find_node("/system/escape.txt"),
          "absolute path outside the authenticated home is blocked");

    call = call_for("file_create", "touch", "/workspace-other/prefix.txt",
                    "blocked");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              !find_node("/workspace-other/prefix.txt"),
          "home prefix collision is blocked at the path boundary");

    call = call_for("file_create", "touch", "missing/note.txt", "blocked");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              !find_node("/workspace/missing/note.txt"),
          "create fails when the requested parent folder does not exist");

    memset(long_path, 'a', sizeof(long_path));
    memcpy(long_path + sizeof(long_path) - 5u, ".txt", 5u);
    call = call_for("file_create", "touch", long_path, "blocked");
    CHECK(dispatch(ctx, &call, detail) != 0,
          "resolved paths exceeding the shell buffer fail closed");

    call = call_for("file_create", "kill-file", "docs/schema.txt", "blocked");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              !find_node("/workspace/docs/schema.txt"),
          "wrong action-command schema cannot reach the adapter");

    call = call_for("file_create", "touch", "docs/control.txt", "ok");
    call.content[1] = '\x01';
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              !find_node("/workspace/docs/control.txt"),
          "control bytes in text content are rejected");

    call = call_for("file_edit_text", "file-edit", "docs/missing.txt", "text");
    CHECK(dispatch(ctx, &call, detail) != 0,
          "append to a missing file fails without creating it");

    call = call_for("file_edit_text", "file-edit", "docs/missing.txt", "");
    CHECK(dispatch(ctx, &call, detail) != 0,
          "empty append content is rejected by the typed schema");

    write_calls = 0;
    fail_write_call = 1;
    call = call_for("file_create", "touch", "docs/write-fail.txt", "payload");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              !find_node("/workspace/docs/write-fail.txt"),
          "failed create write removes the incomplete file");
    fail_write_call = 0;

    fail_open_write = 1;
    call = call_for("file_create", "touch", "docs/open-fail.txt", "payload");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              !find_node("/workspace/docs/open-fail.txt"),
          "failed create open removes the empty file");
    fail_open_write = 0;

    fail_next_stat = 1;
    call = call_for("file_create", "touch", "docs/stat-fail.txt", "payload");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              !find_node("/workspace/docs/stat-fail.txt"),
          "failed post-create verification removes the new file");

    (void)add_node("/workspace/docs/stable.txt", VFS_MODE_FILE, "stable");
    write_calls = 0;
    fail_write_call = 2;
    call = call_for("file_edit_text", "file-edit", "docs/stable.txt", "new");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              strcmp(node_text("/workspace/docs/stable.txt"), "stable") == 0,
          "partial append failure rolls back to the exact original content");
    fail_write_call = 0;

    rename_calls = 0;
    fail_rename_call = 2;
    call = call_for("file_edit_text", "file-edit", "docs/stable.txt", "new");
    CHECK(dispatch(ctx, &call, detail) != 0 &&
              strcmp(node_text("/workspace/docs/stable.txt"), "stable") == 0,
          "replacement rename failure restores the original file");
}

int main(void) {
    struct session_context session;
    struct shell_context ctx;
    struct capy_ai_output call;
    char detail[CAPYAI_SUMMARY_MAX];

    printf("[test_capyai_native_files]\n");
    memset(&session, 0, sizeof(session));
    memset(&ctx, 0, sizeof(ctx));
    (void)copy_string(session.cwd, sizeof(session.cwd), "/workspace");
    (void)copy_string(session.user.home, sizeof(session.user.home), "/workspace");
    ctx.session = &session;

    reset_vfs();
    call = call_for("file_create", "touch", "docs/null.txt", "data");
    CHECK(capyai_native_file_dispatch(NULL, &call, detail, sizeof(detail)) != 0 &&
              capyai_native_file_dispatch(&ctx, NULL, detail, sizeof(detail)) != 0,
          "NULL context and call fail closed");

    test_create_and_append(&ctx);
    test_move_and_rename(&ctx);
    test_invalid_inputs_and_failures(&ctx);

    if (failures == 0) {
        printf("[test_capyai_native_files] all passed\n");
        return 0;
    }
    printf("[test_capyai_native_files] %d FAILED\n", failures);
    return 1;
}
