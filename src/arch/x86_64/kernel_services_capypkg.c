/* kernel_services_capypkg.c — CapyPKG service runtime adapters + bootstrap.
 *
 * Split from kernel_services.c to keep each TU ≤ 900 lines.
 * Owns:
 *   - VFS-backed reader/writer/remover/mkdir adapters for the in-tree
 *     `capypkg` service.
 *   - HTTPS adapters bound to net/http for text + bytes fetchers.
 *   - kernel_capypkg_bind_runtime_adapters() — idempotent late binding.
 *   - kernel_update_capypkg_service_status() — service_manager bridge.
 *   - Auto-bootstrap orchestration (kernel_capypkg_maybe_bootstrap).
 *   - kernel_service_{poll,start,stop}_capypkg() — service hooks.
 */
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_main_internal.h"
#include "auth/session.h"
#include "fs/vfs.h"
#include "kernel/log/klog.h"
#include "net/http.h"
#include "net/stack.h"
#include "services/capypkg.h"
#include "services/capypkg_local_bundle.h"
#include "services/capypkg_bootstrap.h"
#include "services/capypkg_runtime.h"
#include "services/service_manager.h"

extern uint64_t pit_ticks(void);

/* VFS-backed adapters; mirror update_agent's pattern but kept local
 * because the function signatures are slightly different. */
static int capypkg_runtime_read_file(const char *path, char *buffer,
                                     size_t buffer_size, size_t *out_len) {
  struct file *file = NULL;
  struct session_context *previous_session = NULL;
  long read = 0;
  if (!path || !buffer || buffer_size < 2u) {
    return -1;
  }
  previous_session = session_active();
  session_set_active(NULL);
  file = vfs_open(path, VFS_OPEN_READ);
  if (!file) {
    session_set_active(previous_session);
    return -1;
  }
  read = vfs_read(file, buffer, buffer_size - 1u);
  vfs_close(file);
  if (read < 0) {
    session_set_active(previous_session);
    return -1;
  }
  buffer[(size_t)read] = '\0';
  if (out_len) {
    *out_len = (size_t)read;
  }
  session_set_active(previous_session);
  return 0;
}

static int capypkg_runtime_write_text(const char *path, const char *text) {
  struct session_context *previous_session = session_active();
  session_set_active(NULL);
  int rc = kernel_write_text_file(path, text);
  session_set_active(previous_session);
  return rc;
}

static int capypkg_runtime_write_bytes(const char *path, const uint8_t *data,
                                       size_t len) {
  struct file *file = NULL;
  struct dentry *d = NULL;
  struct session_context *previous_session = NULL;
  int rc = 0;
  if (!path || (!data && len > 0u)) {
    return -1;
  }
  previous_session = session_active();
  session_set_active(NULL);
  if (vfs_lookup(path, &d) == 0) {
    if (d && d->refcount) {
      d->refcount--;
    }
    d = NULL;
    (void)vfs_unlink(path);
  }
  if (vfs_create(path, VFS_MODE_FILE, NULL) != 0 &&
      vfs_lookup(path, &d) != 0) {
    rc = -1;
    goto done;
  }
  if (d && d->refcount) {
    d->refcount--;
  }
  file = vfs_open(path, VFS_OPEN_WRITE);
  if (!file) {
    rc = -1;
    goto done;
  }
  if (len > 0u && vfs_write(file, data, len) != (long)len) {
    vfs_close(file);
    rc = -1;
    goto done;
  }
  vfs_close(file);
done:
  session_set_active(previous_session);
  return rc;
}

static int capypkg_runtime_remove(const char *path) {
  struct dentry *d = NULL;
  struct session_context *previous_session = NULL;
  int rc = 0;
  if (!path) {
    return -1;
  }
  previous_session = session_active();
  session_set_active(NULL);
  if (vfs_lookup(path, &d) != 0) {
    goto done;
  }
  if (d && d->refcount) {
    d->refcount--;
  }
  rc = vfs_unlink(path);
done:
  session_set_active(previous_session);
  return rc;
}

