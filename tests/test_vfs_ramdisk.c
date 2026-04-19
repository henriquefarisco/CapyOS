#include "fs/vfs.h"
#include "fs/capyfs.h"
#include "fs/ramdisk.h"
#include "fs/buffer.h"
#include <stdio.h>
#include <stdlib.h>

void fbcon_print(const char *msg) { printf("%s", msg); }
void fbcon_print_hex(uint32_t val) { printf("%08x", val); }
void vga_write(const char *msg) { printf("%s", msg); }
void vga_newline() { printf("\n"); }

void progress(const char *stage, uint32_t pct) { (void)stage; (void)pct; }
struct session_context *session_active() { return NULL; }

static int expect_result(const char *label, int actual, int expected) {
    if (actual != expected) {
        printf("%s expected %d got %d\n", label, expected, actual);
        return 1;
    }
    return 0;
}

static int count_entry_cb(const char *name, uint16_t mode, void *ctx) {
    int *count = (int *)ctx;
    (void)name;
    (void)mode;
    if (count) {
        (*count)++;
    }
    return 0;
}

static int ensure_dir_recursive(const char *path) {
    char build[128];
    size_t build_len = 1;
    const char *p = path;

    if (!path || path[0] != '/') return -1;
    build[0] = '/'; build[1] = '\0';
    while (*p == '/') p++;

    while (*p) {
        const char *start = p;
        size_t len = 0;
        while (start[len] && start[len] != '/') len++;

        if (len > 0) {
            if (build_len > 1) build[build_len++] = '/';
            for (size_t i = 0; i < len; ++i) build[build_len++] = start[i];
            build[build_len] = '\0';

            struct dentry *d = NULL;
            if (vfs_lookup(build, &d) != 0) {
                if (vfs_create(build, VFS_MODE_DIR, NULL) != 0) {
                    printf("Failed to create %s\n", build);
                    return -1;
                }
            } else if (d && d->refcount) {
                d->refcount--;
            }
        }
        p += len;
        while (*p == '/') p++;
    }
    return 0;
}

int main() {
    buffer_cache_init();
    vfs_init();
    ramdisk_init(512);

    struct block_device *ram = ramdisk_device();
    if (!ram) { printf("Failed to get ramdisk\n"); return 1; }

    int rc = capyfs_format(ram, 128, ram->block_count, progress);
    if (rc != 0) { printf("Format failed: %d\n", rc); return 1; }

    struct super_block *sb = malloc(sizeof(*sb));
    if (mount_capyfs(ram, sb) != 0) { printf("Mount failed\n"); return 1; }
    vfs_mount_root(sb);

    const char *dirs[] = {
        "/bin", "/etc", "/home", "/var", "/var/log", "/usr",
        "/tmp", "/dev", "/proc", "/sys", "/usr/bin", "/usr/lib",
        "/mnt", "/mnt/c", "/run", "/root", "/opt", "/srv",
        "/boot", "/boot/efi"
    };

    for (int i = 0; i < 20; i++) {
        if (ensure_dir_recursive(dirs[i]) != 0) {
            printf("Error on %s\n", dirs[i]);
            return 1;
        }
    }

    if (expect_result("create file", vfs_create("/home/demo.txt", VFS_MODE_FILE, NULL), 0) != 0) {
        return 1;
    }
    if (expect_result("duplicate file", vfs_create("/home/demo.txt", VFS_MODE_FILE, NULL), -VFS_ERR_ALREADY_EXISTS) != 0) {
        return 1;
    }
    if (expect_result("invalid path", vfs_create("demo.txt", VFS_MODE_FILE, NULL), -VFS_ERR_INVALID_PATH) != 0) {
        return 1;
    }
    if (expect_result("missing stat", vfs_stat_path("/missing", &(struct vfs_stat){0}), -VFS_ERR_NOT_FOUND) != 0) {
        return 1;
    }
    if (expect_result("missing listdir", vfs_listdir("/missing", count_entry_cb, &(int){0}), -VFS_ERR_NOT_FOUND) != 0) {
        return 1;
    }
    if (expect_result("unlink dir as file", vfs_unlink("/home"), -VFS_ERR_IS_DIRECTORY) != 0) {
        return 1;
    }
    if (expect_result("rmdir non-empty", vfs_rmdir("/home"), -VFS_ERR_DIR_NOT_EMPTY) != 0) {
        return 1;
    }
    if (expect_result("missing last error", vfs_last_error(), VFS_ERR_DIR_NOT_EMPTY) != 0) {
        return 1;
    }
    if (expect_result("unlink file", vfs_unlink("/home/demo.txt"), 0) != 0) {
        return 1;
    }
    if (expect_result("rmdir empty", vfs_rmdir("/home"), 0) != 0) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
struct user_record { uint32_t uid, gid; };
struct user_record *session_user(void *sess) { (void)sess; return NULL; }
void *kalloc(size_t sz) { return calloc(1, sz); }
void kfree(void *ptr) { free(ptr); }
