/* vfs.c: VFS root state, dentry tree, and metadata defaults. */
#include "fs/vfs.h"
#include "fs/capyfs.h"

#include "memory/kmem.h"
#include "auth/session.h"

static struct super_block *root_sb = NULL;
static int g_vfs_last_error = VFS_OK;

static int vfs_normalize_error(int error) {
    return error < 0 ? -error : error;
}

static void vfs_set_error(int error) {
    g_vfs_last_error = vfs_normalize_error(error);
}

static void vfs_set_ok(void) {
    g_vfs_last_error = VFS_OK;
}

static int vfs_fail(int error) {
    vfs_set_error(error);
    return -g_vfs_last_error;
}

static int vfs_fail_last_or(int fallback) {
    return vfs_fail(g_vfs_last_error != VFS_OK ? g_vfs_last_error : fallback);
}

static void dentry_free_tree(struct dentry *d) {
    if (!d) {
        return;
    }
    struct dentry *child = d->first_child;
    while (child) {
        struct dentry *next = child->next_sibling;
        dentry_free_tree(child);
        child = next;
    }
    kfree(d);
}

static void fill_default_metadata(uint16_t mode, struct vfs_metadata *meta) {
    struct session_context *sess = session_active();
    const struct user_record *user = sess ? session_user(sess) : NULL;
    if (user) {
        meta->uid = user->uid;
        meta->gid = user->gid;
    } else {
        meta->uid = 0;
        meta->gid = 0;
    }
    meta->perm = (mode & VFS_MODE_DIR) ? 0755 : 0644;
}

static int inode_has_permission(struct inode *inode, uint16_t needed) {
    if (!inode) {
        return 0;
    }
    struct session_context *sess = session_active();
    if (!sess) {
        return 1; // sistema (root)
    }
    const struct user_record *user = session_user(sess);
    if (!user) {
        return 1;
    }
    if (user->uid == 0) {
        return 1;
    }

    uint16_t perm = inode->perm;
    uint16_t bits;
    if (user->uid == inode->uid) {
        bits = (perm >> 6) & 0x7;
    } else if (user->gid == inode->gid) {
        bits = (perm >> 3) & 0x7;
    } else {
        bits = perm & 0x7;
    }
    return ((bits & needed) == needed);
}

