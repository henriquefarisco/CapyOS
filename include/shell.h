#ifndef SHELL_H
#define SHELL_H

#include "session.h"
#include "system_init.h"

enum shell_result {
    SHELL_RESULT_EXIT = 0,
    SHELL_RESULT_LOGOUT = 1
};

enum shell_result shell_run(struct session_context *session, const struct system_settings *settings);

#endif