static int capypkg_runtime_mkdir(const char *path) {
  struct session_context *previous_session = session_active();
  session_set_active(NULL);
  int rc = kernel_ensure_directory_recursive(path);
  session_set_active(previous_session);
  return rc;
}

/* HTTPS adapter binding.
 *
 * Both fetchers emit a single, structured klog line on failure so the
 * caller (the first-boot wizard) and post-mortem audit can tell *why*
 * the fetch failed (DNS, TLS, connect, HTTP status, body empty, etc.)
 * without having to wire a new error channel through the in-tree
 * capypkg adapter ABI. The functions still return 0/-1 to keep the
 * existing contract intact.
 *
 * URLs are clipped in the log line to avoid swamping the kernel log
 * with a single message; the wizard prints the full URL on the
 * framebuffer alongside this entry, which is enough context for an
 * operator to reproduce the request from a shell. */

/* Local string helpers, freestanding (no libc, no heap). */
static size_t capypkg_log_strlen(const char *s) {
  size_t n = 0u;
  if (!s) return 0u;
  while (s[n]) ++n;
  return n;
}

static void capypkg_log_append(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0u || !src) return;
  size_t cur = capypkg_log_strlen(dst);
  while (*src && cur + 1u < dst_size) {
    dst[cur++] = *src++;
  }
  dst[cur] = '\0';
}

static void capypkg_log_append_clipped(char *dst, size_t dst_size,
                                       const char *src, size_t max_chars) {
  if (!dst || dst_size == 0u || !src) return;
  size_t cur = capypkg_log_strlen(dst);
  size_t copied = 0u;
  while (*src && copied < max_chars && cur + 1u < dst_size) {
    dst[cur++] = *src++;
    ++copied;
  }
  if (*src && cur + 4u < dst_size) {
    dst[cur++] = '.';
    dst[cur++] = '.';
    dst[cur++] = '.';
  }
  dst[cur] = '\0';
}

static void capypkg_log_i32(int value, char *out, size_t out_size) {
  if (!out || out_size == 0u) return;
  char tmp[12];
  size_t ti = 0u;
  unsigned int abs_val;
  int negative = 0;
  if (value < 0) {
    negative = 1;
    abs_val = (unsigned int)(-(value + 1)) + 1u; /* INT_MIN safe */
  } else {
    abs_val = (unsigned int)value;
  }
  if (abs_val == 0u) {
    tmp[ti++] = '0';
  } else {
    while (abs_val > 0u && ti < sizeof(tmp)) {
      tmp[ti++] = (char)('0' + (abs_val % 10u));
      abs_val /= 10u;
    }
  }
  size_t oi = 0u;
  if (negative && oi + 1u < out_size) {
    out[oi++] = '-';
  }
  while (ti > 0u && oi + 1u < out_size) {
    out[oi++] = tmp[--ti];
  }
  out[oi] = '\0';
}

static void capypkg_runtime_log_fetch_failure(const char *url, int http_rc,
                                              int status_code) {
  char line[160];
  char num[12];
  line[0] = '\0';
  num[0] = '\0';
  capypkg_log_append(line, sizeof(line), "[audit] [capypkg] fetch failed: ");
  capypkg_log_append(line, sizeof(line), http_error_string(http_rc));
  capypkg_log_append(line, sizeof(line), " (rc=");
  capypkg_log_i32(http_rc, num, sizeof(num));
  capypkg_log_append(line, sizeof(line), num);
  capypkg_log_append(line, sizeof(line), ", status=");
  capypkg_log_i32(status_code, num, sizeof(num));
  capypkg_log_append(line, sizeof(line), num);
  capypkg_log_append(line, sizeof(line), ") url=");
  capypkg_log_append_clipped(line, sizeof(line), url ? url : "(null)", 64u);
  klog(KLOG_WARN, line);
}

