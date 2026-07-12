/* Shared CapyAI plan -> dispatch executor.
 *
 * Both the shell command and graphical chat use this path so permission
 * checks, command construction, result reporting and session updates cannot
 * drift.  The actual side effect stays behind a caller-owned callback.
 */
#include "services/capyai.h"

static void capyai_zero(void *ptr, size_t size) {
    unsigned char *p = (unsigned char *)ptr;
    size_t i;
    if (!p) return;
    for (i = 0u; i < size; ++i) p[i] = 0u;
}

static int capyai_action_updates_last_file(const char *action) {
    static const char *const actions[] = {
        "file_create", "file_edit_text", "dir_names_to_file",
        "dir_count_to_file", NULL,
    };
    size_t i;
    if (!action) return 0;
    for (i = 0u; actions[i]; ++i) {
        size_t j = 0u;
        while (action[j] && actions[i][j] && action[j] == actions[i][j]) ++j;
        if (action[j] == '\0' && actions[i][j] == '\0') return 1;
    }
    return 0;
}

static int capyai_build_command_line(const struct capyai_plan *plan,
                                     char *line, size_t line_size) {
    size_t n = 0u;
    size_t i;
    if (!plan || !line || line_size == 0u || !plan->out.command[0]) return -1;
    for (i = 0u; plan->out.command[i] && n + 1u < line_size; ++i) {
        line[n++] = plan->out.command[i];
    }
    if (plan->out.command[i]) return -1;
    if (plan->out.path[0]) {
        if (n + 2u > line_size) return -1;
        line[n++] = ' ';
        for (i = 0u; plan->out.path[i] && n + 1u < line_size; ++i) {
            line[n++] = plan->out.path[i];
        }
        if (plan->out.path[i]) return -1;
    }
    line[n] = '\0';
    return 0;
}

int capyai_execute_intent(const char *model_text, size_t model_len,
                          const char *intent, const char *platform,
                          const char *shell,
                          const struct capyai_perms *perms,
                          struct capyai_session *session,
                          capyai_dispatch_fn dispatch, void *dispatch_ctx,
                          struct capyai_plan *plan,
                          struct capyai_exec_result *result) {
    char command_line[CAPYAI_COMMAND_MAX];
    int rc;
    if (!plan || !result || !intent) return -1;
    capyai_zero(result, sizeof(*result));
    rc = capyai_plan(model_text, model_len, intent, platform, shell, perms,
                     session, plan);
    if (rc != 0) return rc;

    if (plan->decision == CAPYAI_DECISION_ALLOWED && dispatch) {
        if (capyai_build_command_line(plan, command_line,
                                      sizeof(command_line)) != 0) {
            result->executed = 1;
            result->rc = 127;
        } else {
            result->executed = 1;
            result->rc = dispatch(dispatch_ctx, command_line);
            if (result->rc < 0) result->rc = 127;
            if (result->rc == 0 && session && plan->out.path[0] &&
                capyai_action_updates_last_file(plan->out.action)) {
                capyai_session_note_file(session, plan->out.path);
            }
        }
    }
    if (session) session->turns++;
    return 0;
}
