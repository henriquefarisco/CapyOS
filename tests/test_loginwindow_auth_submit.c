#include <stdio.h>
#include <string.h>

#include "auth/login_runtime.h"

static int g_auth_calls;
static int g_auth_result;
static const char *g_seen_username;

static int expect_true(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "[loginwindow_auth_submit] %s\n", message);
    return 1;
  }
  return 0;
}

static void reset_auth_submit_state(void) {
  g_auth_calls = 0;
  g_auth_result = USERDB_AUTH_OK;
  g_seen_username = NULL;
}

static void copy_text(char *dst, size_t dst_size, const char *src) {
  size_t idx = 0;
  if (!dst || dst_size == 0) return;
  if (src) {
    while (src[idx] && idx + 1 < dst_size) {
      dst[idx] = src[idx];
      ++idx;
    }
  }
  dst[idx] = '\0';
}

static int strings_equal(const char *a, const char *b) {
  if (!a || !b) return 0;
  return strcmp(a, b) == 0;
}

static int auth_stub(const char *username,
                     const char *password,
                     struct user_record *out) {
  ++g_auth_calls;
  g_seen_username = username;
  if (!strings_equal(password, "pw")) {
    return USERDB_AUTH_FAILED;
  }
  if (g_auth_result == USERDB_AUTH_OK && out) {
    copy_text(out->username, sizeof(out->username), "admin");
    copy_text(out->role, sizeof(out->role), "admin");
    copy_text(out->home, sizeof(out->home), "/home/admin");
    out->uid = 1000u;
    out->gid = 1000u;
  }
  return g_auth_result;
}

static void ready_contract(struct login_window_contract *contract) {
  memset(contract, 0, sizeof(*contract));
  contract->ready = 1;
  contract->has_input = 1;
  contract->session_available = 1;
  contract->settings_available = 1;
  contract->shell_callbacks_ready = 1;
  contract->auth_callbacks_ready = 1;
  contract->ui_callbacks_ready = 1;
  contract->blocked_reason = "ready";
}

static int build_auth_buffer(struct login_window_credential_policy *policy,
                             char *storage,
                             size_t storage_size,
                             struct login_window_credential_buffer *buffer) {
  struct login_window_contract contract;
  ready_contract(&contract);
  if (login_window_credential_auth_policy_from_contract(&contract, policy) != 0) {
    return -1;
  }
  if (login_window_credential_buffer_init(buffer, storage, storage_size,
                                          policy) != 0) {
    return -1;
  }
  if (login_window_credential_buffer_append(buffer, 'p') != 1 ||
      login_window_credential_buffer_append(buffer, 'w') != 1) {
    return -1;
  }
  return 0;
}

static int test_auth_policy_enables_explicit_submit(void) {
  int fails = 0;
  struct login_window_contract contract;
  struct login_window_credential_policy policy;

  reset_auth_submit_state();
  ready_contract(&contract);
  fails += expect_true(login_window_credential_auth_policy_from_contract(
                           &contract, &policy) == 0,
                       "auth policy should build from ready contract");
  fails += expect_true(policy.password_field_allowed == 1,
                       "auth policy should keep password field available");
  fails += expect_true(policy.password_submit_allowed == 1,
                       "auth policy should explicitly enable submit");
  fails += expect_true(policy.password_mask_required == 1 &&
                           policy.password_wipe_required == 1,
                       "auth policy should require mask and wipe");
  fails += expect_true(policy.text_login_authoritative == 1,
                       "auth policy should keep text fallback authoritative");
  return fails;
}

