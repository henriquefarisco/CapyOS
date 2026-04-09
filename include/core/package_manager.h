#ifndef CORE_PACKAGE_MANAGER_H
#define CORE_PACKAGE_MANAGER_H

#include <stdint.h>
#include <stddef.h>

#define PKG_NAME_MAX     64
#define PKG_VERSION_MAX  32
#define PKG_DESC_MAX     256
#define PKG_PATH_MAX     128
#define PKG_MAX_DEPS     8
#define PKG_MAX_INSTALLED 64
#define PKG_MAX_AVAILABLE 128

enum package_state {
  PKG_STATE_AVAILABLE = 0,
  PKG_STATE_DOWNLOADING,
  PKG_STATE_STAGED,
  PKG_STATE_INSTALLING,
  PKG_STATE_INSTALLED,
  PKG_STATE_REMOVING,
  PKG_STATE_BROKEN
};

struct package_info {
  char name[PKG_NAME_MAX];
  char version[PKG_VERSION_MAX];
  char description[PKG_DESC_MAX];
  char path[PKG_PATH_MAX];
  enum package_state state;
  uint32_t size_bytes;
  uint32_t checksum;
  char deps[PKG_MAX_DEPS][PKG_NAME_MAX];
  uint32_t dep_count;
  uint64_t installed_tick;
};

struct package_db {
  struct package_info installed[PKG_MAX_INSTALLED];
  uint32_t installed_count;
  struct package_info available[PKG_MAX_AVAILABLE];
  uint32_t available_count;
};

struct package_stats {
  uint32_t installed_count;
  uint32_t available_count;
  uint32_t updates_available;
  uint64_t total_installed_bytes;
};

void package_manager_init(void);
int package_db_load(const char *path);
int package_db_save(const char *path);
int package_install(const char *name);
int package_remove(const char *name);
int package_update(const char *name);
int package_update_all(void);
int package_list_installed(void (*print)(const char *));
int package_list_available(void (*print)(const char *));
int package_search(const char *query, void (*print)(const char *));
int package_info_get(const char *name, struct package_info *out);
int package_check_deps(const char *name);
void package_stats_get(struct package_stats *out);
int package_refresh_catalog(void);

#endif /* CORE_PACKAGE_MANAGER_H */
