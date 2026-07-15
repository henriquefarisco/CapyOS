/*
 * capyai_plan — risk-gated planning over a capy-ai-core prediction.
 *
 * Pure logic (no VFS, no allocation, no globals): host-testable. Mirrors
 * CapyAI's executor.can_execute + is_blocked_command + low-confidence-write
 * clarification, adapted to CapyOS permission toggles.
 */
#include "services/capyai.h"

/* ---- tiny local helpers (no libc / no kernel deps) -------------------- */

static size_t cap_slen(const char *s) {
    size_t n = 0u;
    while (s && s[n]) ++n;
    return n;
}

static void cap_copy(char *dst, size_t dst_size, const char *src) {
    size_t i = 0u;
    if (!dst || dst_size == 0u) return;
    for (; src && src[i] && i + 1u < dst_size; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

static char cap_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* case-insensitive substring search */
static int cap_icontains(const char *hay, const char *needle) {
    size_t hn = cap_slen(hay);
    size_t nn = cap_slen(needle);
    if (nn == 0u) return 1;
    if (hn < nn) return 0;
    for (size_t i = 0u; i + nn <= hn; ++i) {
        size_t j = 0u;
        for (; j < nn; ++j) {
            if (cap_lower(hay[i + j]) != cap_lower(needle[j])) break;
        }
        if (j == nn) return 1;
    }
    return 0;
}

static int cap_streq(const char *a, const char *b) {
    size_t i = 0u;
    for (; a && b && a[i] && b[i]; ++i) {
        if (a[i] != b[i]) return 0;
    }
    return a && b && a[i] == b[i];
}

/* ---- denylist + action classification --------------------------------- */

int capyai_command_is_blocked(const char *command) {
    /* Hard-blocked destructive fragments; never runnable regardless of gates.
     * Kept CapyOS-oriented plus the cross-platform ones CapyAI already knew. */
    static const char *const frags[] = {
        "rm -rf", "rm -fr", "mkfs", "dd if=", "> /dev/", ":(){", "fork bomb",
        "format-volume", "remove-item -recurse", "del /s", "sudo rm",
        "vfs_rmdir_recursive /", "capy volume wipe", NULL,
    };
    if (!command) return 0;
    for (size_t i = 0u; frags[i]; ++i) {
        if (cap_icontains(command, frags[i])) return 1;
    }
    return 0;
}

static int cap_action_is_delete(const char *action) {
    return cap_streq(action, "file_delete") || cap_streq(action, "dir_delete");
}

static int cap_action_writes_last_file(const char *action) {
    /* actions that operate on / update the session's implicit last file */
    return cap_streq(action, "file_edit_text") || cap_streq(action, "file_create") ||
           cap_streq(action, "file_move") || cap_streq(action, "dir_names_to_file") ||
           cap_streq(action, "dir_count_to_file");
}

static int cap_action_requires_path(const char *action) {
    return cap_streq(action, "dir_create") || cap_streq(action, "file_create") ||
           cap_streq(action, "file_edit_text") || cap_streq(action, "file_move") ||
           cap_streq(action, "file_read") || cap_streq(action, "file_delete") ||
           cap_streq(action, "dir_delete") || cap_streq(action, "dir_find") ||
           cap_streq(action, "app_open") || cap_streq(action, "app_close") ||
           cap_streq(action, "power_schedule");
}

static void cap_mark_slot_clarification(struct capy_ai_output *out,
                                        const char *question) {
    if (!out) return;
    out->needs_clarification = 1;
    cap_copy(out->clarifying_question, sizeof(out->clarifying_question), question);
}

/* ---- planning --------------------------------------------------------- */

int capyai_plan(const char *model_text, size_t model_len,
                const char *intent, const char *platform, const char *shell,
                const struct capyai_perms *perms,
                const struct capyai_session *session,
                struct capyai_plan *plan) {
    struct capyai_perms zero_perms = {0, 0, 0};
    const struct capyai_perms *p = perms ? perms : &zero_perms;
    int rc;
    int is_delete;

    if (!plan) return -1;
    /* zero the plan */
    {
        char *b = (char *)plan;
        for (size_t i = 0u; i < sizeof(*plan); ++i) b[i] = 0;
    }
    plan->decision = CAPYAI_DECISION_INVALID;
    plan->out.matched_label = -1;

    rc = capy_ai_predict(model_text, model_len, intent, platform, shell, &plan->out);
    if (rc != CAPY_AI_OK) {
        cap_copy(plan->reason, sizeof(plan->reason),
                 "Nao consegui interpretar o pedido (modelo indisponivel ou vazio).");
        return 0;
    }

    /* capy-ai-core v1: extract argument slots (target name/path + content) from
     * the raw intent so the executor can run the function with real arguments. */
    capy_ai_fill_slots(intent, &plan->out);

    /* The portable slot ABI is bounded to CAPY_AI_STR_MAX.  Never execute a
     * mutating request whose original text exceeded that boundary, because a
     * path or payload could otherwise be silently truncated. */
    if (cap_slen(intent) >= CAPY_AI_STR_MAX &&
        plan->out.risk != CAPY_AI_RISK_READ_ONLY) {
        cap_mark_slot_clarification(
            &plan->out,
            "O pedido e longo demais; envie o caminho e o conteudo em uma mensagem menor.");
    }

    /* Resolve an implicit "last file" target for follow-up edits. */
    if (session && session->last_file[0] &&
        cap_action_writes_last_file(plan->out.action) && plan->out.path[0] == '\0') {
        cap_copy(plan->out.path, sizeof(plan->out.path), session->last_file);
    }

    /* Slot completeness is a deterministic policy boundary, not a model
     * guess. Missing file content or a move destination must ask only for the
     * absent value and can never fall through to an approximate command. */
    if ((cap_streq(plan->out.action, "app_open") ||
         cap_streq(plan->out.action, "app_close")) &&
        plan->out.path[0] == '\0') {
        cap_mark_slot_clarification(&plan->out,
                                    "Qual aplicativo devo abrir ou fechar?");
    } else if (cap_streq(plan->out.action, "power_schedule") &&
               plan->out.path[0] == '\0') {
        cap_mark_slot_clarification(&plan->out,
                                    "Para quando devo agendar o reinicio?");
    } else if (cap_action_requires_path(plan->out.action) &&
               plan->out.path[0] == '\0') {
        cap_mark_slot_clarification(&plan->out,
                                    "Qual e o arquivo, pasta ou caminho alvo?");
    } else if (cap_streq(plan->out.action, "file_edit_text") &&
               plan->out.content[0] == '\0') {
        cap_mark_slot_clarification(&plan->out,
                                    "Qual texto devo adicionar ao arquivo?");
    } else if (cap_streq(plan->out.action, "file_move") &&
               plan->out.content[0] == '\0') {
        cap_mark_slot_clarification(&plan->out,
                                    "Qual e o caminho de destino?");
    }

    if (plan->out.needs_clarification) {
        plan->decision = CAPYAI_DECISION_NEEDS_CLARIFICATION;
        cap_copy(plan->reason, sizeof(plan->reason),
                 plan->out.clarifying_question[0]
                     ? plan->out.clarifying_question
                     : "Preciso de mais detalhes para executar com seguranca.");
        return 0;
    }

    if (capyai_command_is_blocked(plan->out.command)) {
        plan->decision = CAPYAI_DECISION_BLOCKED_DENYLIST;
        cap_copy(plan->reason, sizeof(plan->reason),
                 "Comando bloqueado por seguranca (fragmento destrutivo).");
        return 0;
    }

    /* Delete actions need the dedicated delete permission on top of the risk
     * gate (they map to the "deletar" button). */
    is_delete = cap_action_is_delete(plan->out.action);
    if (is_delete && !p->allow_delete) {
        plan->decision = CAPYAI_DECISION_BLOCKED_RISK;
        cap_copy(plan->reason, sizeof(plan->reason),
                 "Requer permissao de deletar (--allow-delete / botao Deletar).");
        return 0;
    }

    switch (plan->out.risk) {
    case CAPY_AI_RISK_READ_ONLY:
        plan->decision = CAPYAI_DECISION_ALLOWED;
        cap_copy(plan->reason, sizeof(plan->reason), "Leitura permitida.");
        break;
    case CAPY_AI_RISK_WRITE_FILE:
    case CAPY_AI_RISK_NETWORK:
        if (p->allow_write) {
            plan->decision = CAPYAI_DECISION_ALLOWED;
            cap_copy(plan->reason, sizeof(plan->reason), "Escrita liberada.");
        } else {
            plan->decision = CAPYAI_DECISION_BLOCKED_RISK;
            cap_copy(plan->reason, sizeof(plan->reason),
                     "Requer liberar escrita (--yes / botao Escrita).");
        }
        break;
    case CAPY_AI_RISK_SYSTEM_CHANGE:
        if (p->allow_system_change) {
            plan->decision = CAPYAI_DECISION_ALLOWED;
            cap_copy(plan->reason, sizeof(plan->reason),
                     "Mudanca de sistema liberada.");
        } else {
            plan->decision = CAPYAI_DECISION_BLOCKED_RISK;
            cap_copy(plan->reason, sizeof(plan->reason),
                     "Requer --allow-system-change (mudanca de sistema).");
        }
        break;
    default:
        plan->decision = CAPYAI_DECISION_INVALID;
        cap_copy(plan->reason, sizeof(plan->reason), "Risco desconhecido.");
        break;
    }

    return 0;
}
