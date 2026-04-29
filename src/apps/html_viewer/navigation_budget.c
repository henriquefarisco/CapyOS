#include "internal/html_viewer_internal.h"

#include "kernel/log/klog.h"

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
}

int hv_resource_budget_take(struct html_viewer_app *app, const char *stage) {
  if (!app) return 0;
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
}

int hv_render_budget_take(struct html_viewer_app *app, const char *stage) {
  if (!app) return 0;
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
}

int hv_parse_budget_take(struct html_viewer_app *app, const char *stage) {
  if (!app) return 1; /* internal scaffolding pages do not enforce budget */
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