static int capypkg_runtime_fetch_text(const char *url, char *buffer,
                                      size_t buffer_size, size_t *out_len) {
  struct http_response resp = {0};
  if (capypkg_local_bundle_fetch_text(url, buffer, buffer_size, out_len) == 0) {
    return 0;
  }
  /* -1 maps to HTTP_ERR_INVALID_ARGUMENT in http_error_string(). The
   * enum itself lives in the http internal header and is not part of
   * the public ABI, so we use the numeric value here. */
  if (!url || !buffer || buffer_size < 2u) {
    capypkg_runtime_log_fetch_failure(url, -1, 0);
    return -1;
  }
  int rc = http_get(url, &resp);
  if (rc != 0) {
    capypkg_runtime_log_fetch_failure(url, rc, resp.status_code);
    http_response_free(&resp);
    return -1;
  }
  if (resp.status_code < 200 || resp.status_code >= 300 ||
      !resp.body || resp.body_len == 0u) {
    capypkg_runtime_log_fetch_failure(url, 0, resp.status_code);
    http_response_free(&resp);
    return -1;
  }
  size_t to_copy = resp.body_len;
  if (to_copy + 1u > buffer_size) {
    to_copy = buffer_size - 1u;
  }
  for (size_t i = 0u; i < to_copy; ++i) {
    buffer[i] = (char)resp.body[i];
  }
  buffer[to_copy] = '\0';
  if (out_len) {
    *out_len = to_copy;
  }
  http_response_free(&resp);
  return 0;
}

static int capypkg_runtime_fetch_bytes(const char *url, uint8_t *buffer,
                                       size_t buffer_size, size_t *out_len) {
  if (capypkg_local_bundle_fetch_bytes(url, buffer, buffer_size, out_len) == 0) {
    return 0;
  }
  if (!url || !buffer || buffer_size == 0u) {
    capypkg_runtime_log_fetch_failure(url, -1, 0);
    return -1;
  }
  int rc = http_download(url, buffer, buffer_size, out_len);
  if (rc != 0) {
    capypkg_runtime_log_fetch_failure(url, http_last_error(), 0);
    return -1;
  }
  return 0;
}

/* Bridge net/http's size_t progress callback to the capypkg uint64_t
 * download-progress callback. The context is a small stack struct owned
 * by capypkg_runtime_fetch_bytes_progress for the (synchronous)
 * lifetime of the download, so no global state is involved. */
struct capypkg_dl_bridge {
  capypkg_download_progress_fn cb;
  void *ctx;
};

static void capypkg_runtime_http_progress(size_t received, size_t total,
                                          void *ctx) {
  struct capypkg_dl_bridge *b = (struct capypkg_dl_bridge *)ctx;
  if (b && b->cb) {
    b->cb((uint64_t)received, (uint64_t)total, b->ctx);
  }
}

/* Progress-aware payload fetcher bound into the capypkg adapter so the
 * first-boot wizard can render a live byte-level download bar. Mirrors
 * capypkg_runtime_fetch_bytes (local bundle fast path + audit logging)
 * but routes the network download through http_download_progress. */
static int capypkg_runtime_fetch_bytes_progress(
    const char *url, uint8_t *buffer, size_t buffer_size, size_t *out_len,
    capypkg_download_progress_fn cb, void *cb_ctx) {
  struct capypkg_dl_bridge bridge;
  int rc;
  if (capypkg_local_bundle_fetch_bytes(url, buffer, buffer_size, out_len) == 0) {
    /* Local bundle resolves instantly with no streaming; surface a
     * single 100% sample so the bar does not appear stuck at 0%. */
    if (cb && out_len) {
      cb((uint64_t)*out_len, (uint64_t)*out_len, cb_ctx);
    }
    return 0;
  }
  if (!url || !buffer || buffer_size == 0u) {
    capypkg_runtime_log_fetch_failure(url, -1, 0);
    return -1;
  }
  bridge.cb = cb;
  bridge.ctx = cb_ctx;
  rc = http_download_progress(url, buffer, buffer_size, out_len,
                              cb ? capypkg_runtime_http_progress : NULL,
                              cb ? &bridge : NULL);
  if (rc != 0) {
    capypkg_runtime_log_fetch_failure(url, http_last_error(), 0);
    return -1;
  }
  return 0;
}

