#include <stdio.h>
#include <string.h>

#include "kernel/log/klog.h"
#include "services/update_agent.h"

#define UPDATE_AGENT_REPOSITORY_PATH "/system/update/repository.ini"
#define UPDATE_AGENT_CACHE_PATH "/system/update/latest.ini"
#define UPDATE_AGENT_STAGE_PATH "/system/update/staged.ini"
#define UPDATE_AGENT_STATE_PATH "/system/update/state.ini"
#define UPDATE_AGENT_IMPORT_PATH "/tmp/update-import.ini"

static char g_klog_capture[4096];
static size_t g_klog_capture_len;

static int capture_klog(const char *path, const char *text) {
  size_t i = 0;
  (void)path;
  while (text[i] && g_klog_capture_len + 1u < sizeof(g_klog_capture)) {
    g_klog_capture[g_klog_capture_len++] = text[i++];
  }
  g_klog_capture[g_klog_capture_len] = '\0';
  return 0;
}

static void reset_capture(void) {
  klog_reset();
  g_klog_capture[0] = '\0';
  g_klog_capture_len = 0u;
}

static void flush_capture(void) {
  klog_flush(capture_klog);
}

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "[audit_events] %s\n", msg);
    return 1;
  }
  return 0;
}

struct fake_file {
  const char *path;
  char text[512];
  int present;
};

static struct fake_file g_files[] = {
    {UPDATE_AGENT_REPOSITORY_PATH, "", 0},
    {UPDATE_AGENT_CACHE_PATH, "", 0},
    {UPDATE_AGENT_STAGE_PATH, "", 0},
    {UPDATE_AGENT_STATE_PATH, "", 0},
    {UPDATE_AGENT_IMPORT_PATH, "", 0},
};

static struct fake_file *find_file(const char *path) {
  size_t i = 0;
  if (!path) return NULL;
  for (i = 0; i < sizeof(g_files) / sizeof(g_files[0]); ++i) {
    if (strcmp(g_files[i].path, path) == 0) return &g_files[i];
  }
  return NULL;
}

static void set_file_text(const char *path, const char *text) {
  struct fake_file *f = find_file(path);
  if (!f) return;
  f->present = text ? 1 : 0;
  f->text[0] = '\0';
  if (text) {
    strncpy(f->text, text, sizeof(f->text) - 1u);
    f->text[sizeof(f->text) - 1u] = '\0';
  }
}

static void reset_files(void) {
  size_t i = 0;
  for (i = 0; i < sizeof(g_files) / sizeof(g_files[0]); ++i) {
    g_files[i].present = 0;
    g_files[i].text[0] = '\0';
  }
}

static int stub_read(const char *path, char *buf, size_t size, size_t *out) {
  struct fake_file *f = find_file(path);
  size_t len = 0;
  size_t i = 0;
  if (!f || !f->present || !buf || size == 0u) return -1;
  len = strlen(f->text);
  if (len + 1u > size) len = size - 1u;
  for (i = 0; i < len; ++i) buf[i] = f->text[i];
  buf[len] = '\0';
  if (out) *out = len;
  return 0;
}

static int stub_write(const char *path, const char *text) {
  if (!path || !text) return -1;
  set_file_text(path, text);
  return 0;
}

static int stub_remove(const char *path) {
  struct fake_file *f = find_file(path);
  if (!f) return -1;
  f->present = 0;
  f->text[0] = '\0';
  return 0;
}

static void setup_agent(void) {
  reset_files();
  update_agent_reset();
  update_agent_set_reader(stub_read);
  update_agent_set_writer(stub_write);
  update_agent_set_remover(stub_remove);
  update_agent_init("0.8.0-alpha.0+20260305");
}

int run_audit_events_tests(void) {
  int fails = 0;

  /* stage success emits [update] Update staged. */
  setup_agent();
  set_file_text(UPDATE_AGENT_REPOSITORY_PATH,
                "channel=stable\nbranch=main\nsource=github:test/CapyOS\n");
  set_file_text(UPDATE_AGENT_CACHE_PATH,
                "available_version=0.9.0-alpha.1\nchannel=stable\npublished_at=2026-04-08\n");
  reset_capture();
  fails += expect_true(update_agent_stage_latest() == 0,
                       "stage should succeed with valid cache");
  flush_capture();
  fails += expect_true(strstr(g_klog_capture, "[update] Update staged.") != NULL,
                       "stage success should emit audit event");

  /* arm success emits [update] Update armed for activation. */
  reset_capture();
  fails += expect_true(update_agent_set_pending_activation(1) == 0,
                       "arm should succeed when staged");
  flush_capture();
  fails += expect_true(strstr(g_klog_capture, "[update] Update armed for activation.") != NULL,
                       "arm success should emit audit event");

  /* disarm emits [update] Update activation disarmed. */
  reset_capture();
  fails += expect_true(update_agent_set_pending_activation(0) == 0,
                       "disarm should succeed");
  flush_capture();
  fails += expect_true(strstr(g_klog_capture, "[update] Update activation disarmed.") != NULL,
                       "disarm should emit audit event");

  /* clear emits [update] Staged update cleared. */
  reset_capture();
  fails += expect_true(update_agent_clear_stage() == 0,
                       "clear should succeed");
  flush_capture();
  fails += expect_true(strstr(g_klog_capture, "[update] Staged update cleared.") != NULL,
                       "clear should emit audit event");

  /* import success emits [update] Manifest imported into local catalog. */
  setup_agent();
  set_file_text(UPDATE_AGENT_REPOSITORY_PATH,
                "channel=stable\nbranch=main\nsource=github:test/CapyOS\n");
  set_file_text(UPDATE_AGENT_IMPORT_PATH,
                "available_version=1.0.0-alpha.1\nchannel=stable\nbranch=main\n"
                "source=github:test/CapyOS\npublished_at=2026-04-09\n");
  reset_capture();
  fails += expect_true(update_agent_import_manifest_path(UPDATE_AGENT_IMPORT_PATH) == 0,
                       "import should succeed with matching manifest");
  flush_capture();
  fails += expect_true(strstr(g_klog_capture, "[update] Manifest imported into local catalog.") != NULL,
                       "import success should emit audit event");

  /* import mismatch emits [update] Manifest import rejected: repository mismatch. */
  setup_agent();
  set_file_text(UPDATE_AGENT_REPOSITORY_PATH,
                "channel=stable\nbranch=main\nsource=github:test/CapyOS\n");
  set_file_text(UPDATE_AGENT_IMPORT_PATH,
                "available_version=1.0.0-alpha.2\nchannel=develop\nbranch=develop\n"
                "source=github:test/CapyOS\npublished_at=2026-04-09\n");
  reset_capture();
  fails += expect_true(update_agent_import_manifest_path(UPDATE_AGENT_IMPORT_PATH) == -19,
                       "import with channel mismatch should fail");
  flush_capture();
  fails += expect_true(strstr(g_klog_capture, "[update] Manifest import rejected: repository mismatch.") != NULL,
                       "import mismatch should emit audit event");

  update_agent_reset();

  if (fails == 0) {
    printf("[tests] audit_events OK\n");
  }
  return fails;
}
