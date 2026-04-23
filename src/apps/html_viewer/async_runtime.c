#include "internal/html_viewer_internal.h"

#ifndef UNIT_TEST
static int html_viewer_async_supported(void) {
  return scheduler_running();
}

static int html_viewer_worker_ensure(void) {
  if (!html_viewer_async_supported()) return -1;
  if (!g_hv_browser_worker_bootstrapped) {
    worker_system_init();
    g_hv_browser_worker_bootstrapped = 1;
  }
  if (!g_hv_browser_job.staged_app) {
    g_hv_browser_job.staged_app =
        (struct html_viewer_app *)kmalloc(sizeof(struct html_viewer_app));
    if (!g_hv_browser_job.staged_app) return -1;
  }
  if (g_hv_browser_worker_pool < 0) {
    g_hv_browser_worker_pool = worker_pool_create("app-bg", 1);
  }
  return g_hv_browser_worker_pool >= 0 ? 0 : -1;
}

static void html_viewer_background_invalidate(void) {
  if (g_viewer.window) compositor_invalidate(g_viewer.window->id);
}

static void html_viewer_background_worker(void *arg) {
  struct hv_browser_job *job = (struct hv_browser_job *)arg;
  if (!job || !job->staged_app) return;
  job->state = HV_BROWSER_RUNNING;
  hv_doc_reset(&job->staged_app->doc);
  job->staged_app->window = NULL;
  job->staged_app->scroll_offset = 0;
  job->staged_app->loading = 0;
  job->staged_app->url_editing = 0;
  job->staged_app->url_cursor = 0;
  job->staged_app->content_height = 0;
  job->staged_app->focused_node_index = -1;
  job->staged_app->url_searching = 0;
  job->staged_app->search_query[0] = '\0';
  job->staged_app->search_cursor = 0;
  job->staged_app->background_mode = 1;
  job->staged_app->active_navigation_id = job->ticket;
  job->staged_app->navigation_id = job->ticket;
  html_viewer_request_sync(job->staged_app, job->url, job->method,
                           job->body_len > 0 ? job->body : NULL,
                           job->body_len, 0, job->ticket);
  job->staged_app->background_mode = 0;
  job->state = HV_BROWSER_READY;
  html_viewer_background_invalidate();
}

void html_viewer_background_cancel(void) {
  g_hv_browser_active_ticket = ++g_hv_browser_ticket;
  g_hv_browser_followup_pending = 0;
  g_hv_browser_followup_url[0] = '\0';
  g_hv_browser_followup_body_len = 0;
  if (g_hv_browser_job.state == HV_BROWSER_READY && g_hv_browser_job.staged_app) {
    hv_doc_reset(&g_hv_browser_job.staged_app->doc);
    g_hv_browser_job.state = HV_BROWSER_IDLE;
  }
}

int html_viewer_queue_async_request(struct html_viewer_app *app,
                                    const char *url,
                                    enum http_method method,
                                    const uint8_t *body,
                                    size_t body_len) {
  uint32_t nav_ticket;
  if (!app || !url || html_viewer_worker_ensure() != 0) return -1;

  nav_ticket = app->active_navigation_id ? app->active_navigation_id
                                         : ++g_hv_browser_ticket;
  g_hv_browser_active_ticket = nav_ticket;
  if (g_hv_browser_job.state == HV_BROWSER_QUEUED ||
      g_hv_browser_job.state == HV_BROWSER_RUNNING) {
    g_hv_browser_followup_pending = 1;
    g_hv_browser_followup_method = method;
    kstrcpy(g_hv_browser_followup_url, sizeof(g_hv_browser_followup_url), url);
    g_hv_browser_followup_body_len = body_len < sizeof(g_hv_browser_followup_body)
                                         ? body_len
                                         : sizeof(g_hv_browser_followup_body);
    if (g_hv_browser_followup_body_len > 0 && body) {
      kmemcpy(g_hv_browser_followup_body, body, g_hv_browser_followup_body_len);
    }
    html_viewer_set_state(app, HTML_VIEWER_NAV_LOADING, "loading");
    html_viewer_load_loading_stub(app, url);
    html_viewer_background_invalidate();
    return 0;
  }

  hv_doc_reset(&g_hv_browser_job.staged_app->doc);
  kmemzero(g_hv_browser_job.staged_app, sizeof(*g_hv_browser_job.staged_app));
  g_hv_browser_job.staged_app->cookie_count = app->cookie_count;
  kmemcpy(g_hv_browser_job.staged_app->cookies, app->cookies,
          sizeof(app->cookies));
  g_hv_browser_job.staged_app->focused_node_index = -1;
  g_hv_browser_job.ticket = nav_ticket;
  g_hv_browser_job.method = method;
  kstrcpy(g_hv_browser_job.url, sizeof(g_hv_browser_job.url), url);
  g_hv_browser_job.body_len =
      body_len < sizeof(g_hv_browser_job.body) ? body_len
                                               : sizeof(g_hv_browser_job.body);
  if (g_hv_browser_job.body_len > 0 && body) {
    kmemcpy(g_hv_browser_job.body, body, g_hv_browser_job.body_len);
  }
  g_hv_browser_job.state = HV_BROWSER_QUEUED;
  if (worker_pool_submit((uint32_t)g_hv_browser_worker_pool,
                         html_viewer_background_worker,
                         &g_hv_browser_job) != 0) {
    g_hv_browser_job.state = HV_BROWSER_IDLE;
    return -1;
  }
  html_viewer_set_state(app, HTML_VIEWER_NAV_LOADING, "loading");
  app->scroll_offset = 0;
  app->focused_node_index = -1;
  kstrcpy(app->url, sizeof(app->url), url);
  kstrcpy(app->final_url, sizeof(app->final_url), url);
  html_viewer_load_loading_stub(app, url);
  html_viewer_background_invalidate();
  return 0;
}

