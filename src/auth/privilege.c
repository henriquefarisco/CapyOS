#include "auth/privilege.h"

#include "kernel/log/klog.h"

#include <stddef.h>

static int priv_str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int privilege_user_is_admin(const struct user_record *user) {
    if (!user) return 0;
    return priv_str_eq(user->role, "admin") ? 1 : 0;
}

int privilege_session_is_admin(const struct session_context *session) {
    if (!session) return 0;
    return privilege_user_is_admin(session_user(session));
}

int privilege_active_is_admin(void) {
    struct session_context *session = session_active();
    return privilege_session_is_admin(session);
}

int privilege_check_admin_or_self(const struct user_record *user,
                                  const char *target_username) {
    if (!user) return 0;
    if (privilege_user_is_admin(user)) return 1;
    if (!target_username) return 0;
    return priv_str_eq(user->username, target_username) ? 1 : 0;
}

static void priv_log_emit(int level, const char *verdict, const char *action,
                          const struct user_record *actor) {
    char line[160];
    size_t pos = 0;
    const char *prefix = "[priv] ";
    while (prefix[pos] && pos + 1 < sizeof(line)) {
        line[pos] = prefix[pos];
        pos++;
    }
    size_t i = 0;
    while (verdict && verdict[i] && pos + 1 < sizeof(line)) {
        line[pos++] = verdict[i++];
    }
    if (pos + 1 < sizeof(line)) line[pos++] = ':';
    if (pos + 1 < sizeof(line)) line[pos++] = ' ';
    i = 0;
    while (action && action[i] && pos + 1 < sizeof(line)) {
        line[pos++] = action[i++];
    }
    if (actor && actor->username[0]) {
        const char *sep = " actor=";
        size_t k = 0;
        while (sep[k] && pos + 1 < sizeof(line)) line[pos++] = sep[k++];
        size_t u = 0;
        while (actor->username[u] && pos + 1 < sizeof(line)) {
            line[pos++] = actor->username[u++];
        }
    }
    line[pos] = '\0';
    klog(level, line);
}

void privilege_log_denied(const char *action,
                          const struct user_record *actor) {
    priv_log_emit(KLOG_WARN, "denied", action ? action : "(unknown)", actor);
}

void privilege_log_granted(const char *action,
                           const struct user_record *actor) {
    priv_log_emit(KLOG_DEBUG, "granted", action ? action : "(unknown)", actor);
}