static int test_auth_submit_success_calls_callback_and_wipes(void) {
  int fails = 0;
  char storage[8];
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_auth_submit submit;
  struct user_record user;

  reset_auth_submit_state();
  memset(&user, 0xA5, sizeof(user));
  fails += expect_true(build_auth_buffer(&policy, storage, sizeof(storage),
                                         &buffer) == 0,
                       "success fixture should build auth buffer");
  fails += expect_true(login_window_credential_auth_submit_consume(
                           &policy, "admin", &buffer, auth_stub, &user,
                           &submit) == 0,
                       "success submit should consume");
  fails += expect_true(g_auth_calls == 1,
                       "success submit should call auth callback once");
  fails += expect_true(strings_equal(g_seen_username, "admin"),
                       "success submit should pass username internally");
  fails += expect_true(submit.authenticated == 1 && submit.auth_failed == 0 &&
                           submit.auth_locked == 0,
                       "success submit should report authenticated");
  fails += expect_true(submit.user_record_available == 1 &&
                           strings_equal(user.username, "admin"),
                       "success submit should return authenticated user");
  fails += expect_true(submit.wipe_attempted == 1 &&
                           submit.wipe_succeeded == 1 && storage[0] == '\0' &&
                           buffer.length == 0 && buffer.wiped == 1,
                       "success submit should wipe credential buffer");
  fails += expect_true(submit.credential_redacted == 1 &&
                           submit.length_redacted == 1 &&
                           submit.raw_secret_exposed == 0,
                       "success submit should keep diagnostics redacted");
  return fails;
}

static int test_auth_submit_failure_and_lockout_fail_closed(void) {
  int fails = 0;
  char storage[8];
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_auth_submit submit;
  struct user_record user;

  reset_auth_submit_state();
  g_auth_result = USERDB_AUTH_FAILED;
  memset(&user, 0xA5, sizeof(user));
  fails += expect_true(build_auth_buffer(&policy, storage, sizeof(storage),
                                         &buffer) == 0,
                       "failure fixture should build auth buffer");
  fails += expect_true(login_window_credential_auth_submit_consume(
                           &policy, "admin", &buffer, auth_stub, &user,
                           &submit) == 0,
                       "failure submit should consume");
  fails += expect_true(g_auth_calls == 1 && submit.auth_failed == 1 &&
                           submit.authenticated == 0,
                       "failure submit should call auth and fail closed");
  fails += expect_true(user.username[0] == '\0' && storage[0] == '\0' &&
                           buffer.length == 0,
                       "failure submit should zero user and credential storage");
  fails += expect_true(strings_equal(submit.blocked_reason, "auth-failed"),
                       "failure submit should expose generic failure reason");

  reset_auth_submit_state();
  g_auth_result = USERDB_AUTH_LOCKED;
  memset(&user, 0xA5, sizeof(user));
  fails += expect_true(build_auth_buffer(&policy, storage, sizeof(storage),
                                         &buffer) == 0,
                       "lockout fixture should build auth buffer");
  fails += expect_true(login_window_credential_auth_submit_consume(
                           &policy, "admin", &buffer, auth_stub, &user,
                           &submit) == 0,
                       "lockout submit should consume");
  fails += expect_true(g_auth_calls == 1 && submit.auth_locked == 1 &&
                           submit.authenticated == 0,
                       "lockout submit should preserve lockout state");
  fails += expect_true(user.username[0] == '\0' && storage[0] == '\0' &&
                           buffer.length == 0,
                       "lockout submit should zero user and credential storage");
  fails += expect_true(strings_equal(submit.blocked_reason, "auth-locked"),
                       "lockout submit should expose locked reason");
  return fails;
}

