#include <stdio.h>
#include <string.h>

#include "auth/privilege.h"
#include "auth/session.h"
#include "auth/user.h"
#include "kernel/log/klog.h"

/* session_active and session_set_active are provided by tests/test_user_home.c
 * in the same test binary; we only need session_user, which neither
 * test_user_home.c nor test_login_runtime.c expose externally. Defining it
 * here keeps the privilege test independent of src/auth/session.c (which
 * depends on the VFS). */
const struct user_record *session_user(const struct session_context *ctx) {
    if (!ctx) return NULL;
    return &ctx->user;
}

static int expect_true(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "[privilege] %s\n", msg);
        return 1;
    }
    return 0;
}

static void make_user(struct user_record *u, const char *name,
                      const char *role) {
    memset(u, 0, sizeof(*u));
    strncpy(u->username, name, sizeof(u->username) - 1);
    strncpy(u->role, role, sizeof(u->role) - 1);
    strncpy(u->home, "/home/test", sizeof(u->home) - 1);
}

static int test_user_admin_check(void) {
    struct user_record admin, regular;
    int fails = 0;
    make_user(&admin, "alice", "admin");
    make_user(&regular, "bob", "user");
    fails += expect_true(privilege_user_is_admin(&admin) == 1,
                         "admin role flagged admin");
    fails += expect_true(privilege_user_is_admin(&regular) == 0,
                         "user role NOT flagged admin");
    fails += expect_true(privilege_user_is_admin(NULL) == 0,
                         "null user not admin");
    return fails;
}

static int test_admin_or_self(void) {
    struct user_record regular;
    int fails = 0;
    make_user(&regular, "bob", "user");
    fails += expect_true(privilege_check_admin_or_self(&regular, "bob") == 1,
                         "bob may act on bob");
    fails += expect_true(privilege_check_admin_or_self(&regular, "carol") == 0,
                         "bob may NOT act on carol");
    fails += expect_true(privilege_check_admin_or_self(NULL, "bob") == 0,
                         "null user denied");
    fails += expect_true(privilege_check_admin_or_self(&regular, NULL) == 0,
                         "null target denied");

    struct user_record admin;
    make_user(&admin, "alice", "admin");
    fails += expect_true(privilege_check_admin_or_self(&admin, "carol") == 1,
                         "admin may act on anyone");
    fails += expect_true(privilege_check_admin_or_self(&admin, NULL) == 1,
                         "admin allowed even with null target");
    return fails;
}

static int test_session_admin_check(void) {
    struct session_context ctx;
    struct user_record admin;
    int fails = 0;
    memset(&ctx, 0, sizeof(ctx));
    make_user(&admin, "alice", "admin");
    ctx.user = admin;
    fails += expect_true(privilege_session_is_admin(&ctx) == 1,
                         "session of admin reported admin");
    session_set_active(&ctx);
    fails += expect_true(privilege_active_is_admin() == 1,
                         "active session of admin reported admin");
    session_set_active(NULL);
    fails += expect_true(privilege_active_is_admin() == 0,
                         "no active session is not admin");
    return fails;
}

static char g_priv_klog_capture[4096];
static size_t g_priv_klog_capture_len;

static int capture_klog(const char *path, const char *text) {
    size_t i = 0;
    (void)path;
    while (text && text[i] &&
           g_priv_klog_capture_len + 1u < sizeof(g_priv_klog_capture)) {
        g_priv_klog_capture[g_priv_klog_capture_len++] = text[i++];
    }
    g_priv_klog_capture[g_priv_klog_capture_len] = '\0';
    return 0;
}

static void priv_reset_klog_capture(void) {
    klog_reset();
    g_priv_klog_capture[0] = '\0';
    g_priv_klog_capture_len = 0u;
}

static int test_privilege_log_omits_username(void) {
    /* alpha.211 contract: `priv_log_emit` must not write any user
     * identifier (`actor->username`, `actor->home`, ...) into the klog
     * stream. Every shell/UI denial path runs with a real user_record
     * pulled from the active session, so this is the single chokepoint
     * where the previous `actor=<username>` leak fired. */
    int fails = 0;
    struct user_record actor;
    make_user(&actor, "alice", "admin");

    priv_reset_klog_capture();
    privilege_log_denied("settings-add-user", &actor);
    klog_flush(capture_klog);
    fails += expect_true(strstr(g_priv_klog_capture, "alice") == NULL,
                         "denied log must not leak the username");
    fails += expect_true(strstr(g_priv_klog_capture, "actor_role=admin") != NULL,
                         "denied log must expose the role for audit");
    fails += expect_true(strstr(g_priv_klog_capture, "denied: settings-add-user") != NULL,
                         "denied log must carry the verdict/action pair");

    priv_reset_klog_capture();
    privilege_log_granted("recovery-login", &actor);
    klog_flush(capture_klog);
    fails += expect_true(strstr(g_priv_klog_capture, "alice") == NULL,
                         "granted log must not leak the username");
    fails += expect_true(strstr(g_priv_klog_capture, "actor_role=admin") != NULL,
                         "granted log must expose the role for audit");
    fails += expect_true(strstr(g_priv_klog_capture, "granted: recovery-login") != NULL,
                         "granted log must carry the verdict/action pair");

    /* When the role is empty (defensive path), the log must still emit a
     * valid line without the actor_role= suffix and obviously without
     * leaking the username. */
    struct user_record roleless;
    memset(&roleless, 0, sizeof(roleless));
    strncpy(roleless.username, "carol", sizeof(roleless.username) - 1);
    priv_reset_klog_capture();
    privilege_log_denied("set-pass:other", &roleless);
    klog_flush(capture_klog);
    fails += expect_true(strstr(g_priv_klog_capture, "carol") == NULL,
                         "denied log must not leak username even when role is empty");
    fails += expect_true(strstr(g_priv_klog_capture, "actor_role=") == NULL,
                         "denied log must omit actor_role when role is empty");
    fails += expect_true(strstr(g_priv_klog_capture, "denied: set-pass:other") != NULL,
                         "denied log must still carry verdict/action");

    /* NULL actor must not crash and must still emit the verdict/action. */
    priv_reset_klog_capture();
    privilege_log_denied("open-update-store", NULL);
    klog_flush(capture_klog);
    fails += expect_true(strstr(g_priv_klog_capture, "denied: open-update-store") != NULL,
                         "denied log must work with NULL actor");
    return fails;
}

int run_privilege_tests(void) {
    int fails = 0;
    fails += test_user_admin_check();
    fails += test_admin_or_self();
    fails += test_session_admin_check();
    fails += test_privilege_log_omits_username();
    if (fails == 0) {
        printf("[tests] privilege OK\n");
    }
    return fails;
}
