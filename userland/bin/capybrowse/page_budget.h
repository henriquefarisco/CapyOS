/*
 * userland/bin/capybrowse/page_budget.h — per-page memory + time budget
 * (Etapa 7 / Slice 7.6: "limites de memória/tempo por página").
 *
 * A small, pure, fail-closed guard the browser runtime consults while loading a
 * page (top document + its sub-resources): it caps the TOTAL bytes admitted and
 * the wall-time spent, so a hostile or pathologically large/slow page cannot
 * exhaust memory or hang the UI. This is the resource backbone of streaming
 * render — the fetch/render loop checks the budget as bytes arrive and stops
 * fail-closed once a cap is crossed, rather than buffering unboundedly.
 *
 * Deterministic and freestanding: the clock is INJECTED (`now`, in the caller's
 * tick/second domain) and the budget is CALLER-OWNED (no globals), so it is
 * fully host-testable. Once a cap is crossed the budget is STICKY-exceeded (it
 * never silently recovers within the same page load).
 *
 * A cap of 0 means "unlimited" for that dimension.
 */
#ifndef CAPYOS_PAGE_BUDGET_H
#define CAPYOS_PAGE_BUDGET_H

#include <stddef.h>

struct page_budget {
  size_t max_bytes;  /* total admitted bytes cap (0 = unlimited) */
  long   max_ticks;  /* wall-time cap in injected ticks/seconds (0 = unlimited) */
  long   start;      /* `now` at init */
  size_t used_bytes; /* bytes admitted so far */
  int    exceeded;   /* sticky: set once any cap is crossed */
};

/* Start a page budget at `now` with the given caps (0 = unlimited). */
void page_budget_init(struct page_budget *b, size_t max_bytes, long max_ticks,
                      long now);

/* Admit `n` more bytes. Returns 1 if the page is still within budget, 0 if this
 * addition (or a prior one, or an overflow) crossed the byte cap — the budget
 * then stays exceeded. A NULL budget returns 0 (fail-closed). */
int page_budget_add_bytes(struct page_budget *b, size_t n);

/* Check the time budget at `now`. Returns 1 if still within the wall-time cap,
 * 0 if the cap is crossed (budget then stays exceeded) or `b` is NULL. */
int page_budget_check_time(struct page_budget *b, long now);

/* 1 if no cap has been crossed yet, 0 if exceeded or `b` is NULL. */
int page_budget_ok(const struct page_budget *b);

/* Bytes still admissible (max_bytes - used_bytes, clamped at 0). Returns
 * (size_t)-1 when the byte cap is unlimited, 0 when already exceeded/NULL. */
size_t page_budget_remaining_bytes(const struct page_budget *b);

#endif /* CAPYOS_PAGE_BUDGET_H */
