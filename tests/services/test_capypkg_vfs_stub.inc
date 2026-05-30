static struct file g_vfs_file;
static char g_vfs_open_path[160];
static char g_vfs_lookup_path[160];
static struct dentry g_vfs_lookup_dentry;
static struct session_context *g_test_active_session;
static const char *g_vfs_create_already_path;
static const char *g_vfs_create_fail_path;
static const char *g_vfs_open_fail_path;
static const char *g_vfs_rmdir_fail_path;
static const char *g_vfs_unlink_fail_path;
static const char *g_vfs_write_fail_path;
static int g_vfs_last_error = VFS_OK;

int vfs_last_error(void) {
    return g_vfs_last_error;
}

static void reset_bootstrap_vfs_state(void) {
    memset(&g_vfs_file, 0, sizeof(g_vfs_file));
    memset(&g_vfs_lookup_dentry, 0, sizeof(g_vfs_lookup_dentry));
    g_vfs_open_path[0] = '\0';
    g_vfs_lookup_path[0] = '\0';
    g_test_active_session = NULL;
    g_vfs_create_already_path = NULL;
    g_vfs_create_fail_path = NULL;
    g_vfs_open_fail_path = NULL;
    g_vfs_rmdir_fail_path = NULL;
    g_vfs_unlink_fail_path = NULL;
    g_vfs_write_fail_path = NULL;
    g_vfs_last_error = VFS_OK;
}

__attribute__((weak)) void session_set_active(struct session_context *ctx) {
    g_test_active_session = ctx;
}

__attribute__((weak)) struct session_context *session_active(void) {
    return g_test_active_session;
}

static void vfs_remember_path(const char *path) {
    if (!path) {
        g_vfs_open_path[0] = '\0';
        return;
    }
    strncpy(g_vfs_open_path, path, sizeof(g_vfs_open_path) - 1u);
    g_vfs_open_path[sizeof(g_vfs_open_path) - 1u] = '\0';
}

int vfs_lookup(const char *path, struct dentry **out) {
    struct fake_file *f = NULL;
    if (!out) {
        g_vfs_last_error = VFS_ERR_INVALID_ARGUMENT;
        return -1;
    }
    if (!path) {
        g_vfs_last_error = VFS_ERR_INVALID_ARGUMENT;
        return -1;
    }
    f = fs_find(path);
    if (!f) {
        g_vfs_last_error = VFS_ERR_NOT_FOUND;
        return -1;
    }
    memset(&g_vfs_lookup_dentry, 0, sizeof(g_vfs_lookup_dentry));
    strncpy(g_vfs_lookup_dentry.name, path,
            sizeof(g_vfs_lookup_dentry.name) - 1u);
    strncpy(g_vfs_lookup_path, path, sizeof(g_vfs_lookup_path) - 1u);
    g_vfs_lookup_path[sizeof(g_vfs_lookup_path) - 1u] = '\0';
    g_vfs_lookup_dentry.refcount = 1u;
    *out = &g_vfs_lookup_dentry;
    g_vfs_last_error = VFS_OK;
    return 0;
}