void html_viewer_poll_background(struct html_viewer_app *app) {
  if (!app) return;
  if (g_hv_browser_job.state == HV_BROWSER_READY && g_hv_browser_job.staged_app) {
    uint32_t ticket = g_hv_browser_job.ticket;
    if (ticket == g_hv_browser_active_ticket && g_viewer.window == app->window) {
      hv_doc_reset(&app->doc);
      kmemcpy(&app->doc, &g_hv_browser_job.staged_app->doc, sizeof(app->doc));
      kmemzero(&g_hv_browser_job.staged_app->doc,
               sizeof(g_hv_browser_job.staged_app->doc));
      kstrcpy(app->url, sizeof(app->url), g_hv_browser_job.staged_app->url);
      kstrcpy(app->final_url, sizeof(app->final_url),
              g_hv_browser_job.staged_app->final_url);
      app->cookie_count = g_hv_browser_job.staged_app->cookie_count;
      kmemcpy(app->cookies, g_hv_browser_job.staged_app->cookies,
              sizeof(app->cookies));
      app->redirect_count = g_hv_browser_job.staged_app->redirect_count;
      app->safe_mode = g_hv_browser_job.staged_app->safe_mode;
      app->external_css_loaded = g_hv_browser_job.staged_app->external_css_loaded;
      app->external_images_loaded =
          g_hv_browser_job.staged_app->external_images_loaded;
      app->navigation_id = g_hv_browser_job.staged_app->navigation_id;
      app->active_navigation_id = g_hv_browser_job.staged_app->active_navigation_id;
      app->nav_state = g_hv_browser_job.staged_app->nav_state;
      kstrcpy(app->last_stage, sizeof(app->last_stage),
              g_hv_browser_job.staged_app->last_stage);
      kstrcpy(app->last_error_reason, sizeof(app->last_error_reason),
              g_hv_browser_job.staged_app->last_error_reason);
      if (app->doc.title[0] && app->window) {
        compositor_set_title(app->window->id, app->doc.title);
      } else if (app->window) {
        compositor_set_title(app->window->id, app->url);
      }
      hv_history_push(app->url);
      if (app->nav_state != HTML_VIEWER_NAV_FAILED &&
          app->nav_state != HTML_VIEWER_NAV_CANCELLED) {
        html_viewer_set_state(app, HTML_VIEWER_NAV_READY, "ready");
      }
      if (app->window) compositor_invalidate(app->window->id);
    }
    g_hv_browser_job.state = HV_BROWSER_IDLE;
  }

  if (g_hv_browser_job.state == HV_BROWSER_IDLE && g_hv_browser_followup_pending) {
    enum http_method method = g_hv_browser_followup_method;
    char url[HTML_URL_MAX];
    uint8_t body_copy[HTML_URL_MAX];
    size_t body_len = g_hv_browser_followup_body_len;
    g_hv_browser_followup_pending = 0;
    kstrcpy(url, sizeof(url), g_hv_browser_followup_url);
    if (body_len > sizeof(body_copy)) body_len = sizeof(body_copy);
    if (body_len > 0) kmemcpy(body_copy, g_hv_browser_followup_body, body_len);
    (void)html_viewer_queue_async_request(app, url, method,
                                          body_len > 0 ? body_copy : NULL,
                                          body_len);
  }
}
#else
void html_viewer_background_cancel(void) {}

void html_viewer_poll_background(struct html_viewer_app *app) {
  (void)app;
}

int html_viewer_queue_async_request(struct html_viewer_app *app,
                                    const char *url,
                                    enum http_method method,
                                    const uint8_t *body,
                                    size_t body_len) {
  (void)app;
  (void)url;
  (void)method;
  (void)body;
  (void)body_len;
  return -1;
}
#endif