/* Bind once after storage is ready. Idempotent. */
void kernel_capypkg_bind_runtime_adapters(void) {
  static int g_capypkg_bound = 0;
  if (g_capypkg_bound) {
    return;
  }
  capypkg_set_reader(capypkg_runtime_read_file);
  capypkg_set_writer(capypkg_runtime_write_text);
  capypkg_set_bytes_writer(capypkg_runtime_write_bytes);
  capypkg_set_remover(capypkg_runtime_remove);
  capypkg_set_mkdir(capypkg_runtime_mkdir);
  capypkg_set_text_fetcher(capypkg_runtime_fetch_text);
  capypkg_set_bytes_fetcher(capypkg_runtime_fetch_bytes);
  capypkg_set_bytes_fetcher_progress(capypkg_runtime_fetch_bytes_progress);
  /* Register the real CapyOS-side Ed25519 descriptor verifier (over the kernel
   * ed25519_verify). It stays fail-closed until an operator pins the official
   * offline-generated publisher key via capypkg_set_trusted_publisher_key();
   * none is pinned here, so signed repos still fail with CAPYPKG_ERR_SIGNATURE
   * in production (the publicly-known KAT test key is never trusted). */
  capypkg_set_signature_verifier(capypkg_ed25519_verify_signature);
  (void)capypkg_init();
  g_capypkg_bound = 1;
}

void kernel_update_capypkg_service_status(int rc) {
  struct capypkg_stats stats;
  capypkg_stats_get(&stats);
  if (!stats.initialized) {
    (void)service_manager_set_state(SYSTEM_SERVICE_CAPYPKG,
                                    SYSTEM_SERVICE_STATE_STOPPED, 0,
                                    "package adapter not initialized");
    return;
  }
  if (rc < 0 && rc != CAPYPKG_ERR_NOT_READY && rc != CAPYPKG_ERR_NOT_FOUND) {
    (void)service_manager_set_state(SYSTEM_SERVICE_CAPYPKG,
                                    SYSTEM_SERVICE_STATE_DEGRADED, rc,
                                    capypkg_result_label(rc));
    return;
  }
  const char *summary = stats.repo_count == 0u
                            ? "no package repositories configured"
                            : (stats.catalog_fresh ? "package catalog ready"
                                                   : "package catalog idle");
  (void)service_manager_set_state(SYSTEM_SERVICE_CAPYPKG,
                                  SYSTEM_SERVICE_STATE_READY, 0, summary);
}

/* Auto-bootstrap orchestration:
 *
 *   - first poll after bind tries `capypkg_bootstrap_run(0, ...)`;
 *   - the function is fully idempotent and writes its own marker
 *     once a profile is fully applied (or explicitly basic), so
 *     subsequent polls are essentially free;
 *   - per-call failure backs off the automatic hook: the bootstrap
 *     function only persists the marker when the profile said "basic"
 *     or when the index fetch + install sweep completed without
 *     per-package failures and the marker was written; HTTP/package/
 *     marker errors remain retryable without surfacing as a service
 *     error.
 *
 * `g_capypkg_bootstrap_logged_*` are soft hints to avoid spamming
 * the audit log on every poll when nothing is going to change. The
 * bootstrap.done marker in the VFS is the durable source of truth.
 */

static int g_capypkg_bootstrap_logged_idle = 0;
static int g_capypkg_bootstrap_logged_waiting_net = 0;
static uint64_t g_capypkg_bootstrap_retry_after_tick = 0u;
static uint32_t g_capypkg_bootstrap_failure_count = 0u;

static uint32_t kernel_capypkg_bootstrap_backoff_ticks(void) {
  uint32_t shift = g_capypkg_bootstrap_failure_count > 3u
                       ? 3u
                       : g_capypkg_bootstrap_failure_count;
  return 300u << shift;
}