int vfs_create(const char *path, uint16_t mode,
               const struct vfs_metadata *meta) {
    struct fake_file *f = NULL;
    (void)meta;
    if (!path) {
        g_vfs_last_error = VFS_ERR_INVALID_ARGUMENT;
        return -1;
    }
    if (!fs_parent_dir_exists(path)) {
        g_vfs_last_error = VFS_ERR_NOT_FOUND;
        return -1;
    }
    if (g_vfs_create_fail_path && strcmp(path, g_vfs_create_fail_path) == 0) {
        g_vfs_last_error = VFS_ERR_IO;
        return -1;
    }
    if (g_vfs_create_already_path &&
        strcmp(path, g_vfs_create_already_path) == 0) {
        f = fs_find(path);
        if (!f) {
            f = fs_alloc(path);
            if (!f) {
                g_vfs_last_error = VFS_ERR_NO_MEMORY;
                return -1;
            }
            f->is_dir = (mode & VFS_MODE_DIR) ? 1 : 0;
            f->is_text = f->is_dir ? 0 : 1;
            f->text[0] = '\0';
            f->bytes_len = 0u;
        }
        g_vfs_last_error = VFS_ERR_ALREADY_EXISTS;
        return -1;
    }
    f = fs_find(path);
    if (f) {
        g_vfs_last_error = VFS_ERR_ALREADY_EXISTS;
        return -1;
    }
    f = fs_alloc(path);
    if (!f) {
        g_vfs_last_error = VFS_ERR_NO_MEMORY;
        return -1;
    }
    f->is_dir = (mode & VFS_MODE_DIR) ? 1 : 0;
    f->is_text = f->is_dir ? 0 : 1;
    f->text[0] = '\0';
    f->bytes_len = 0u;
    g_vfs_last_error = VFS_OK;
    return 0;
}

struct file *vfs_open(const char *path, uint32_t flags) {
    struct fake_file *f = NULL;
    if (!path) {
        g_vfs_last_error = VFS_ERR_INVALID_ARGUMENT;
        return NULL;
    }
    f = fs_find(path);
    if (!f) {
        g_vfs_last_error = VFS_ERR_NOT_FOUND;
        return NULL;
    }
    if (f->is_dir) {
        g_vfs_last_error = VFS_ERR_IS_DIRECTORY;
        return NULL;
    }
    if (g_vfs_open_fail_path && strcmp(path, g_vfs_open_fail_path) == 0) {
        g_vfs_last_error = VFS_ERR_IO;
        return NULL;
    }
    memset(&g_vfs_file, 0, sizeof(g_vfs_file));
    g_vfs_file.flags = flags ? flags : VFS_OPEN_READ;
    vfs_remember_path(path);
    g_vfs_last_error = VFS_OK;
    return &g_vfs_file;
}

int vfs_close(struct file *file) {
    if (!file) {
        g_vfs_last_error = VFS_ERR_INVALID_ARGUMENT;
        return -1;
    }
    memset(file, 0, sizeof(*file));
    g_vfs_open_path[0] = '\0';
    g_vfs_last_error = VFS_OK;
    return 0;
}

long vfs_read(struct file *file, void *buffer, size_t size) {
    struct fake_file *f = fs_find(g_vfs_open_path);
    size_t len = 0u;
    size_t available = 0u;
    size_t to_copy = 0u;
    if (!file || !buffer || !f) {
        g_vfs_last_error = VFS_ERR_INVALID_ARGUMENT;
        return -1;
    }
    if (f->is_dir) {
        g_vfs_last_error = VFS_ERR_IS_DIRECTORY;
        return -1;
    }
    len = strlen(f->text);
    if (file->position >= len) {
        g_vfs_last_error = VFS_OK;
        return 0;
    }
    available = len - file->position;
    to_copy = available < size ? available : size;
    memcpy(buffer, f->text + file->position, to_copy);
    file->position += (uint32_t)to_copy;
    g_vfs_last_error = VFS_OK;
    return (long)to_copy;
}

long vfs_write(struct file *file, const void *buffer, size_t size) {
    struct fake_file *f = NULL;
    size_t pos = 0u;
    size_t end = 0u;
    if (!file || (!buffer && size > 0u)) {
        g_vfs_last_error = VFS_ERR_INVALID_ARGUMENT;
        return -1;
    }
    if (!(file->flags & VFS_OPEN_WRITE)) {
        g_vfs_last_error = VFS_ERR_PERMISSION_DENIED;
        return -1;
    }
    if (g_vfs_write_fail_path &&
        strcmp(g_vfs_open_path, g_vfs_write_fail_path) == 0) {
        g_vfs_last_error = VFS_ERR_IO;
        return -1;
    }
    f = fs_find(g_vfs_open_path);
    if (!f || f->is_dir) {
        g_vfs_last_error = f && f->is_dir ? VFS_ERR_IS_DIRECTORY : VFS_ERR_IO;
        return -1;
    }
    if (size == 0u) {
        g_vfs_last_error = VFS_OK;
        return 0;
    }
    pos = file->position;
    if (pos >= sizeof(f->text) || size >= sizeof(f->text) - pos) {
        g_vfs_last_error = VFS_ERR_IO;
        return -1;
    }
    end = pos + size;
    if (size > 0u) {
        memcpy(f->text + pos, buffer, size);
    }
    f->text[end] = '\0';
    f->is_text = 1;
    f->bytes_len = 0u;
    file->position += (uint32_t)size;
    g_vfs_last_error = VFS_OK;
    return (long)size;
}

