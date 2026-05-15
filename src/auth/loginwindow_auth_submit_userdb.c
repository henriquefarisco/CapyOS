#include "auth/login_runtime.h"

int login_window_credential_auth_submit_userdb_consume(
    const struct login_window_credential_policy *policy,
    const char *username,
    struct login_window_credential_buffer *buffer,
    struct user_record *out_user,
    struct login_window_credential_auth_submit *out) {
  return login_window_credential_auth_submit_consume(
      policy, username, buffer, userdb_authenticate_with_policy, out_user, out);
}