/* Return 1 if the kernel network stack is in a usable state for
 * capypkg index/payload fetching. DHCP-mode installs additionally
 * require a lease, because http_get would otherwise fail at DNS
 * resolution and the bootstrap would just churn the audit log.
 *
 * Gating the background bootstrap on this prevents the wizard's
 * "[modules] network or repo unavailable" line from being repeated
 * silently every 60 seconds while the kernel is still in network
 * warm-up. */
static int kernel_capypkg_network_is_usable(void) {
  if (capypkg_local_bundle_available()) {
    return 1;
  }
  struct net_stack_status status;
  if (net_stack_status(&status) != 0) {
    return 0;
  }
  if (!status.initialized || !status.runtime_supported ||
      !status.nic.found || !status.ready) {
    return 0;
  }
  if (status.dhcp_attempts > 0u && !status.dhcp_lease_acquired) {
    return 0;
  }
  return 1;
}

static void kernel_capypkg_maybe_bootstrap(void) {
  uint64_t now_ticks = pit_ticks();

  if (g_capypkg_bootstrap_retry_after_tick != 0u &&
      now_ticks < g_capypkg_bootstrap_retry_after_tick) {
    return;
  }
  if (!kernel_capypkg_network_is_usable()) {
    if (!g_capypkg_bootstrap_logged_waiting_net) {
      klog(KLOG_INFO,
           "[audit] [capypkg] bootstrap deferred: network not ready");
      g_capypkg_bootstrap_logged_waiting_net = 1;
    }
    return;
  }
  /* Network came up since last poll; allow the "waiting" line to fire
   * again if connectivity is lost later. */
  g_capypkg_bootstrap_logged_waiting_net = 0;

  int installed = 0;
  int failed = 0;
  int rc = capypkg_bootstrap_run(0, &installed, &failed);
  if (rc == INSTALL_PROFILE_OK) {
    g_capypkg_bootstrap_failure_count = 0u;
    g_capypkg_bootstrap_retry_after_tick = 0u;
    if (installed > 0 || failed > 0) {
      klog(KLOG_INFO,
           "[audit] [capypkg] bootstrap reached install sweep");
    } else if (!g_capypkg_bootstrap_logged_idle) {
      klog(KLOG_INFO,
           "[audit] [capypkg] bootstrap idle (basic profile or marker file present)");
      g_capypkg_bootstrap_logged_idle = 1;
    }
    return;
  }
  if (rc == INSTALL_PROFILE_ERR_STORAGE) {
    /* Transient: per-package failures, repo/index error or marker
     * write error. The bootstrap function emitted its own audit
     * entry; a later service poll will retry after backoff. */
    g_capypkg_bootstrap_retry_after_tick =
        now_ticks + (uint64_t)kernel_capypkg_bootstrap_backoff_ticks();
    if (g_capypkg_bootstrap_failure_count < 8u) {
      g_capypkg_bootstrap_failure_count++;
    }
    return;
  }
  /* Profile/config failure. Keep polling so an operator can correct
   * profile.ini without rebooting; the bootstrap function emitted the
   * audit entry for this attempt. */
  g_capypkg_bootstrap_logged_idle = 1;
}

int kernel_service_poll_capypkg(void *ctx) {
  (void)ctx;
  if (!g_shell_fs_ready) {
    (void)service_manager_set_state(SYSTEM_SERVICE_CAPYPKG,
                                    SYSTEM_SERVICE_STATE_STOPPED, 0,
                                    "waiting for storage runtime");
    return 0;
  }
  kernel_capypkg_bind_runtime_adapters();
  kernel_update_capypkg_service_status(0);
  kernel_capypkg_maybe_bootstrap();
  return 0;
}

int kernel_service_start_capypkg(void *ctx) {
  return kernel_service_poll_capypkg(ctx);
}

int kernel_service_stop_capypkg(void *ctx) {
  (void)ctx;
  (void)service_manager_set_state(SYSTEM_SERVICE_CAPYPKG,
                                  SYSTEM_SERVICE_STATE_STOPPED, 0,
                                  "package adapter stopped");
  return 0;
}