static void copy_name(char dest[VFS_NAME_MAX], const char *src) {
    size_t i = 0;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    for (; i < VFS_NAME_MAX - 1 && src[i]; ++i) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static int name_equal(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == b[i];
}

static struct dentry *dentry_alloc(const char *name, struct inode *inode, struct dentry *parent) {
    struct dentry *d = (struct dentry *)kalloc(sizeof(struct dentry));
    if (!d) {
        return NULL;
    }
    copy_name(d->name, name);
    d->inode = inode;
    d->parent = parent;
    d->first_child = NULL;
    d->next_sibling = NULL;
    d->refcount = 1;
    return d;
}

static void dentry_attach_child(struct dentry *parent, struct dentry *child) {
    if (!parent || !child) {
        return;
    }
    child->next_sibling = parent->first_child;
    parent->first_child = child;
}

static struct dentry *dentry_detach_child(struct dentry *parent, const char *name) {
    if (!parent || !name) {
        return NULL;
    }
    struct dentry *prev = NULL;
    struct dentry *cur = parent->first_child;
    while (cur) {
        if (name_equal(cur->name, name)) {
            if (prev) {
                prev->next_sibling = cur->next_sibling;
            } else {
                parent->first_child = cur->next_sibling;
            }
            cur->next_sibling = NULL;
            return cur;
        }
        prev = cur;
        cur = cur->next_sibling;
    }
    return NULL;
}

static struct dentry *dentry_lookup_child(struct dentry *parent, const char *name) {
    struct dentry *child = parent ? parent->first_child : NULL;
    while (child) {
        if (name_equal(child->name, name)) {
            return child;
        }
        child = child->next_sibling;
    }
    return NULL;
}

static int path_next(const char **path, char component[VFS_NAME_MAX]) {
    const char *p = *path;
    size_t len = 0;

    while (*p == '/') {
        ++p;
    }
    if (*p == '\0') {
        *path = p;
        return 0;
    }
    while (*p && *p != '/') {
        if (len >= VFS_NAME_MAX - 1) {
            return -1;
        }
        component[len++] = *p++;
    }
    component[len] = '\0';
    while (*p == '/') {
        ++p;
    }
    *path = p;
    return 1;
}

static struct dentry *vfs_resolve(const char *path, int parent_only, char *last_component) {
    if (!root_sb || !root_sb->root || !path) {
        vfs_set_error(path ? VFS_ERR_IO : VFS_ERR_INVALID_ARGUMENT);
        return NULL;
    }
    if (path[0] == '\0' || path[0] != '/') {
        vfs_set_error(VFS_ERR_INVALID_PATH);
        return NULL;
    }

    const char *p = path;
    struct dentry *current = root_sb->root;
    char component[VFS_NAME_MAX];

    while (1) {
        int res = path_next(&p, component);
        if (res < 0) {
            vfs_set_error(VFS_ERR_NAME_TOO_LONG);
            return NULL;
        }
        if (res == 0) {
            if (parent_only && last_component) {
                last_component[0] = '\0';
            }
            return current;
        }

        if (parent_only && *p == '\0') {
            if (last_component) {
                copy_name(last_component, component);
            }
            return current;
        }

        struct dentry *child = dentry_lookup_child(current, component);
        if (!child) {
            if (!current->inode || !current->inode->ops || !current->inode->ops->lookup) {
                vfs_set_error(VFS_ERR_UNSUPPORTED);
                return NULL;
            }
            struct inode *new_inode = NULL;
            if (current->inode->ops->lookup(current->inode, component, &new_inode) != 0 || !new_inode) {
                vfs_set_error(VFS_ERR_NOT_FOUND);
                return NULL;
            }
            child = dentry_alloc(component, new_inode, current);
            if (!child) {
                vfs_set_error(VFS_ERR_NO_MEMORY);
                return NULL;
            }
            dentry_attach_child(current, child);
        }
        current = child;
    }
}

static int vfs_iter_stop_after_one(const char *name, uint16_t mode, void *ctx) {
    int *found = (int *)ctx;
    (void)name;
    (void)mode;
    if (found) {
        *found = 1;
    }
    return 1;
}

static int vfs_directory_has_entries(struct dentry *d) {
    int found = 0;
    if (!d || !d->inode || !d->inode->ops || !d->inode->ops->iterate) {
        return 0;
    }
    (void)d->inode->ops->iterate(d->inode, vfs_iter_stop_after_one, &found);
    return found;
}

int vfs_init(void) {
    root_sb = NULL;
    vfs_set_ok();
    return 0;
}

int vfs_mount_root(struct super_block *sb) {
    if (!sb || !sb->root) {
        return -1;
    }
    root_sb = sb;
    return 0;
}

struct super_block *vfs_root(void) {
    return root_sb;
}

int vfs_lookup(const char *path, struct dentry **out) {
    if (!out) {
        return vfs_fail(VFS_ERR_INVALID_ARGUMENT);
    }
    vfs_set_ok();
    struct dentry *d = vfs_resolve(path, 0, NULL);
    if (!d) {
        return vfs_fail_last_or(VFS_ERR_NOT_FOUND);
    }
    *out = d;
    d->refcount++;
    vfs_set_ok();
    return 0;
}

int vfs_create(const char *path, uint16_t mode, const struct vfs_metadata *meta) {
    vfs_set_ok();
    char name[VFS_NAME_MAX];
    struct dentry *parent = vfs_resolve(path, 1, name);
    struct dentry *existing = NULL;
    if (!parent) {
        return vfs_fail_last_or(VFS_ERR_INVALID_PATH);
    }
    if (name[0] == '\0') {
        return vfs_fail(VFS_ERR_INVALID_PATH);
    }

    existing = vfs_resolve(path, 0, NULL);
    if (existing) {
        return vfs_fail(VFS_ERR_ALREADY_EXISTS);
    }
    if (g_vfs_last_error != VFS_ERR_NOT_FOUND) {
        return vfs_fail_last_or(VFS_ERR_IO);
    }

    // Para o FS nativo atual (CAPYFS), garantimos que o conjunto de ops
    // a ser usado ÃƒÂ© o oficial, evitando qualquer salto indevido por corrupcao.
    if (parent->inode) {
        parent->inode->ops = capyfs_file_ops();
    }
    if (!parent->inode || !parent->inode->ops || !parent->inode->ops->create) {
        return vfs_fail(VFS_ERR_UNSUPPORTED);
    }
    if (!inode_has_permission(parent->inode, VFS_PERM_WRITE)) {
        return vfs_fail(VFS_ERR_PERMISSION_DENIED);
    }
    struct vfs_metadata local_meta;
    if (meta) {
        local_meta = *meta;
    } else {
        fill_default_metadata(mode, &local_meta);
    }
    struct inode *inode = NULL;
    // Chamada direta ao CAPYFS para evitar dependencia de ponteiros corrompidos
    if (capyfs_create_pub(parent->inode, name, mode, &local_meta, &inode) != 0 || !inode) {
        return vfs_fail(VFS_ERR_IO);
    }
    struct dentry *child = dentry_alloc(name, inode, parent);
    if (!child) {
        return vfs_fail(VFS_ERR_NO_MEMORY);
    }
    dentry_attach_child(parent, child);
    vfs_set_ok();
    return 0;
}

struct file *vfs_open(const char *path, uint32_t flags) {
    vfs_set_ok();
    struct dentry *d = vfs_resolve(path, 0, NULL);
    if (!d || !d->inode) {
        vfs_fail_last_or(VFS_ERR_NOT_FOUND);
        return NULL;
    }
    uint32_t open_flags = flags ? flags : VFS_OPEN_READ;
    if ((open_flags & VFS_OPEN_READ) && !inode_has_permission(d->inode, VFS_PERM_READ)) {
        vfs_set_error(VFS_ERR_PERMISSION_DENIED);
        return NULL;
    }
    if ((open_flags & VFS_OPEN_WRITE) && !inode_has_permission(d->inode, VFS_PERM_WRITE)) {
        vfs_set_error(VFS_ERR_PERMISSION_DENIED);
        return NULL;
    }
    struct file *f = (struct file *)kalloc(sizeof(struct file));
    if (!f) {
        vfs_set_error(VFS_ERR_NO_MEMORY);
        return NULL;
    }
    f->dentry = d;
    f->flags = open_flags;
    f->position = 0;
    if (d->inode->ops && d->inode->ops->open) {
        if (d->inode->ops->open(d->inode, f) != 0) {
            kfree(f);
            vfs_set_error(VFS_ERR_IO);
            return NULL;
        }
    }
    d->refcount++;
    vfs_set_ok();
    return f;
}

int vfs_close(struct file *file) {
    if (!file) {
        return -1;
    }
    if (file->dentry && file->dentry->inode && file->dentry->inode->ops && file->dentry->inode->ops->close) {
        file->dentry->inode->ops->close(file);
    }
    if (file->dentry && file->dentry->refcount) {
        file->dentry->refcount--;
    }
    kfree(file);
    return 0;
}

long vfs_read(struct file *file, void *buffer, size_t size) {
    if (!file || !file->dentry || !file->dentry->inode || !file->dentry->inode->ops || !file->dentry->inode->ops->read) {
        return -1;
    }
    long bytes = file->dentry->inode->ops->read(file, buffer, size);
    if (bytes > 0) {
        file->position += (uint32_t)bytes;
    }
    return bytes;
}

long vfs_write(struct file *file, const void *buffer, size_t size) {
    if (!file || !file->dentry || !file->dentry->inode || !file->dentry->inode->ops || !file->dentry->inode->ops->write) {
        return -1;
    }
    if (!(file->flags & VFS_OPEN_WRITE)) {
        return -1;
    }
    long bytes = file->dentry->inode->ops->write(file, buffer, size);
    if (bytes > 0) {
        file->position += (uint32_t)bytes;
    }
    return bytes;
}

int vfs_listdir(const char *path, vfs_iter_cb cb, void *ctx) {
    if (!path || !cb) {
        return vfs_fail(VFS_ERR_INVALID_ARGUMENT);
    }
    vfs_set_ok();
    struct dentry *d = vfs_resolve(path, 0, NULL);
    if (!d || !d->inode) {
        return vfs_fail_last_or(VFS_ERR_NOT_FOUND);
    }
    if ((d->inode->mode & VFS_MODE_DIR) == 0) {
        return vfs_fail(VFS_ERR_NOT_DIRECTORY);
    }
    if (!inode_has_permission(d->inode, VFS_PERM_READ | VFS_PERM_EXEC)) {
        return vfs_fail(VFS_ERR_PERMISSION_DENIED);
    }
    if (!d->inode->ops || !d->inode->ops->iterate) {
        return vfs_fail(VFS_ERR_UNSUPPORTED);
    }
    if (d->inode->ops->iterate(d->inode, cb, ctx) != 0) {
        return vfs_fail(VFS_ERR_IO);
    }
    vfs_set_ok();
    return 0;
}

int vfs_unlink(const char *path) {
    vfs_set_ok();
    struct dentry *target = vfs_resolve(path, 0, NULL);
    if (!target || !target->inode) {
        return vfs_fail_last_or(VFS_ERR_NOT_FOUND);
    }
    if (target->inode->mode & VFS_MODE_DIR) {
        return vfs_fail(VFS_ERR_IS_DIRECTORY);
    }
    struct dentry *parent = target->parent;
    if (!parent || target->name[0] == '\0') {
        return vfs_fail(VFS_ERR_INVALID_PATH);
    }
    if (!inode_has_permission(parent->inode, VFS_PERM_WRITE)) {
        return vfs_fail(VFS_ERR_PERMISSION_DENIED);
    }
    if (!parent->inode->ops || !parent->inode->ops->remove) {
        return vfs_fail(VFS_ERR_UNSUPPORTED);
    }
    if (parent->inode->ops->remove(parent->inode, target->name, 0) != 0) {
        return vfs_fail(VFS_ERR_IO);
    }
    struct dentry *removed = dentry_detach_child(parent, target->name);
    dentry_free_tree(removed);
    vfs_set_ok();
    return 0;
}

int vfs_rmdir(const char *path) {
    vfs_set_ok();
    struct dentry *target = vfs_resolve(path, 0, NULL);
    if (!target || !target->inode) {
        return vfs_fail_last_or(VFS_ERR_NOT_FOUND);
    }
    if ((target->inode->mode & VFS_MODE_DIR) == 0) {
        return vfs_fail(VFS_ERR_NOT_DIRECTORY);
    }
    struct dentry *parent = target->parent;
    if (!parent || target->name[0] == '\0') {
        return vfs_fail(VFS_ERR_INVALID_PATH);
    }
    if (!inode_has_permission(parent->inode, VFS_PERM_WRITE)) {
        return vfs_fail(VFS_ERR_PERMISSION_DENIED);
    }
    if (!parent->inode->ops || !parent->inode->ops->remove) {
        return vfs_fail(VFS_ERR_UNSUPPORTED);
    }
    if (vfs_directory_has_entries(target)) {
        return vfs_fail(VFS_ERR_DIR_NOT_EMPTY);
    }
    if (parent->inode->ops->remove(parent->inode, target->name, 1) != 0) {
        if (vfs_directory_has_entries(target)) {
            return vfs_fail(VFS_ERR_DIR_NOT_EMPTY);
        }
        return vfs_fail(VFS_ERR_IO);
    }
    struct dentry *removed = dentry_detach_child(parent, target->name);
    dentry_free_tree(removed);
    vfs_set_ok();
    return 0;
}

int vfs_rename(const char *src_path, const char *dst_path) {
    vfs_set_ok();
    if (!src_path || !dst_path) {
        return vfs_fail(VFS_ERR_INVALID_ARGUMENT);
    }
    char dst_name[VFS_NAME_MAX];
    struct dentry *src = vfs_resolve(src_path, 0, NULL);
    struct dentry *src_parent = src ? src->parent : NULL;
    struct dentry *dst_parent = vfs_resolve(dst_path, 1, dst_name);
    struct dentry *dst = NULL;
    if (!src || !src_parent) {
        return vfs_fail_last_or(VFS_ERR_NOT_FOUND);
    }
    if (!dst_parent || dst_name[0] == '\0') {
        return vfs_fail_last_or(VFS_ERR_INVALID_PATH);
    }
    dst = vfs_resolve(dst_path, 0, NULL);
    if (dst && dst != src) {
        return vfs_fail(VFS_ERR_ALREADY_EXISTS);
    }
    if (!dst && g_vfs_last_error != VFS_ERR_NOT_FOUND) {
        return vfs_fail_last_or(VFS_ERR_IO);
    }
    if (!inode_has_permission(src_parent->inode, VFS_PERM_WRITE) ||
        !inode_has_permission(dst_parent->inode, VFS_PERM_WRITE)) {
        return vfs_fail(VFS_ERR_PERMISSION_DENIED);
    }
    if (!src_parent->inode->ops || !src_parent->inode->ops->rename) {
        return vfs_fail(VFS_ERR_UNSUPPORTED);
    }
    if (src_parent->inode->ops->rename(src_parent->inode, src->name, dst_parent->inode, dst_name) != 0) {
        return vfs_fail(VFS_ERR_IO);
    }

    struct dentry *child = dentry_detach_child(src_parent, src->name);
    if (child) {
        copy_name(child->name, dst_name);
        child->parent = dst_parent;
        dentry_attach_child(dst_parent, child);
    }
    vfs_set_ok();
    return 0;
}

int vfs_stat_path(const char *path, struct vfs_stat *out) {
    if (!out) {
        return vfs_fail(VFS_ERR_INVALID_ARGUMENT);
    }
    vfs_set_ok();
    struct dentry *d = vfs_resolve(path, 0, NULL);
    if (!d || !d->inode) {
        return vfs_fail_last_or(VFS_ERR_NOT_FOUND);
    }
    if (!inode_has_permission(d->inode, VFS_PERM_READ)) {
        return vfs_fail(VFS_ERR_PERMISSION_DENIED);
    }
    if (d->inode->ops && d->inode->ops->stat) {
        if (d->inode->ops->stat(d->inode, out) != 0) {
            return vfs_fail(VFS_ERR_IO);
        }
        vfs_set_ok();
        return 0;
    }
    out->ino = d->inode->ino;
    out->size = d->inode->size;
    out->uid = d->inode->uid;
    out->gid = d->inode->gid;
    out->mode = d->inode->mode;
    out->perm = d->inode->perm;
    vfs_set_ok();
    return 0;
}

int vfs_set_metadata(const char *path, const struct vfs_metadata *meta) {
    if (!path || !meta) {
        return vfs_fail(VFS_ERR_INVALID_ARGUMENT);
    }
    vfs_set_ok();
    struct dentry *d = vfs_resolve(path, 0, NULL);
    if (!d || !d->inode) {
        return vfs_fail_last_or(VFS_ERR_NOT_FOUND);
    }
    if (!d->inode->ops || !d->inode->ops->set_metadata) {
        if (d && d->refcount) {
            d->refcount--;
        }
        return vfs_fail(VFS_ERR_UNSUPPORTED);
    }
    if (d->inode->ops->set_metadata(d->inode, meta) != 0) {
        if (d->refcount) {
            d->refcount--;
        }
        return vfs_fail(VFS_ERR_IO);
    }
    if (d->refcount) {
        d->refcount--;
    }
    vfs_set_ok();
    return 0;
}

int vfs_last_error(void) {
    return g_vfs_last_error;
}

const char *vfs_error_string(int error) {
    switch (vfs_normalize_error(error)) {
        case VFS_OK: return "ok";
        case VFS_ERR_INVALID_ARGUMENT: return "invalid argument";
        case VFS_ERR_INVALID_PATH: return "invalid path";
        case VFS_ERR_NAME_TOO_LONG: return "name too long";
        case VFS_ERR_NOT_FOUND: return "not found";
        case VFS_ERR_ALREADY_EXISTS: return "already exists";
        case VFS_ERR_NOT_DIRECTORY: return "not a directory";
        case VFS_ERR_IS_DIRECTORY: return "is a directory";
        case VFS_ERR_PERMISSION_DENIED: return "permission denied";
        case VFS_ERR_DIR_NOT_EMPTY: return "directory not empty";
        case VFS_ERR_UNSUPPORTED: return "operation unsupported";
        case VFS_ERR_NO_MEMORY: return "out of memory";
        case VFS_ERR_IO: return "io error";
        default: return "unknown error";
    }
}
