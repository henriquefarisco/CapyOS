#include "vfs.h"

#include "kmem.h"

static struct super_block *root_sb = NULL;

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
        return NULL;
    }
    if (path[0] == '\0' || path[0] != '/') {
        return NULL;
    }

    const char *p = path;
    struct dentry *current = root_sb->root;
    char component[VFS_NAME_MAX];

    while (1) {
        int res = path_next(&p, component);
        if (res < 0) {
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
                return NULL;
            }
            struct inode *new_inode = NULL;
            if (current->inode->ops->lookup(current->inode, component, &new_inode) != 0 || !new_inode) {
                return NULL;
            }
            child = dentry_alloc(component, new_inode, current);
            if (!child) {
                return NULL;
            }
            dentry_attach_child(current, child);
        }
        current = child;
    }
}

int vfs_init(void) {
    root_sb = NULL;
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
        return -1;
    }
    struct dentry *d = vfs_resolve(path, 0, NULL);
    if (!d) {
        return -1;
    }
    *out = d;
    d->refcount++;
    return 0;
}

int vfs_create(const char *path, uint16_t mode) {
    char name[VFS_NAME_MAX];
    struct dentry *parent = vfs_resolve(path, 1, name);
    if (!parent) {
        return -1;
    }
    if (name[0] == '\0') {
        return -1;
    }
    if (!parent->inode || !parent->inode->ops || !parent->inode->ops->create) {
        return -1;
    }
    struct inode *inode = NULL;
    if (parent->inode->ops->create(parent->inode, name, mode, &inode) != 0 || !inode) {
        return -1;
    }
    struct dentry *child = dentry_alloc(name, inode, parent);
    if (!child) {
        return -1;
    }
    dentry_attach_child(parent, child);
    return 0;
}

struct file *vfs_open(const char *path, uint32_t flags) {
    (void)flags;
    struct dentry *d = vfs_resolve(path, 0, NULL);
    if (!d || !d->inode) {
        return NULL;
    }
    struct file *f = (struct file *)kalloc(sizeof(struct file));
    if (!f) {
        return NULL;
    }
    f->dentry = d;
    f->flags = flags;
    f->position = 0;
    if (d->inode->ops && d->inode->ops->open) {
        if (d->inode->ops->open(d->inode, f) != 0) {
            kfree(f);
            return NULL;
        }
    }
    d->refcount++;
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
    long bytes = file->dentry->inode->ops->write(file, buffer, size);
    if (bytes > 0) {
        file->position += (uint32_t)bytes;
    }
    return bytes;
}
