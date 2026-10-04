#include "audio/builtin_music.h"
#include "fs/vfs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t stored[40000], source[40000];
static size_t length;
static int temporary, published, fail_write, corrupt, renamed, existing;
static struct file handle;
int vfs_stat_path(const char *path, struct vfs_stat *st) {
    *st = (struct vfs_stat){0};
    if (!strcmp(path, "/Music") || !strcmp(path, "/system") ||
        !strcmp(path, "/system/music-install")) { st->mode = VFS_MODE_DIR; return VFS_OK; }
    if ((!strcmp(path, "/Music/song.ogg") && (published || existing)) ||
        (!strcmp(path, "/system/music-install/song.tmp") && temporary)) {
        st->mode = VFS_MODE_FILE; st->size = (uint32_t)length; return VFS_OK;
    }
    return VFS_ERR_NOT_FOUND;
}
int vfs_create(const char *path, uint16_t mode, const struct vfs_metadata *meta) {
    assert(!strcmp(path, "/system/music-install/song.tmp"));
    assert(mode == VFS_MODE_FILE && meta->perm == 0644 && !temporary);
    temporary = 1; length = 0; return VFS_OK;
}
int vfs_unlink(const char *path) {
    assert(!strcmp(path, "/system/music-install/song.tmp"));
    temporary = 0; length = 0; return VFS_OK;
}
struct file *vfs_open(const char *path, uint32_t flags) {
    assert(!strcmp(path, "/system/music-install/song.tmp") && temporary);
    handle = (struct file){.flags = flags}; return &handle;
}
int vfs_close(struct file *file) { assert(file == &handle); return VFS_OK; }
long vfs_write(struct file *file, const void *data, size_t size) {
    assert(file->flags == VFS_OPEN_WRITE && size <= 16384);
    if (fail_write) return -1;
    if (size > 7000) size = 7000; /* successful short writes */
    assert(length + size <= sizeof(stored));
    memcpy(stored + length, data, size); length += size; return (long)size;
}
long vfs_read(struct file *file, void *data, size_t size) {
    assert(file->flags == VFS_OPEN_READ && size <= 4096);
    if (size > 1000) size = 1000;
    if (size > length - file->position) size = length - file->position;
    memcpy(data, stored + file->position, size); file->position += (uint32_t)size;
    if (corrupt && size) ((uint8_t *)data)[0] ^= 1;
    return (long)size;
}
int vfs_rename(const char *from, const char *to) {
    assert(!strcmp(from, "/system/music-install/song.tmp") && !strcmp(to, "/Music/song.ogg"));
    assert(temporary && length == sizeof(source) && !memcmp(source, stored, length));
    temporary = 0; published = 1; ++renamed; return VFS_OK;
}
int main(void) {
    for (size_t i = 0; i < sizeof(source); ++i) source[i] = (uint8_t)i;
    struct builtin_music_track t = {"/Music/song.ogg", "/system/music-install/song.tmp", source, sizeof(source)};
    fail_write = 1;
    assert(builtin_music_seed(&t, 1) == -1 && !published && !temporary);
    fail_write = 0; corrupt = 1;
    assert(builtin_music_seed(&t, 1) == -1 && !published && !temporary);
    corrupt = 0; temporary = 1; length = 12; /* interrupted earlier boot */
    assert(builtin_music_seed(&t, 1) == 0 && published && renamed == 1);
    assert(builtin_music_seed(&t, 1) == 0 && renamed == 1);
    published = 0; existing = 1; length = 7;
    assert(builtin_music_seed(&t, 1) == 0 && length == 7 && renamed == 1);
    puts("[builtin-music] readback, short I/O, failure, retry and user-file preservation passed");
}
