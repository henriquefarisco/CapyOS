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

static void capyai_copy(char *dst, size_t dst_size, const char *src) {
    size_t i = 0u;
    if (!dst || dst_size == 0u) return;
    while (src && src[i] && i + 1u < dst_size) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

enum capyai_legacy_target_mode {
    CAPYAI_LEGACY_TARGET_NONE = 0,
    CAPYAI_LEGACY_TARGET_OPTIONAL,
    CAPYAI_LEGACY_TARGET_REQUIRED,
};

struct capyai_legacy_dispatch_spec {
    const char *action;
    const char *command;
    enum capy_ai_risk risk;
    uint8_t target_mode;
};

enum capyai_typed_secondary_mode {
    CAPYAI_TYPED_SECONDARY_NONE = 0,
    CAPYAI_TYPED_SECONDARY_TEXT_OPTIONAL,
    CAPYAI_TYPED_SECONDARY_TEXT_REQUIRED,
    CAPYAI_TYPED_SECONDARY_PATH_REQUIRED,
};

struct capyai_typed_dispatch_spec {
    const char *action;
    const char *command;
    enum capy_ai_risk risk;
    uint8_t target_mode;
    uint8_t secondary_mode;
};

/* Typed calls never become shell text. `content` is an append payload for the
 * first two actions and the destination path for file_move. */
static const struct capyai_typed_dispatch_spec capyai_typed_specs[] = {
    {"file_create",    "touch",     CAPY_AI_RISK_WRITE_FILE,
     CAPYAI_LEGACY_TARGET_REQUIRED,
     CAPYAI_TYPED_SECONDARY_TEXT_OPTIONAL},
    {"file_edit_text", "file-edit", CAPY_AI_RISK_WRITE_FILE,
     CAPYAI_LEGACY_TARGET_REQUIRED,
     CAPYAI_TYPED_SECONDARY_TEXT_REQUIRED},
    {"file_move",      "move",      CAPY_AI_RISK_WRITE_FILE,
     CAPYAI_LEGACY_TARGET_REQUIRED,
     CAPYAI_TYPED_SECONDARY_PATH_REQUIRED},
    {"app_open",       "app-open",  CAPY_AI_RISK_READ_ONLY,
     CAPYAI_LEGACY_TARGET_REQUIRED, CAPYAI_TYPED_SECONDARY_NONE},
    {"app_close",      "app-close", CAPY_AI_RISK_SYSTEM_CHANGE,
     CAPYAI_LEGACY_TARGET_REQUIRED, CAPYAI_TYPED_SECONDARY_NONE},
    {"power_control",  "power-reboot", CAPY_AI_RISK_SYSTEM_CHANGE,
     CAPYAI_LEGACY_TARGET_NONE, CAPYAI_TYPED_SECONDARY_NONE},
    {"power_control",  "power-shutdown", CAPY_AI_RISK_SYSTEM_CHANGE,
     CAPYAI_LEGACY_TARGET_NONE, CAPYAI_TYPED_SECONDARY_NONE},
    {"power_schedule", "power-schedule-reboot", CAPY_AI_RISK_SYSTEM_CHANGE,
     CAPYAI_LEGACY_TARGET_REQUIRED, CAPYAI_TYPED_SECONDARY_NONE},
    {"power_schedule_status", "power-schedule-status", CAPY_AI_RISK_READ_ONLY,
     CAPYAI_LEGACY_TARGET_NONE, CAPYAI_TYPED_SECONDARY_NONE},
    {"power_schedule_cancel", "power-schedule-cancel", CAPY_AI_RISK_SYSTEM_CHANGE,
     CAPYAI_LEGACY_TARGET_NONE, CAPYAI_TYPED_SECONDARY_NONE},
};

/* This is the complete legacy CapyOS shell bridge vocabulary.  The model is
 * untrusted data: only an exact tuple compiled into the kernel can reach the
 * caller-owned capysh dispatcher.  Commands that need more than one argument
 * (move/service-control) intentionally stay unavailable until they use a
 * typed adapter instead of a text command line. */
static const struct capyai_legacy_dispatch_spec capyai_legacy_specs[] = {
    {"shell_command", "list",            CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_OPTIONAL},
    {"shell_command", "mypath",          CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_NONE},
    {"shell_command", "pkg-list",        CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_NONE},
    {"shell_command", "list-users",      CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_NONE},
    {"shell_command", "net-status",      CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_NONE},
    {"shell_command", "net-ip",          CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_NONE},
    {"shell_command", "mess",            CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_NONE},
    {"shell_command", "help-any",        CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_NONE},
    {"shell_command", "print-tasks",     CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_NONE},
    {"shell_command", "print-mem",       CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_NONE},
    {"shell_command", "print-file",      CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_REQUIRED},
    {"file_read",     "print-file",      CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_REQUIRED},
    {"shell_command", "find",            CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_REQUIRED},
    {"shell_command", "hunt-dir",        CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_REQUIRED},
    {"dir_find",      "hunt-dir",        CAPY_AI_RISK_READ_ONLY,     CAPYAI_LEGACY_TARGET_REQUIRED},
    {"dir_create",    "mkdir",           CAPY_AI_RISK_WRITE_FILE,    CAPYAI_LEGACY_TARGET_REQUIRED},
    {"file_create",   "touch",           CAPY_AI_RISK_WRITE_FILE,    CAPYAI_LEGACY_TARGET_REQUIRED},
    {"file_delete",   "kill-file",       CAPY_AI_RISK_WRITE_FILE,    CAPYAI_LEGACY_TARGET_REQUIRED},
    {"dir_delete",    "kill-dir",        CAPY_AI_RISK_WRITE_FILE,    CAPYAI_LEGACY_TARGET_REQUIRED},
    {"shell_command", "net-fetch",       CAPY_AI_RISK_NETWORK,       CAPYAI_LEGACY_TARGET_REQUIRED},
    {"shell_command", "pkg-install",     CAPY_AI_RISK_SYSTEM_CHANGE, CAPYAI_LEGACY_TARGET_REQUIRED},
    {"shell_command", "pkg-remove",      CAPY_AI_RISK_SYSTEM_CHANGE, CAPYAI_LEGACY_TARGET_REQUIRED},
    {"shell_command", "shutdown-reboot", CAPY_AI_RISK_SYSTEM_CHANGE, CAPYAI_LEGACY_TARGET_NONE},
    {"shell_command", "shutdown-off",    CAPY_AI_RISK_SYSTEM_CHANGE, CAPYAI_LEGACY_TARGET_NONE},
};

static int capyai_bounded_equal(const char *value, size_t capacity,
                                const char *literal) {
    size_t i;
    if (!value || !literal || capacity == 0u) return 0;
    for (i = 0u; i < capacity; ++i) {
        if (literal[i] == '\0') return value[i] == '\0';
        if (value[i] == '\0' || value[i] != literal[i]) return 0;
    }
    return 0;
}

static int capyai_bounded_text_safe(const char *value, size_t capacity,
                                    int required) {
    size_t i;
    if (!value || capacity == 0u) return 0;
    for (i = 0u; i < capacity; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (c == 0u) return required ? i > 0u : 1;
        if ((c < 0x20u && c != '\n' && c != '\r' && c != '\t') || c == 0x7fu) {
            return 0;
        }
    }
    return 0;
}

static int capyai_action_is_typed(const struct capy_ai_output *out) {
    size_t i;
    if (!out) return 0;
    for (i = 0u; i < sizeof(capyai_typed_specs) / sizeof(capyai_typed_specs[0]);
         ++i) {
        if (capyai_bounded_equal(out->action, sizeof(out->action),
                                 capyai_typed_specs[i].action)) return 1;
    }
    return 0;
}

static const struct capyai_typed_dispatch_spec *capyai_find_typed_spec(
    const struct capy_ai_output *out) {
    size_t i;
    if (!out) return NULL;
    for (i = 0u; i < sizeof(capyai_typed_specs) / sizeof(capyai_typed_specs[0]);
         ++i) {
        const struct capyai_typed_dispatch_spec *spec = &capyai_typed_specs[i];
        if (out->risk == spec->risk &&
            capyai_bounded_equal(out->action, sizeof(out->action), spec->action) &&
            capyai_bounded_equal(out->command, sizeof(out->command), spec->command)) {
            return spec;
        }
    }
    return NULL;
}

static const struct capyai_legacy_dispatch_spec *capyai_find_legacy_spec(
    const struct capy_ai_output *out) {
    size_t i;
    if (!out) return NULL;
    for (i = 0u; i < sizeof(capyai_legacy_specs) / sizeof(capyai_legacy_specs[0]);
         ++i) {
        const struct capyai_legacy_dispatch_spec *spec = &capyai_legacy_specs[i];
        if (out->risk == spec->risk &&
            capyai_bounded_equal(out->action, sizeof(out->action), spec->action) &&
            capyai_bounded_equal(out->command, sizeof(out->command), spec->command)) {
            return spec;
        }
    }
    return NULL;
}

/* One bounded argv token only.  Whitespace, quoting, control bytes and shell
 * metacharacters are rejected instead of escaped.  Parent traversal is also
 * rejected so an authorized legacy action cannot silently broaden its path. */
static int capyai_legacy_target_safe(const char *target, size_t capacity) {
    size_t i;
    size_t segment = 0u;
    if (!target || capacity == 0u || target[0] == '\0' || target[0] == '-') {
        return 0;
    }
    for (i = 0u; i < capacity; ++i) {
        unsigned char c = (unsigned char)target[i];
        if (c == 0u) {
            if (i - segment == 2u && target[segment] == '.' &&
                target[segment + 1u] == '.') return 0;
            return 1;
        }
        if (c == '/') {
            if (i - segment == 2u && target[segment] == '.' &&
                target[segment + 1u] == '.') return 0;
            segment = i + 1u;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.' || c == ':')) {
            return 0;
        }
    }
    return 0;
}

int capyai_legacy_dispatch_valid(const struct capy_ai_output *out) {
    const struct capyai_legacy_dispatch_spec *spec = capyai_find_legacy_spec(out);
    int has_target;
    if (!spec) return 0;
    has_target = out->path[0] != '\0';
    if (spec->target_mode == CAPYAI_LEGACY_TARGET_NONE) return !has_target;
    if (!has_target) return spec->target_mode == CAPYAI_LEGACY_TARGET_OPTIONAL;
    return capyai_legacy_target_safe(out->path, sizeof(out->path));
}

int capyai_typed_dispatch_valid(const struct capy_ai_output *out) {
    const struct capyai_typed_dispatch_spec *spec = capyai_find_typed_spec(out);
    int has_target;
    if (!spec) return 0;
    has_target = out->path[0] != '\0';
    if (spec->target_mode == CAPYAI_LEGACY_TARGET_NONE && has_target) return 0;
    if (spec->target_mode == CAPYAI_LEGACY_TARGET_REQUIRED &&
        (!has_target || !capyai_legacy_target_safe(out->path,
                                                   sizeof(out->path)))) return 0;
    if (spec->target_mode == CAPYAI_LEGACY_TARGET_OPTIONAL && has_target &&
        !capyai_legacy_target_safe(out->path, sizeof(out->path))) return 0;
    switch ((enum capyai_typed_secondary_mode)spec->secondary_mode) {
    case CAPYAI_TYPED_SECONDARY_NONE:
        return out->content[0] == '\0';
    case CAPYAI_TYPED_SECONDARY_TEXT_OPTIONAL:
        return capyai_bounded_text_safe(out->content, sizeof(out->content), 0);
    case CAPYAI_TYPED_SECONDARY_TEXT_REQUIRED:
        return capyai_bounded_text_safe(out->content, sizeof(out->content), 1);
    case CAPYAI_TYPED_SECONDARY_PATH_REQUIRED:
        return capyai_legacy_target_safe(out->content, sizeof(out->content));
    default:
        return 0;
    }
}

static int capyai_action_updates_last_file(const char *action) {
    static const char *const actions[] = {
        "file_create", "file_edit_text", "file_move", "dir_names_to_file",
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

static const char *capyai_last_file_result_path(
    const struct capy_ai_output *out, const struct capyai_exec_result *result,
    char *resolved, size_t resolved_size) {
    static const char moved_prefix[] = "arquivo movido: ";
    size_t i, n, basename = 0u, destination_name = 0u;
    int destination_is_file = 0;
    if (!out) return NULL;
    if (!capyai_bounded_equal(out->action, sizeof(out->action), "file_move")) {
        return out->path;
    }
    /* The native adapter reports the canonical verified destination.  Prefer
     * that result over inferring file-vs-directory from punctuation. */
    if (result) {
        for (i = 0u; moved_prefix[i] &&
                     result->detail[i] == moved_prefix[i]; ++i) { }
        if (!moved_prefix[i] && result->detail[i]) return &result->detail[i];
    }
    if (!resolved || resolved_size == 0u) return out->content;
    for (i = 0u; out->path[i]; ++i) {
        if (out->path[i] == '/' || out->path[i] == '\\') basename = i + 1u;
    }
    for (i = 0u; out->content[i]; ++i) {
        if (out->content[i] == '/' || out->content[i] == '\\') {
            destination_name = i + 1u;
            destination_is_file = 0;
        } else if (out->content[i] == '.' && i > destination_name) {
            destination_is_file = 1;
        }
    }
    n = 0u;
    for (i = 0u; out->content[i] && n + 1u < resolved_size; ++i) {
        resolved[n++] = out->content[i];
    }
    if (!destination_is_file && n > 0u && n + 1u < resolved_size) {
        if (resolved[n - 1u] != '/' && resolved[n - 1u] != '\\') {
            resolved[n++] = '/';
        }
        for (i = basename; out->path[i] && n + 1u < resolved_size; ++i) {
            resolved[n++] = out->path[i];
        }
    }
    resolved[n] = '\0';
    return resolved;
}

static int capyai_build_command_line(const struct capyai_plan *plan,
                                     char *line, size_t line_size) {
    size_t n = 0u;
    size_t i;
    if (!plan || !line || line_size == 0u || !plan->out.command[0]) return -1;
    for (i = 0u; i < sizeof(plan->out.command) && plan->out.command[i] &&
                 n + 1u < line_size; ++i) {
        line[n++] = plan->out.command[i];
    }
    if (i == sizeof(plan->out.command) || plan->out.command[i]) return -1;
    if (plan->out.path[0]) {
        if (n + 2u > line_size) return -1;
        line[n++] = ' ';
        for (i = 0u; i < sizeof(plan->out.path) && plan->out.path[i] &&
                     n + 1u < line_size; ++i) {
            line[n++] = plan->out.path[i];
        }
        if (i == sizeof(plan->out.path) || plan->out.path[i]) return -1;
    }
    line[n] = '\0';
    return 0;
}

static int capyai_execute_intent_internal(
    const char *model_text, size_t model_len,
    const char *intent, const char *platform, const char *shell,
    const struct capyai_perms *perms, struct capyai_session *session,
    capyai_dispatch_fn dispatch, void *dispatch_ctx,
    capyai_typed_dispatch_fn typed_dispatch, void *typed_dispatch_ctx,
    int typed_enabled, struct capyai_plan *plan,
    struct capyai_exec_result *result) {
    char command_line[CAPYAI_COMMAND_MAX];
    char resolved_last_file[CAPYAI_PATH_MAX];
    const char *last_file;
    int rc;
    if (!plan || !result || !intent) return -1;
    capyai_zero(result, sizeof(*result));
    rc = capyai_plan(model_text, model_len, intent, platform, shell, perms,
                     session, plan);
    if (rc != 0) return rc;

    if (plan->decision == CAPYAI_DECISION_ALLOWED && typed_enabled &&
        capyai_action_is_typed(&plan->out)) {
        if (!capyai_typed_dispatch_valid(&plan->out) || !typed_dispatch) {
            plan->decision = CAPYAI_DECISION_INVALID;
            capyai_copy(plan->reason, sizeof(plan->reason),
                        "Saida tipada invalida ou ferramenta nativa indisponivel.");
            result->rc = 127;
        } else {
            result->executed = 1;
            result->rc = typed_dispatch(typed_dispatch_ctx, &plan->out,
                                        result->detail,
                                        sizeof(result->detail));
            if (result->rc < 0) result->rc = 127;
            last_file = capyai_last_file_result_path(
                &plan->out, result, resolved_last_file,
                sizeof(resolved_last_file));
            if (result->rc == 0 && session && last_file && last_file[0] &&
                capyai_action_updates_last_file(plan->out.action)) {
                capyai_session_note_file(session, last_file);
            }
        }
    } else if (plan->decision == CAPYAI_DECISION_ALLOWED && dispatch) {
        if (!capyai_legacy_dispatch_valid(&plan->out)) {
            plan->decision = CAPYAI_DECISION_INVALID;
            capyai_copy(plan->reason, sizeof(plan->reason),
                        "Saida do modelo nao corresponde a uma ferramenta segura.");
            result->rc = 127;
        } else if (capyai_build_command_line(plan, command_line,
                                             sizeof(command_line)) != 0) {
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

int capyai_execute_intent(const char *model_text, size_t model_len,
                          const char *intent, const char *platform,
                          const char *shell,
                          const struct capyai_perms *perms,
                          struct capyai_session *session,
                          capyai_dispatch_fn dispatch, void *dispatch_ctx,
                          struct capyai_plan *plan,
                          struct capyai_exec_result *result) {
    return capyai_execute_intent_internal(
        model_text, model_len, intent, platform, shell, perms, session,
        dispatch, dispatch_ctx, NULL, NULL, 0, plan, result);
}

int capyai_execute_intent_v2(
    const char *model_text, size_t model_len,
    const char *intent, const char *platform, const char *shell,
    const struct capyai_perms *perms, struct capyai_session *session,
    capyai_dispatch_fn legacy_dispatch, void *legacy_dispatch_ctx,
    capyai_typed_dispatch_fn typed_dispatch, void *typed_dispatch_ctx,
    struct capyai_plan *plan, struct capyai_exec_result *result) {
    return capyai_execute_intent_internal(
        model_text, model_len, intent, platform, shell, perms, session,
        legacy_dispatch, legacy_dispatch_ctx, typed_dispatch,
        typed_dispatch_ctx, 1, plan, result);
}
