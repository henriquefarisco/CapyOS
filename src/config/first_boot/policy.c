#include "config/first_boot_policy.h"

int first_boot_setup_required(int marker_exists, int has_users,
                              int config_exists) {
  return marker_exists && has_users && config_exists ? 0 : 1;
}
