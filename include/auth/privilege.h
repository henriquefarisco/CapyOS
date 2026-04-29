#ifndef AUTH_PRIVILEGE_H
#define AUTH_PRIVILEGE_H

#include "auth/session.h"
#include "auth/user.h"

/* privilege: centralized authorization helpers for the shell, GUI apps and
 * runtime jobs. The API is intentionally narrow so it can be audited as a
 * single chokepoint:
 *
 *   - privilege_user_is_admin(user)        : pure check on a user_record
 *   - privilege_session_is_admin(session)  : check on the active session
 *   - privilege_active_is_admin()          : convenience for the global
 *                                            session pointer
 *   - privilege_check_admin_or_self(...)   : self-or-admin policy used by
 *                                            password change and similar
 *
 * Every privileged code path should funnel through these helpers and emit
 * a "[priv]" klog entry on denial (handled by the caller via
 * privilege_log_denied) so the persistent audit trail captures the event.
 */

int privilege_user_is_admin(const struct user_record *user);
int privilege_session_is_admin(const struct session_context *session);
int privilege_active_is_admin(void);

/* Returns 1 if `user` may operate on `target_username` either because they
 * are admin, or because they are the same account. Returns 0 otherwise. */
int privilege_check_admin_or_self(const struct user_record *user,
                                  const char *target_username);

/* Emit a uniform "[priv] denied: <action>" warn-level klog entry. The
 * action label should be a short, stable string like "add-user",
 * "set-pass:other", "open-update-store", "recovery-login". Use this from
 * every denial path so the audit trail is consistent. */
void privilege_log_denied(const char *action,
                          const struct user_record *actor);

/* Emit a "[priv] granted: <action>" debug-level klog entry. This is
 * optional; use it for sensitive operations whose successful execution
 * should also be visible in the audit trail (e.g. recovery, key reset). */
void privilege_log_granted(const char *action,
                           const struct user_record *actor);

#endif /* AUTH_PRIVILEGE_H */