static int test_auth_submit_blocks_without_explicit_submit_policy(void) {
  int fails = 0;
  char storage[8];
  struct login_window_contract contract;
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_auth_submit submit;
  struct user_record user;

  reset_auth_submit_state();
  ready_contract(&contract);
  memset(&user, 0xA5, sizeof(user));
  fails += expect_true(login_window_credential_policy_from_contract(
                           &contract, &policy) == 0,
                       "base policy should build");
  fails += expect_true(policy.password_submit_allowed == 0,
                       "base policy should keep submit disabled");
  fails += expect_true(login_window_credential_buffer_init(
                           &buffer, storage, sizeof(storage), &policy) == 0,
                       "base policy buffer should initialize");
  fails += expect_true(login_window_credential_buffer_append(&buffer, 'p') == 1,
                       "base policy buffer should accept secret");
  fails += expect_true(login_window_credential_auth_submit_consume(
                           &policy, "admin", &buffer, auth_stub, &user,
                           &submit) == 0,
                       "blocked submit should consume");
  fails += expect_true(g_auth_calls == 0 && submit.auth_called == 0 &&
                           submit.authenticated == 0,
                       "blocked submit must not call auth callback");
  fails += expect_true(storage[0] == '\0' && buffer.length == 0 &&
                           buffer.wiped == 1 && user.username[0] == '\0',
                       "blocked submit should wipe credential and user output");
  fails += expect_true(strings_equal(submit.blocked_reason,
                                     "gui-submit-disabled"),
                       "blocked submit should preserve gate reason");
  return fails;
}

static int test_auth_submit_requires_output_and_callback(void) {
  int fails = 0;
  char storage[8];
  struct login_window_credential_policy policy;
  struct login_window_credential_buffer buffer;
  struct login_window_credential_auth_submit submit;
  struct user_record user;

  reset_auth_submit_state();
  fails += expect_true(build_auth_buffer(&policy, storage, sizeof(storage),
                                         &buffer) == 0,
                       "missing output fixture should build auth buffer");
  fails += expect_true(login_window_credential_auth_submit_consume(
                           &policy, "admin", &buffer, auth_stub, NULL,
                           &submit) == 0,
                       "missing output submit should consume");
  fails += expect_true(g_auth_calls == 0 && submit.auth_called == 0 &&
                           submit.user_record_available == 0,
                       "missing output submit must not call auth");
  fails += expect_true(submit.submit_allowed == 0 &&
                           submit.auth_attempt_allowed == 0,
                       "missing output submit should clear permission flags");
  fails += expect_true(storage[0] == '\0' && buffer.length == 0 &&
                           buffer.wiped == 1,
                       "missing output submit should wipe credential");
  fails += expect_true(strings_equal(submit.blocked_reason,
                                     "user-output-unavailable"),
                       "missing output submit should explain block");

  reset_auth_submit_state();
  memset(&user, 0xA5, sizeof(user));
  fails += expect_true(build_auth_buffer(&policy, storage, sizeof(storage),
                                         &buffer) == 0,
                       "missing callback fixture should build auth buffer");
  fails += expect_true(login_window_credential_auth_submit_consume(
                           &policy, "admin", &buffer, NULL, &user,
                           &submit) == 0,
                       "missing callback submit should consume");
  fails += expect_true(g_auth_calls == 0 && submit.auth_called == 0 &&
                           submit.authenticated == 0,
                       "missing callback submit must not call auth");
  fails += expect_true(submit.submit_allowed == 0 &&
                           submit.auth_attempt_allowed == 0,
                       "missing callback submit should clear permission flags");
  fails += expect_true(storage[0] == '\0' && user.username[0] == '\0',
                       "missing callback submit should wipe credential and output");
  fails += expect_true(strings_equal(submit.blocked_reason,
                                     "auth-callback-unavailable"),
                       "missing callback submit should explain block");
  return fails;
}

int run_loginwindow_auth_submit_tests(void) {
  int fails = 0;
  fails += test_auth_policy_enables_explicit_submit();
  fails += test_auth_submit_success_calls_callback_and_wipes();
  fails += test_auth_submit_failure_and_lockout_fail_closed();
  fails += test_auth_submit_blocks_without_explicit_submit_policy();
  fails += test_auth_submit_requires_output_and_callback();
  if (fails == 0) {
    printf("[tests] loginwindow_auth_submit: ok\n");
  }
  return fails;
}
