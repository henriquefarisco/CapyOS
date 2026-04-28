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
