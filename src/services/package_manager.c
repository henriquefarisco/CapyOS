#include "services/package_manager.h"
#include "net/http.h"
#include "security/ed25519.h"
#include "security/crypt.h"
#include "fs/vfs.h"
#include <stddef.h>

static struct package_db pkg_db;
static int pkg_initialized = 0;

static void pkg_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}
static void pkg_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = '\0';
}
static int pkg_streq(const char *a, const char *b) {
  while (*a && *b) { if (*a != *b) return 0; a++; b++; } return *a == *b;
}
static size_t pkg_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

void package_manager_init(void) {
  pkg_memset(&pkg_db, 0, sizeof(pkg_db));
  pkg_initialized = 1;
}

static int pkg_parse_line(const char *line, char *key, size_t kmax,
                           char *val, size_t vmax) {
  size_t i = 0;
  while (line[i] && line[i] != '=' && i < kmax - 1) { key[i] = line[i]; i++; }
  key[i] = '\0';
  if (line[i] != '=') return -1;
  i++;
  size_t j = 0;
  while (line[i] && line[i] != '\n' && line[i] != '\r' && j < vmax - 1) {
    val[j++] = line[i++];
  }
  val[j] = '\0';
  return 0;
}

int package_db_load(const char *path) {
  if (!pkg_initialized || !path) return -1;
  struct file *f = vfs_open(path, VFS_OPEN_READ);
  if (!f) return -1;

  char buf[4096];
  long rd = vfs_read(f, buf, sizeof(buf) - 1);
  vfs_close(f);
  if (rd <= 0) return -1;
  buf[rd] = '\0';

  struct package_info *current = NULL;
  const char *p = buf;
  while (*p) {
    char line[256];
    size_t li = 0;
    while (*p && *p != '\n' && li < 255) line[li++] = *p++;
    line[li] = '\0';
    if (*p == '\n') p++;

    if (li == 0 || line[0] == '#') continue;

    char key[64], val[192];
    if (pkg_parse_line(line, key, 64, val, 192) != 0) continue;

    if (pkg_streq(key, "name")) {
      if (pkg_db.installed_count < PKG_MAX_INSTALLED) {
        current = &pkg_db.installed[pkg_db.installed_count++];
        pkg_memset(current, 0, sizeof(*current));
        pkg_strcpy(current->name, val, PKG_NAME_MAX);
        current->state = PKG_STATE_INSTALLED;
      }
    } else if (current) {
      if (pkg_streq(key, "version")) pkg_strcpy(current->version, val, PKG_VERSION_MAX);
      else if (pkg_streq(key, "description")) pkg_strcpy(current->description, val, PKG_DESC_MAX);
      else if (pkg_streq(key, "path")) pkg_strcpy(current->path, val, PKG_PATH_MAX);
      else if (pkg_streq(key, "size")) {
        uint32_t sz = 0;
        for (int i = 0; val[i] >= '0' && val[i] <= '9'; i++) sz = sz * 10 + (uint32_t)(val[i] - '0');
        current->size_bytes = sz;
      }
    }
  }
  return 0;
}

int package_db_save(const char *path) {
  if (!pkg_initialized || !path) return -1;

  char buf[4096];
  size_t pos = 0;

  for (uint32_t i = 0; i < pkg_db.installed_count; i++) {
    struct package_info *p = &pkg_db.installed[i];
    if (p->state != PKG_STATE_INSTALLED) continue;

    const char *prefix;
    prefix = "name=";
    while (*prefix && pos < 4090) buf[pos++] = *prefix++;
    const char *v = p->name;
    while (*v && pos < 4090) buf[pos++] = *v++;
    buf[pos++] = '\n';

    prefix = "version=";
    while (*prefix && pos < 4090) buf[pos++] = *prefix++;
    v = p->version;
    while (*v && pos < 4090) buf[pos++] = *v++;
    buf[pos++] = '\n';

    prefix = "description=";
    while (*prefix && pos < 4090) buf[pos++] = *prefix++;
    v = p->description;
    while (*v && pos < 4090) buf[pos++] = *v++;
    buf[pos++] = '\n';

    buf[pos++] = '\n';
  }
  buf[pos] = '\0';

  struct vfs_metadata meta = {0, 0, 0x1FF};
  vfs_create(path, VFS_MODE_FILE, &meta);
  struct file *f = vfs_open(path, VFS_OPEN_WRITE);
  if (!f) return -1;
  vfs_write(f, buf, pos);
  vfs_close(f);
  return 0;
}

static struct package_info *pkg_find_installed(const char *name) {
  for (uint32_t i = 0; i < pkg_db.installed_count; i++) {
    if (pkg_streq(pkg_db.installed[i].name, name)) return &pkg_db.installed[i];
  }
  return NULL;
}

static struct package_info *pkg_find_available(const char *name) {
  for (uint32_t i = 0; i < pkg_db.available_count; i++) {
    if (pkg_streq(pkg_db.available[i].name, name)) return &pkg_db.available[i];
  }
  return NULL;
}

