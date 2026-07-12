/*
 * services/capyai.h — CapyOS in-tree adapter for the capy-ai-core ABI.
 *
 * This is the CapyOS-owned boundary that consumes the decoupled CapyAI
 * inference core (../CapyAI/src/core, ABI "capy-ai-core" v0). It mirrors the
 * capybrowse/capy-browser-core pattern: the sibling owns the model + portable
 * inference; CapyOS owns the executor that turns a prediction into a gated,
 * auditable action against the real system (VFS/shell/capypkg).
 *
 * Split of concerns:
 *   - capy_ai_predict() (sibling core)  -> intent => {action,command,risk,...}
 *   - capyai_plan()      (this adapter) -> apply risk gates + denylist +
 *                                          clarification + session memory
 *   - caller (shell/gfx)                -> dispatch an ALLOWED command to the
 *                                          existing capysh command runner, then
 *                                          report the outcome back
 *   - capyai_summary()   (this adapter) -> PT-BR natural-language summary
 *
 * The planning + summary layer is pure logic (no VFS, no allocation, no
 * globals) so it is fully host-testable. Real side effects run through the
 * caller's shell dispatch (mkdir/ls/cat/rm/capy ... already exist as gated
 * commands); the executor never touches the filesystem itself.
 */
#ifndef SERVICES_CAPYAI_H
#define SERVICES_CAPYAI_H

#include <stddef.h>
#include <stdint.h>

#include "capy_ai_core.h" /* ABI: -I ../CapyAI/src/core (Makefile sibling) */

#define CAPYAI_PATH_MAX 192u
#define CAPYAI_REASON_MAX 160u
#define CAPYAI_SUMMARY_MAX 320u
#define CAPYAI_COMMAND_MAX 256u

/* Canonical activation module name (mirrors module_gate.c). */
#define CAPYAI_MODULE_NAME "org.capyos.ai.assistant"

/*
 * Permission toggles. read_only always runs. These correspond to the three
 * graphical buttons ("liberar escrita", "edicao", "deletar") and the shell
 * flags (--yes, --allow-delete, --allow-system-change). Writes and edits share
 * one write gate (as in CapyAI's schema, both are risk write_file); delete and
 * system_change are separate, more dangerous gates.
 */
struct capyai_perms {
    int allow_write;          /* write_file + network (create/edit/append) */
    int allow_delete;         /* file/dir deletion */
    int allow_system_change;  /* system_change (pkg install/remove, etc.) */
};

/* One-session memory: last file created/edited (for "adicione neste arquivo"
 * style follow-ups) + a turn counter. Caller owns and persists it. */
struct capyai_session {
    char last_file[CAPYAI_PATH_MAX];
    uint32_t turns;
};

enum capyai_decision {
    CAPYAI_DECISION_ALLOWED = 0,
    CAPYAI_DECISION_NEEDS_CLARIFICATION,
    CAPYAI_DECISION_BLOCKED_RISK,      /* needs a permission not granted */
    CAPYAI_DECISION_BLOCKED_DENYLIST,  /* dangerous command fragment */
    CAPYAI_DECISION_INVALID,           /* no label / malformed model */
};

struct capyai_plan {
    struct capy_ai_output out;      /* raw prediction from the core */
    enum capyai_decision decision;
    char reason[CAPYAI_REASON_MAX]; /* short PT-BR explanation of the decision */
};

/*
 * Plan a request: run inference over `model_text`, apply the denylist + risk
 * gates against `perms`, honor a low-confidence-write clarification, and (when
 * the action targets an implicit "last file") resolve it from `session`.
 * Pure: no side effects. `session` may be NULL. Returns 0 always (the outcome
 * is in plan->decision); negative only on NULL required args.
 */
int capyai_plan(const char *model_text, size_t model_len,
                const char *intent, const char *platform, const char *shell,
                const struct capyai_perms *perms,
                const struct capyai_session *session,
                struct capyai_plan *plan);

/* Return 1 if `command` contains a hard-blocked destructive fragment. */
int capyai_command_is_blocked(const char *command);

/* Return the built-in default capy-ai-core model artifact (shared by the
 * terminal command and the graphical chat app). `*out_len` gets its byte
 * length. Defined in capyai_command.c; only when CAPYOS_HAVE_CAPYAI. */
const char *capyai_builtin_model(size_t *out_len);

/* Outcome the caller reports back after dispatching an allowed command. */
struct capyai_exec_result {
    int executed;      /* 1 if the command actually ran */
    int rc;            /* 0 ok, non-zero on command failure */
    char detail[CAPYAI_SUMMARY_MAX]; /* optional extra (path acted on, etc.) */
};

/* Existing shell/VFS execution remains owned by the caller.  The shared
 * executor invokes this callback only after inference, clarification,
 * denylist and permission gates all allow the request. */
typedef int (*capyai_dispatch_fn)(void *ctx, const char *command_line);

/* Shared plan + execute path used by both the terminal command and graphical
 * chat.  It builds the bounded command line, calls `dispatch` for ALLOWED
 * plans, updates `session`, and increments the turn counter exactly once.
 * A NULL dispatch performs a dry run. */
int capyai_execute_intent(const char *model_text, size_t model_len,
                          const char *intent, const char *platform,
                          const char *shell,
                          const struct capyai_perms *perms,
                          struct capyai_session *session,
                          capyai_dispatch_fn dispatch, void *dispatch_ctx,
                          struct capyai_plan *plan,
                          struct capyai_exec_result *result);

/*
 * Record the acted-on file into the session so follow-up edits ("adicione
 * neste arquivo novo ...") can target it without repeating the path.
 */
void capyai_session_note_file(struct capyai_session *session, const char *path);

/*
 * Compose a PT-BR natural-language summary of the outcome into `buf`.
 * `result` may be NULL when the plan was blocked / needs clarification.
 */
void capyai_summary(const struct capyai_plan *plan,
                    const struct capyai_exec_result *result,
                    char *buf, size_t buf_size);

#endif /* SERVICES_CAPYAI_H */
