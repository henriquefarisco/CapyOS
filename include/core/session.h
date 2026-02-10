#ifndef SESSION_H
#define SESSION_H

#include <stddef.h>

#include "core/user.h"

#define SESSION_PATH_MAX 128

struct session_context {
    struct user_record user;
    char cwd[SESSION_PATH_MAX];
};

void session_reset(struct session_context *ctx);
int session_begin(struct session_context *ctx, const struct user_record *user);
const struct user_record *session_user(const struct session_context *ctx);
const char *session_cwd(const struct session_context *ctx);
int session_set_cwd(struct session_context *ctx, const char *path);
int session_resolve_path(const struct session_context *ctx, const char *input, char *out, size_t out_len);
void session_set_active(struct session_context *ctx);
struct session_context *session_active(void);


#endif
