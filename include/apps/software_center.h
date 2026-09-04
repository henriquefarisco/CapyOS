#ifndef APPS_SOFTWARE_CENTER_H
#define APPS_SOFTWARE_CENTER_H

#include <stddef.h>

#define SOFTWARE_CENTER_PACKAGE_MAX 16u

struct software_center_package {
  char name[64];
  char version[32];
  char summary[96];
  int installed;
};

struct software_center_backend {
  int (*refresh)(void);
  size_t (*count)(void);
  int (*get)(size_t index, struct software_center_package *out);
  int (*install)(const char *name);
  int (*remove)(const char *name);
};

void software_center_set_backend(const struct software_center_backend *backend);
void software_center_open(void);
int software_center_smoke_roundtrip(void);

#endif