int vfs_unlink(const char *path) {
    struct fake_file *f = NULL;
    if (!path) {
        g_vfs_last_error = VFS_ERR_INVALID_ARGUMENT;
        return -1;
    }
    if (path && g_vfs_lookup_dentry.refcount > 0u &&
        strcmp(g_vfs_lookup_path, path) == 0) {
        g_vfs_last_error = VFS_ERR_IO;
        return -1;
    }
    if (g_vfs_unlink_fail_path && strcmp(path, g_vfs_unlink_fail_path) == 0) {
        g_vfs_last_error = VFS_ERR_IO;
        return -1;
    }
    f = fs_find(path);
    if (!f) {
        g_vfs_last_error = VFS_ERR_NOT_FOUND;
        return -1;
    }
    if (f && f->is_dir) {
        g_vfs_last_error = VFS_ERR_IS_DIRECTORY;
        return -1;
    }
    if (fs_remove(path) != 0) {
        g_vfs_last_error = VFS_ERR_IO;
        return -1;
    }
    g_vfs_last_error = VFS_OK;
    return 0;
}

int vfs_rmdir(const char *path) {
    struct fake_file *f = NULL;
    if (!path) {
        g_vfs_last_error = VFS_ERR_INVALID_ARGUMENT;
        return -1;
    }
    if (path && g_vfs_lookup_dentry.refcount > 0u &&
        strcmp(g_vfs_lookup_path, path) == 0) {
        g_vfs_last_error = VFS_ERR_IO;
        return -1;
    }
    if (g_vfs_rmdir_fail_path && strcmp(path, g_vfs_rmdir_fail_path) == 0) {
        g_vfs_last_error = VFS_ERR_IO;
        return -1;
    }
    f = fs_find(path);
    if (!f) {
        g_vfs_last_error = VFS_ERR_NOT_FOUND;
        return -1;
    }
    if (!f->is_dir) {
        g_vfs_last_error = VFS_ERR_NOT_DIRECTORY;
        return -1;
    }
    if (fs_has_child(path)) {
        g_vfs_last_error = VFS_ERR_DIR_NOT_EMPTY;
        return -1;
    }
    memset(f, 0, sizeof(*f));
    g_vfs_last_error = VFS_OK;
    return 0;
}

int vfs_stat_path(const char *path, struct vfs_stat *out) {
    struct fake_file *f = NULL;
    if (!out) {
        g_vfs_last_error = VFS_ERR_INVALID_ARGUMENT;
        return -1;
    }
    if (!path) {
        g_vfs_last_error = VFS_ERR_INVALID_ARGUMENT;
        return -1;
    }
    f = fs_find(path);
    if (!f) {
        g_vfs_last_error = VFS_ERR_NOT_FOUND;
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->size = f->is_text ? (uint32_t)strlen(f->text)
                           : (uint32_t)f->bytes_len;
    out->mode = f->is_dir ? VFS_MODE_DIR : VFS_MODE_FILE;
    out->perm = 0644;
    g_vfs_last_error = VFS_OK;
    return 0;
}

static int vfs_path_exists(const char *path) {
    struct vfs_stat st;
    return vfs_stat_path(path, &st) == 0 ? 1 : 0;
}
