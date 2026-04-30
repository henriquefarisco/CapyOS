#include "internal/html_viewer_internal.h"

#include "kernel/log/klog.h"
#include "util/op_budget.h"

/* Navigation-level op_budget. Single source of truth for "is this navigation
 * still alive?". Per-phase budgets (parse/render/external fetch) feed it via
 * hv_nav_budget_propagate_exhaust(); external actors call hv_nav_budget_cancel.
 * Inner loops poll hv_nav_budget_blocked() to drop out cooperatively. */

void hv_nav_budget_reset(struct html_viewer_app *app) {
  if (!app) return;
  /* total = sum of declared per-phase caps so per-phase consumption never
   * spuriously exhausts the navigation-level budget on its own. The shared
   * budget is primarily a cancellation/propagation channel, not a hard
   * unit cap. */
  uint32_t total = (uint32_t)HV_PARSE_NODE_BUDGET +
                   (uint32_t)HV_RENDER_NODE_BUDGET +
                   (uint32_t)HV_EXTERNAL_FETCH_LIMIT;
  op_budget_init(&app->nav_op_budget, "html_viewer_nav", total);
}

int hv_nav_budget_blocked(const struct html_viewer_app *app) {
  if (!app) return 0;
  return op_budget_is_blocked(&app->nav_op_budget);
}

void hv_nav_budget_cancel(struct html_viewer_app *app, const char *reason) {
  if (!app) return;
  if (op_budget_is_blocked(&app->nav_op_budget)) {
    /* Already cancelled or exhausted: keep first reason for diagnostics. */
    return;
  }
  op_budget_cancel(&app->nav_op_budget,
                   reason && reason[0] ? reason : "html-viewer cancel");
}

void hv_nav_budget_propagate_exhaust(struct html_viewer_app *app,
                                     const char *reason) {
  if (!app) return;
  /* Idempotent: do not overwrite a prior cancel/exhaust reason. */
  if (op_budget_is_blocked(&app->nav_op_budget)) return;
  op_budget_exhaust(&app->nav_op_budget,
                    reason && reason[0] ? reason
                                        : "html-viewer phase exhausted");
}

const char *hv_nav_budget_reason(const struct html_viewer_app *app) {
  if (!app) return "";
  return op_budget_reason(&app->nav_op_budget);
}

void hv_resource_budget_reset(struct html_viewer_app *app) {
  if (!app) return;
  app->external_fetch_attempts = 0;
  app->resource_budget_exhausted = 0;
}

void hv_resource_budget_mark_exhausted(struct html_viewer_app *app,
                                       const char *stage) {
  if (!app) return;
  app->safe_mode = 1;
  app->resource_budget_exhausted = 1;
  html_viewer_set_error_context(app, stage && stage[0] ? stage : "resource",
                                "External resource budget exhausted.");
  klog(KLOG_WARN, "[browser] external resource budget exhausted");
  /* Forward to the navigation-level op_budget so any other phase still
   * looping (parse, render) can observe the exhaustion immediately. */
  hv_nav_budget_propagate_exhaust(app, "external-resource");
}

int hv_resource_budget_take(struct html_viewer_app *app, const char *stage) {
  if (!app) return 0;
  /* Check the unified cancel/abort flag first so a cancelled navigation
   * stops issuing fetches even before its own budget would trip. */
  if (op_budget_is_blocked(&app->nav_op_budget)) {
    return 0;
  }
  if (app->external_fetch_attempts >= HV_EXTERNAL_FETCH_LIMIT) {
    hv_resource_budget_mark_exhausted(app, stage);
    return 0;
  }
  app->external_fetch_attempts++;
  return 1;
}

void hv_render_budget_reset(struct html_viewer_app *app) {
  if (!app) return;
  app->render_nodes_visited = 0;
  app->render_budget_exhausted = 0;
}

void hv_render_budget_begin_frame(struct html_viewer_app *app) {
  if (!app) return;
  app->render_nodes_visited = 0;
}

static void hv_render_budget_mark_exhausted(struct html_viewer_app *app,
                                            const char *stage) {
  if (!app) return;
  app->safe_mode = 1;
  if (!app->render_budget_exhausted) {
    klog(KLOG_WARN, "[browser] render node budget exhausted");
  }
  app->render_budget_exhausted = 1;
  html_viewer_set_error_context(app, stage && stage[0] ? stage : "paint",
                                "Render node budget exhausted.");
  hv_nav_budget_propagate_exhaust(app, "render-node");
}

int hv_render_budget_take(struct html_viewer_app *app, const char *stage) {
  if (!app) return 0;
  if (op_budget_is_blocked(&app->nav_op_budget)) {
    return 0;
  }
  if (app->render_nodes_visited >= HV_RENDER_NODE_BUDGET) {
    hv_render_budget_mark_exhausted(app, stage);
    return 0;
  }
  app->render_nodes_visited++;
  return 1;
}

/* Parse-phase budget. Bounds the number of HTML nodes the parser is allowed
 * to allocate per navigation. The hard cap (HTML_MAX_NODES) is still
 * enforced at the array level; this budget kicks in earlier and gives the
 * parser a chance to enter safe_mode before the document is truncated by
 * the array boundary, which would otherwise show no diagnostic. */

void hv_parse_budget_reset(struct html_viewer_app *app) {
  if (!app) return;
  app->parse_nodes_visited = 0;
  app->parse_budget_exhausted = 0;
}

void hv_parse_budget_mark_exhausted(struct html_viewer_app *app,
                                    const char *stage) {
  if (!app) return;
  app->safe_mode = 1;
  if (!app->parse_budget_exhausted) {
    klog(KLOG_WARN, "[browser] parse node budget exhausted");
  }
  app->parse_budget_exhausted = 1;
  html_viewer_set_error_context(app, stage && stage[0] ? stage : "parse",
                                "Parse node budget exhausted.");
  hv_nav_budget_propagate_exhaust(app, "parse-node");
}

int hv_parse_budget_take(struct html_viewer_app *app, const char *stage) {
  if (!app) return 1; /* internal scaffolding pages do not enforce budget */
  /* Cooperative cancellation point: if the navigation has been cancelled
   * (Esc, supervisor) the parser bails out cleanly rather than building a
   * full DOM that would just be discarded. */
  if (op_budget_is_blocked(&app->nav_op_budget)) {
    return 0;
  }
  if (app->parse_nodes_visited >= HV_PARSE_NODE_BUDGET) {
    hv_parse_budget_mark_exhausted(app, stage);
    return 0;
  }
  app->parse_nodes_visited++;
  return 1;
}

/* Single-threaded current-parse pointer, mirroring the existing
 * g_hv_parse_lock pattern. The parser entry sets this so html_push_node can
 * consult the budget without changing every existing call signature. */
static struct html_viewer_app *g_hv_parse_app = NULL;

void hv_parse_app_set(struct html_viewer_app *app) { g_hv_parse_app = app; }

struct html_viewer_app *hv_parse_app_get(void) { return g_hv_parse_app; }
