#ifndef SERVICES_UPDATE_AGENT_INTERNAL_H
#define SERVICES_UPDATE_AGENT_INTERNAL_H

/* Internal boundary between the update_agent translation units.
 *
 * The agent is split across two .c files to keep each one under the
 * monolith threshold while preserving a single coherent state machine:
 *
 *   - src/services/update_agent.c
 *       Catalog/manifest parsing, repository configuration, state file
 *       persistence, status accessors, stage/clear-stage/arm flow.
 *
 *   - src/services/update_agent_transact.c
 *       Boot-slot integration: apply_boot_slot, confirm_health,
 *       check_rollback, and the M6.4 payload sha256 verification path
 *       (apply_boot_slot_verified + helpers).
 *
 * Both TUs share the singleton runtime status struct and the small string
 * helper used to write to its char fields. The shared symbols live here
 * with the `update_agent_` prefix so this header acts as a private API
 * boundary; nothing outside src/services/ should ever include it. */

#include "services/update_agent.h"

#include <stddef.h>
#include <stdint.h>

extern struct system_update_status update_agent_g_status;

void update_agent_local_copy(char *dst, size_t dst_size, const char *src);

#endif /* SERVICES_UPDATE_AGENT_INTERNAL_H */
