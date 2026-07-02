/*
 * userland/bin/capybrowse/page_budget.c — see page_budget.h.
 *
 * Pure, freestanding, fail-closed per-page memory/time budget. No allocation,
 * no globals, clock injected.
 */

#include "page_budget.h"

void page_budget_init(struct page_budget *b, size_t max_bytes, long max_ticks,
                      long now) {
  if (!b) return;
  b->max_bytes = max_bytes;
  b->max_ticks = max_ticks;
  b->start = now;
  b->used_bytes = 0;
  b->exceeded = 0;
}

int page_budget_add_bytes(struct page_budget *b, size_t n) {
  if (!b) return 0;
  if (b->exceeded) return 0;
  /* Overflow-safe accumulate: a wrap is treated as exceeding the budget. */
  if (b->used_bytes > (size_t)-1 - n) {
    b->exceeded = 1;
    return 0;
  }
  b->used_bytes += n;
  if (b->max_bytes != 0 && b->used_bytes > b->max_bytes) {
    b->exceeded = 1;
    return 0;
  }
  return 1;
}

int page_budget_check_time(struct page_budget *b, long now) {
  if (!b) return 0;
  if (b->exceeded) return 0;
  if (b->max_ticks != 0 && (now - b->start) > b->max_ticks) {
    b->exceeded = 1;
    return 0;
  }
  return 1;
}

int page_budget_ok(const struct page_budget *b) {
  if (!b) return 0;
  return b->exceeded ? 0 : 1;
}

size_t page_budget_remaining_bytes(const struct page_budget *b) {
  if (!b || b->exceeded) return 0;
  if (b->max_bytes == 0) return (size_t)-1; /* unlimited */
  if (b->used_bytes >= b->max_bytes) return 0;
  return b->max_bytes - b->used_bytes;
}
