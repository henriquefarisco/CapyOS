#include "audio/builtin_music.h"
#include "fs/vfs.h"

static int ensure_directory(const char *path, uint16_t perm) {
    struct vfs_stat st;
    if (vfs_stat_path(path, &st) == VFS_OK) return st.mode == VFS_MODE_DIR ? 0 : -1;
    struct vfs_metadata meta = {0, 0, perm};
    return vfs_create(path, VFS_MODE_DIR, &meta) == VFS_OK ? 0 : -1;
}

int builtin_music_seed(const struct builtin_music_track *tracks, size_t count) {
    if (!tracks || count > 3 || ensure_directory("/Music", 0755) ||
        ensure_directory("/system", 0755) ||
        ensure_directory("/system/music-install", 0700)) return -1;
    for (size_t i = 0; i < count; ++i) {
        const struct builtin_music_track *t = &tracks[i];
        struct vfs_stat st;
        if (!t->path || !t->staging || !t->data || !t->size || t->size > 4u*1024u*1024u) return -1;
        if (vfs_stat_path(t->path, &st) == VFS_OK) {
            if (st.mode != VFS_MODE_FILE) return -1;
            continue; /* Never overwrite a user's replacement. */
        }
        /* This private service-owned staging namespace is recoverable after
         * interruption; destination publication happens only after readback. */
        if (vfs_stat_path(t->staging, &st) == VFS_OK &&
            (st.mode != VFS_MODE_FILE || vfs_unlink(t->staging) != VFS_OK)) return -1;
        struct vfs_metadata meta = {0, 0, 0644};
        if (vfs_create(t->staging, VFS_MODE_FILE, &meta) != VFS_OK) return -1;
        struct file *file = vfs_open(t->staging, VFS_OPEN_WRITE);
        size_t done = 0;
        if (file) {
            while (done < t->size) {
                size_t n = t->size - done;
                if (n > 16384) n = 16384;
                long written = vfs_write(file, t->data + done, n);
                if (written <= 0 || (size_t)written > n) break;
                done += (size_t)written;
            }
            vfs_close(file);
        }
        if (done != t->size) { (void)vfs_unlink(t->staging); return -1; }
        file = vfs_open(t->staging, VFS_OPEN_READ);
        uint8_t buffer[4096];
        done = 0;
        if (file) {
            while (done < t->size) {
                size_t n = t->size - done;
                if (n > sizeof(buffer)) n = sizeof(buffer);
                long read = vfs_read(file, buffer, n);
                if (read <= 0 || (size_t)read > n) break;
                size_t j = 0;
                while (j < (size_t)read && buffer[j] == t->data[done + j]) ++j;
                if (j != (size_t)read) break;
                done += (size_t)read;
            }
            vfs_close(file);
        }
        if (done != t->size) { (void)vfs_unlink(t->staging); return -1; }
        if (vfs_rename(t->staging, t->path) != VFS_OK) return -1;
    }
    return 0;
}
