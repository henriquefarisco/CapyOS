/*
 * capyai_summary — PT-BR natural-language outcome text + session helpers.
 * Pure logic (no allocation/globals); host-testable.
 */
#include "services/capyai.h"

static void cs_append(char *dst, size_t dst_size, size_t *len, const char *src) {
    if (!dst || dst_size == 0u) return;
    while (src && *src && *len + 1u < dst_size) {
        dst[(*len)++] = *src++;
    }
    dst[*len] = '\0';
}

static void cs_append_int(char *dst, size_t dst_size, size_t *len, int v) {
    char tmp[12];
    int i = 0;
    int neg = v < 0;
    unsigned int u = neg ? (unsigned int)(-(long)v) : (unsigned int)v;
    if (u == 0u) { cs_append(dst, dst_size, len, "0"); return; }
    while (u > 0u && i < (int)sizeof(tmp)) { tmp[i++] = (char)('0' + (u % 10u)); u /= 10u; }
    if (neg) cs_append(dst, dst_size, len, "-");
    while (i > 0) {
        char c[2];
        c[0] = tmp[--i];
        c[1] = '\0';
        cs_append(dst, dst_size, len, c);
    }
}

void capyai_session_note_file(struct capyai_session *session, const char *path) {
    size_t i = 0u;
    if (!session || !path || path[0] == '\0') return;
    for (; path[i] && i + 1u < CAPYAI_PATH_MAX; ++i) session->last_file[i] = path[i];
    session->last_file[i] = '\0';
}

void capyai_summary(const struct capyai_plan *plan,
                    const struct capyai_exec_result *result,
                    char *buf, size_t buf_size) {
    size_t len = 0u;
    const char *command;
    if (!buf || buf_size == 0u) return;
    buf[0] = '\0';
    if (!plan) { cs_append(buf, buf_size, &len, "Sem plano."); return; }
    command = plan->out.command[0] ? plan->out.command : "(sem comando)";

    switch (plan->decision) {
    case CAPYAI_DECISION_NEEDS_CLARIFICATION:
        cs_append(buf, buf_size, &len, "Preciso de mais detalhes: ");
        cs_append(buf, buf_size, &len, plan->reason);
        return;
    case CAPYAI_DECISION_BLOCKED_DENYLIST:
        cs_append(buf, buf_size, &len, "Bloqueado por seguranca: ");
        cs_append(buf, buf_size, &len, plan->reason);
        return;
    case CAPYAI_DECISION_BLOCKED_RISK:
        cs_append(buf, buf_size, &len, "Nao executei \"");
        cs_append(buf, buf_size, &len, command);
        cs_append(buf, buf_size, &len, "\". ");
        cs_append(buf, buf_size, &len, plan->reason);
        return;
    case CAPYAI_DECISION_INVALID:
        cs_append(buf, buf_size, &len, plan->reason[0] ? plan->reason
                                                       : "Nao entendi o pedido.");
        return;
    case CAPYAI_DECISION_ALLOWED:
    default:
        break;
    }

    if (!result || !result->executed) {
        cs_append(buf, buf_size, &len, "Plano pronto: \"");
        cs_append(buf, buf_size, &len, command);
        cs_append(buf, buf_size, &len, "\" (risco ");
        cs_append(buf, buf_size, &len, capy_ai_risk_to_str(plan->out.risk));
        cs_append(buf, buf_size, &len, ").");
        return;
    }
    if (result->rc == 0) {
        cs_append(buf, buf_size, &len, "Executei \"");
        cs_append(buf, buf_size, &len, command);
        cs_append(buf, buf_size, &len, "\" com sucesso.");
        if (result->detail[0]) {
            cs_append(buf, buf_size, &len, " ");
            cs_append(buf, buf_size, &len, result->detail);
        }
        return;
    }
    cs_append(buf, buf_size, &len, "Executei \"");
    cs_append(buf, buf_size, &len, command);
    cs_append(buf, buf_size, &len, "\", mas falhou (codigo ");
    cs_append_int(buf, buf_size, &len, result->rc);
    cs_append(buf, buf_size, &len, ").");
    if (result->detail[0]) {
        cs_append(buf, buf_size, &len, " Motivo: ");
        cs_append(buf, buf_size, &len, result->detail);
    }
}
