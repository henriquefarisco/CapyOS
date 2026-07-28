/*
 * src/boot/boot_slot_lifecycle.c
 *
 * A/B lifecycle transitions of the boot-slot manager: arming a staged slot,
 * activation, boot selection (which spends the single attempt or applies the
 * rollback), health confirmation and explicit rollback.
 *
 * Carved out of `src/boot/boot_slot.c` so both translation units stay under
 * the 900-line layout limit. Every function here mutates the manager only
 * through `bs_commit_locked`, so a transition is either durably committed and
 * mirrored or reported as indeterminate; callers reach them through the public
 * wrappers in `boot_slot_operations.c` and `boot_slot_store.c`.
 */
#include "internal/boot_slot_internal.h"

#include <stddef.h>
int boot_slot_internal_arm_provider_locked(uint32_t slot,
                                           uint64_t expected_generation,
                                           uint64_t *out_generation) {
  struct boot_slot_manager candidate;
  uint32_t previous;
  if (out_generation)
    *out_generation = 0u;
  if (!out_generation || !boot_slot_internal_persistence_ready_locked() ||
      slot >= BOOT_SLOT_COUNT || expected_generation != bsp.generation ||
      bsm.pending_slot != BOOT_SLOT_NONE || slot == bsm.confirmed_slot ||
      bsm.slots[slot].state != BOOT_SLOT_VALID ||
      bsm.slots[bsm.confirmed_slot].state != BOOT_SLOT_ACTIVE ||
      !bsm.slots[bsm.confirmed_slot].health_confirmed)
    return BOOT_SLOT_ERR_IO;
  candidate = bsm;
  previous = candidate.confirmed_slot;
  candidate.slots[previous].state = BOOT_SLOT_ROLLBACK;
  candidate.slots[slot].state = BOOT_SLOT_ACTIVE;
  candidate.slots[slot].health_confirmed = 0;
  candidate.active_slot = slot;
  candidate.next_slot = slot;
  candidate.rollback_pending = 1;
  candidate.pending_slot = slot;
  candidate.tries_remaining = BOOT_SLOT_DEFAULT_TRIES;
  if (bs_commit_locked(&candidate) != 0)
    return bsp.ready ? BOOT_SLOT_ERR_IO : BOOT_SLOT_ERR_COMMIT_UNKNOWN;
  *out_generation = bsp.generation;
  return 0;
}