int package_install(const char *name) {
  if (!pkg_initialized || !name) return -1;
  if (pkg_find_installed(name)) return 0;

  struct package_info *avail = pkg_find_available(name);
  if (!avail) return -1;

  if (avail->dep_count > 0) {
    for (uint32_t d = 0; d < avail->dep_count; d++) {
      if (!pkg_find_installed(avail->deps[d])) {
        if (package_install(avail->deps[d]) != 0) return -1;
      }
    }
  }

  if (pkg_db.installed_count >= PKG_MAX_INSTALLED) return -1;
  struct package_info *inst = &pkg_db.installed[pkg_db.installed_count++];
  *inst = *avail;
  inst->state = PKG_STATE_INSTALLED;
  return 0;
}

int package_remove(const char *name) {
  if (!pkg_initialized || !name) return -1;
  for (uint32_t i = 0; i < pkg_db.installed_count; i++) {
    if (pkg_streq(pkg_db.installed[i].name, name)) {
      for (uint32_t j = i; j < pkg_db.installed_count - 1; j++)
        pkg_db.installed[j] = pkg_db.installed[j + 1];
      pkg_db.installed_count--;
      return 0;
    }
  }
  return -1;
}

int package_update(const char *name) {
  if (!pkg_initialized || !name) return -1;
  struct package_info *inst = pkg_find_installed(name);
  struct package_info *avail = pkg_find_available(name);
  if (!inst || !avail) return -1;
  pkg_strcpy(inst->version, avail->version, PKG_VERSION_MAX);
  pkg_strcpy(inst->description, avail->description, PKG_DESC_MAX);
  inst->size_bytes = avail->size_bytes;
  inst->checksum = avail->checksum;
  return 0;
}

int package_update_all(void) {
  int updated = 0;
  for (uint32_t i = 0; i < pkg_db.installed_count; i++) {
    struct package_info *avail = pkg_find_available(pkg_db.installed[i].name);
    if (avail && !pkg_streq(avail->version, pkg_db.installed[i].version)) {
      package_update(pkg_db.installed[i].name);
      updated++;
    }
  }
  return updated;
}

int package_list_installed(void (*print)(const char *)) {
  if (!print) return -1;
  for (uint32_t i = 0; i < pkg_db.installed_count; i++) {
    struct package_info *p = &pkg_db.installed[i];
    print(p->name); print(" "); print(p->version); print("\n");
  }
  return (int)pkg_db.installed_count;
}

int package_list_available(void (*print)(const char *)) {
  if (!print) return -1;
  for (uint32_t i = 0; i < pkg_db.available_count; i++) {
    struct package_info *p = &pkg_db.available[i];
    print(p->name); print(" "); print(p->version);
    if (pkg_find_installed(p->name)) print(" [installed]");
    print("\n");
  }
  return (int)pkg_db.available_count;
}

int package_search(const char *query, void (*print)(const char *)) {
  if (!query || !print) return -1;
  size_t qlen = pkg_strlen(query);
  int found = 0;
  for (uint32_t i = 0; i < pkg_db.available_count; i++) {
    struct package_info *p = &pkg_db.available[i];
    int match = 0;
    for (size_t j = 0; p->name[j] && !match; j++) {
      int m = 1;
      for (size_t k = 0; k < qlen && m; k++) {
        if (p->name[j + k] != query[k]) m = 0;
      }
      if (m) match = 1;
    }
    if (!match) {
      for (size_t j = 0; p->description[j] && !match; j++) {
        int m = 1;
        for (size_t k = 0; k < qlen && m; k++) {
          if (p->description[j + k] != query[k]) m = 0;
        }
        if (m) match = 1;
      }
    }
    if (match) {
      print(p->name); print(" - "); print(p->description); print("\n");
      found++;
    }
  }
  return found;
}

int package_info_get(const char *name, struct package_info *out) {
  if (!name || !out) return -1;
  struct package_info *p = pkg_find_installed(name);
  if (!p) p = pkg_find_available(name);
  if (!p) return -1;
  *out = *p;
  return 0;
}

int package_check_deps(const char *name) {
  struct package_info *p = pkg_find_available(name);
  if (!p) p = pkg_find_installed(name);
  if (!p) return -1;
  for (uint32_t i = 0; i < p->dep_count; i++) {
    if (!pkg_find_installed(p->deps[i])) return -1;
  }
  return 0;
}

void package_stats_get(struct package_stats *out) {
  if (!out) return;
  out->installed_count = pkg_db.installed_count;
  out->available_count = pkg_db.available_count;
  out->updates_available = 0;
  out->total_installed_bytes = 0;
  for (uint32_t i = 0; i < pkg_db.installed_count; i++) {
    out->total_installed_bytes += pkg_db.installed[i].size_bytes;
    struct package_info *avail = pkg_find_available(pkg_db.installed[i].name);
    if (avail && !pkg_streq(avail->version, pkg_db.installed[i].version))
      out->updates_available++;
  }
}

int package_refresh_catalog(void) {
  /* In a full implementation, this would HTTP GET the remote catalog
   * and populate pkg_db.available[]. For now, return success. */
  return 0;
}