int boot_slot_internal_activate_locked(uint32_t slot) {
  struct boot_slot_manager candidate;
  struct boot_slot *target = NULL;
  uint32_t previous = 0u;
  if (!boot_slot_internal_transition_ready_locked() ||
      slot >= BOOT_SLOT_COUNT)
    return BOOT_SLOT_ERR_IO;
  target = &bsm.slots[slot];
  if (target->state != BOOT_SLOT_VALID && target->state != BOOT_SLOT_ACTIVE)
    return BOOT_SLOT_ERR_IO;
  if (bsp.configured) {
    if (!boot_slot_internal_persistence_ready_locked() ||
        bsm.pending_slot != BOOT_SLOT_NONE || slot == bsm.confirmed_slot ||
        bsm.slots[bsm.confirmed_slot].state != BOOT_SLOT_ACTIVE ||
        !bsm.slots[bsm.confirmed_slot].health_confirmed)
      return BOOT_SLOT_ERR_IO;
    candidate = bsm;
    previous = candidate.confirmed_slot;
    candidate.slots[previous].state = BOOT_SLOT_ROLLBACK;
    target = &candidate.slots[slot];
    target->state = BOOT_SLOT_ACTIVE;
    target->health_confirmed = 0;
    candidate.active_slot = slot;
    candidate.next_slot = slot;
    candidate.rollback_pending = 1;
    candidate.pending_slot = slot;
    candidate.tries_remaining = BOOT_SLOT_DEFAULT_TRIES;
    return bs_commit_locked(&candidate);
  }
  candidate = bsm;
  previous = candidate.active_slot;
  if (candidate.rollback_pending)
    return BOOT_SLOT_ERR_IO;
  if (previous != slot) {
    if (candidate.slots[previous].state != BOOT_SLOT_ACTIVE ||
        !candidate.slots[previous].version[0] ||
        !candidate.slots[previous].health_confirmed)
      return BOOT_SLOT_ERR_IO;
    candidate.confirmed_slot = previous;
  } else {
    uint32_t fallback = slot ^ 1u;
    if (candidate.slots[fallback].state == BOOT_SLOT_VALID &&
        candidate.slots[fallback].version[0]) {
      candidate.confirmed_slot = fallback;
      previous = fallback;
    } else {
      target = &candidate.slots[slot];
      target->state = BOOT_SLOT_ACTIVE;
      target->health_confirmed = 1;
      target->boot_count++;
      candidate.confirmed_slot = slot;
      candidate.pending_slot = BOOT_SLOT_NONE;
      candidate.tries_remaining = 0u;
      candidate.active_slot = slot;
      candidate.next_slot = slot;
      candidate.rollback_pending = 0;
      return bs_commit_locked(&candidate);
    }
  }
  candidate.slots[previous].state = BOOT_SLOT_ROLLBACK;
  candidate.slots[previous].health_confirmed = 1;
  target = &candidate.slots[slot];
  target->state = BOOT_SLOT_ACTIVE;
  target->health_confirmed = 0;
  target->boot_count++;
  candidate.active_slot = slot;
  candidate.next_slot = slot;
  candidate.pending_slot = slot;
  candidate.tries_remaining = 0u;
  candidate.rollback_pending = 1;
  return bs_commit_locked(&candidate);
}
static int bs_apply_rollback(struct boot_slot_manager *candidate) {
  struct boot_slot *failed = NULL;
  struct boot_slot *fallback = NULL;
  uint32_t pending = 0u;
  uint32_t confirmed = 0u;
  if (!candidate || candidate->pending_slot == BOOT_SLOT_NONE ||
      candidate->pending_slot >= BOOT_SLOT_COUNT ||
      candidate->confirmed_slot >= BOOT_SLOT_COUNT)
    return BOOT_SLOT_ERR_IO;
  pending = candidate->pending_slot;
  confirmed = candidate->confirmed_slot;
  failed = &candidate->slots[pending];
  fallback = &candidate->slots[confirmed];
  failed->state = BOOT_SLOT_FAILED;
  failed->health_confirmed = 0;
  failed->fail_count++;
  fallback->state = BOOT_SLOT_ACTIVE;
  fallback->health_confirmed = 1;
  candidate->active_slot = confirmed;
  candidate->next_slot = confirmed;
  candidate->rollback_pending = 0;
  candidate->pending_slot = BOOT_SLOT_NONE;
  candidate->tries_remaining = 0u;
  return 0;
}
int boot_slot_internal_select_locked(uint32_t *out_slot,
                                     uint64_t *out_generation) {
  struct boot_slot_manager candidate;
  uint32_t selected = 0u;
  int rc = 0;
  if (out_slot)
    *out_slot = BOOT_SLOT_NONE;
  if (out_generation)
    *out_generation = 0u;
  if (!out_slot || !out_generation || !boot_slot_internal_persistence_ready_locked())
    return BOOT_SLOT_ERR_IO;
  if (bsm.pending_slot == BOOT_SLOT_NONE) {
    *out_slot = bsm.confirmed_slot;
    *out_generation = bsp.generation;
    return 0;
  }
  candidate = bsm;
  if (candidate.tries_remaining > 0u) {
    selected = candidate.pending_slot;
    candidate.tries_remaining--;
    candidate.slots[selected].boot_count++;
    rc = bs_commit_locked(&candidate);
    if (rc != 0)
      return rc;
    *out_slot = selected;
    *out_generation = bsp.generation;
    return 1;
  }
  selected = candidate.confirmed_slot;
  if (bs_apply_rollback(&candidate) != 0)
    return BOOT_SLOT_ERR_IO;
  rc = bs_commit_locked(&candidate);
  if (rc != 0)
    return rc;
  *out_slot = selected;
  *out_generation = bsp.generation;
  return 2;
}
#ifdef UNIT_TEST
int boot_slot_internal_confirm_health_locked(void) {
  struct boot_slot_manager candidate;
  struct boot_slot *active = NULL;
  uint32_t other = 0u;
  if (!bsm_initialized || bsm.active_slot >= BOOT_SLOT_COUNT || bsp.configured)
    return BOOT_SLOT_ERR_IO;
  active = &bsm.slots[bsm.active_slot];
  if (active->state != BOOT_SLOT_ACTIVE)
    return -1;
  if (!bsm.rollback_pending && active->health_confirmed)
    return 0;
  candidate = bsm;
  active = &candidate.slots[candidate.active_slot];
  active->health_confirmed = 1;
  active->success_count++;
  other = candidate.active_slot ^ 1u;
  if (candidate.slots[other].state == BOOT_SLOT_ROLLBACK)
    candidate.slots[other].state = BOOT_SLOT_VALID;
  candidate.confirmed_slot = candidate.active_slot;
  candidate.pending_slot = BOOT_SLOT_NONE;
  candidate.tries_remaining = 0u;
  candidate.next_slot = candidate.active_slot;
  candidate.rollback_pending = 0;
  return bs_commit_locked(&candidate);
}
#endif
int boot_slot_internal_confirm_verified_locked(
    uint32_t slot, uint64_t generation, uint64_t authority_epoch,
    uint64_t lease_epoch) {
  struct boot_slot_manager candidate;
  uint32_t previous = 0u;
  if (!boot_slot_internal_persistence_ready_locked() || slot >= BOOT_SLOT_COUNT ||
      bsm.pending_slot != slot || bsm.active_slot != slot ||
      generation != bsp.generation || authority_epoch != bsp.authority_epoch ||
      lease_epoch != bsp.lease_epoch || bsm.tries_remaining != 0u)
    return BOOT_SLOT_ERR_IO;
  candidate = bsm;
  previous = candidate.confirmed_slot;
  candidate.slots[slot].health_confirmed = 1;
  candidate.slots[slot].success_count++;
  if (candidate.slots[previous].state == BOOT_SLOT_ROLLBACK)
    candidate.slots[previous].state = BOOT_SLOT_VALID;
  candidate.confirmed_slot = slot;
  candidate.pending_slot = BOOT_SLOT_NONE;
  candidate.tries_remaining = 0u;
  candidate.active_slot = slot;
  candidate.next_slot = slot;
  candidate.rollback_pending = 0;
  return bs_commit_locked(&candidate);
}
int boot_slot_internal_rollback_locked(void) {
  struct boot_slot_manager candidate;
  struct boot_slot *failed = NULL;
  struct boot_slot *fallback = NULL;
  uint32_t other = 0u;
  if (!boot_slot_internal_transition_ready_locked() || !bsm.rollback_pending)
    return BOOT_SLOT_ERR_IO;
  candidate = bsm;
  if (bsp.configured) {
    if (!boot_slot_internal_persistence_ready_locked() || bs_apply_rollback(&candidate) != 0)
      return BOOT_SLOT_ERR_IO;
    return bs_commit_locked(&candidate);
  }
  other = bsm.active_slot ^ 1u;
  if (bsm.slots[other].state != BOOT_SLOT_ROLLBACK &&
      bsm.slots[other].state != BOOT_SLOT_VALID)
    return BOOT_SLOT_ERR_IO;
  failed = &candidate.slots[candidate.active_slot];
  fallback = &candidate.slots[other];
  failed->state = BOOT_SLOT_FAILED;
  failed->health_confirmed = 0;
  failed->fail_count++;
  fallback->state = BOOT_SLOT_ACTIVE;
  fallback->health_confirmed = 1;
  candidate.active_slot = other;
  candidate.next_slot = other;
  candidate.confirmed_slot = other;
  candidate.pending_slot = BOOT_SLOT_NONE;
  candidate.tries_remaining = 0u;
  candidate.rollback_pending = 0;
  return bs_commit_locked(&candidate);
}
