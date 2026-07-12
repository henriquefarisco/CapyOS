/*
 * Standalone host test for the CapyOS capyai executor adapter (planner +
 * summary + denylist + session follow-up), driven by a deterministic inline
 * capy-ai-core model so it does not depend on a trained artifact.
 *
 * Build (host):
 *   cc -std=c99 -Wall -Wextra -Werror -Iinclude -I../CapyAI/src/core \
 *      tests/services/test_capyai_standalone.c \
 *      src/services/capyai/capyai_plan.c src/services/capyai/capyai_summary.c \
 *      ../CapyAI/src/core/capy_ai_tokenize.c ../CapyAI/src/core/capy_ai_predict.c \
 *      ../CapyAI/src/core/capy_ai_slots.c \
 *      -o /tmp/test_capyai && /tmp/test_capyai
 */
#include "services/capyai.h"
#include "memory/kmem.h"

#include <stdio.h>
#include <string.h>

static const char MODEL[] =
    "capyaicore 0\n"
    "scale 1000000\n"
    "labels 3\n"
    "L 0\n"
    "prior -100000\n"
    "negdenom -500000\n"
    "action shell_command\n"
    "command ls\n"
    "risk read_only\n"
    "path\n"
    "clar 0\n"
    "question\n"
    "T -50000 listar\n"
    "T -50000 arquivos\n"
    "E\n"
    "L 1\n"
    "prior -100000\n"
    "negdenom -500000\n"
    "action dir_create\n"
    "command mkdir\n"
    "risk write_file\n"
    "path\n"
    "clar 0\n"
    "question\n"
    "T -50000 criar\n"
    "T -50000 pasta\n"
    "E\n"
    "L 2\n"
    "prior -100000\n"
    "negdenom -500000\n"
    "action file_edit_text\n"
    "command edit\n"
    "risk write_file\n"
    "path\n"
    "clar 0\n"
    "question\n"
    "T -50000 adicione\n"
    "T -50000 neste\n"
    "E\n";

static int g_fail = 0;
static int g_dispatch_calls = 0;
static int g_dispatch_rc = 0;
static char g_dispatch_line[CAPYAI_COMMAND_MAX];
#define CHECK(cond, msg) do { \
    if (cond) { printf("  ok   %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); g_fail++; } \
} while (0)

static int fake_dispatch(void *ctx, const char *command_line) {
    size_t i = 0u;
    (void)ctx;
    g_dispatch_calls++;
    for (; command_line && command_line[i] && i + 1u < sizeof(g_dispatch_line);
         ++i) {
        g_dispatch_line[i] = command_line[i];
    }
    g_dispatch_line[i] = '\0';
    return g_dispatch_rc;
}

int main(void) {
    struct capyai_plan plan;
    struct capyai_perms none = {0, 0, 0};
    struct capyai_perms write_ok = {1, 0, 0};
    struct capyai_session sess;
    char summary[CAPYAI_SUMMARY_MAX];
    size_t mlen = sizeof(MODEL) - 1u;

    memset(&sess, 0, sizeof(sess));

    printf("[test_capyai_standalone]\n");

    /* denylist */
    CHECK(capyai_command_is_blocked("rm -rf /home") == 1, "denylist blocks rm -rf");
    CHECK(capyai_command_is_blocked("capy volume WIPE now") == 1, "denylist ci-insensitive");
    CHECK(capyai_command_is_blocked("ls -la") == 0, "denylist allows ls");

    /* read_only always allowed, even with no perms */
    capyai_plan(MODEL, mlen, "listar arquivos do diretorio atual", "capy", "capysh",
                &none, &sess, &plan);
    CHECK(plan.decision == CAPYAI_DECISION_ALLOWED, "read_only allowed w/o perms");
    CHECK(strcmp(plan.out.command, "ls") == 0, "read_only picked label 0 (ls)");
    CHECK(plan.out.risk == CAPY_AI_RISK_READ_ONLY, "read_only risk parsed");

    /* write gated: blocked without perms, allowed with allow_write */
    capyai_plan(MODEL, mlen, "criar uma pasta chamada teste", "capy", "capysh",
                &none, &sess, &plan);
    CHECK(plan.decision == CAPYAI_DECISION_BLOCKED_RISK, "write blocked w/o perms");
    CHECK(strcmp(plan.out.command, "mkdir") == 0, "write picked label 1 (mkdir)");
    capyai_summary(&plan, NULL, summary, sizeof(summary));
    CHECK(strstr(summary, "Nao executei") != NULL, "blocked summary mentions refusal");

    capyai_plan(MODEL, mlen, "criar uma pasta chamada teste", "capy", "capysh",
                &write_ok, &sess, &plan);
    CHECK(plan.decision == CAPYAI_DECISION_ALLOWED, "write allowed with allow_write");
    CHECK(strcmp(plan.out.path, "teste") == 0, "v1 slot extraction: path=teste");
    capyai_summary(&plan, NULL, summary, sizeof(summary));
    CHECK(strstr(summary, "Plano pronto") != NULL, "allowed(not-exec) summary is a plan");
    CHECK(strstr(summary, "mkdir") != NULL, "summary contains the command");

    /* executed-ok and failed summaries */
    {
        struct capyai_exec_result r_ok = {1, 0, ""};
        struct capyai_exec_result r_bad = {1, 3, ""};
        capyai_summary(&plan, &r_ok, summary, sizeof(summary));
        CHECK(strstr(summary, "com sucesso") != NULL, "executed-ok summary");
        capyai_summary(&plan, &r_bad, summary, sizeof(summary));
        CHECK(strstr(summary, "codigo 3") != NULL, "executed-fail summary shows rc");
    }

    /* session follow-up: last file resolves an empty edit path */
    capyai_session_note_file(&sess, "artifacts/demo/notas.txt");
    capyai_plan(MODEL, mlen, "adicione neste arquivo novo o texto", "capy", "capysh",
                &write_ok, &sess, &plan);
    CHECK(strcmp(plan.out.action, "file_edit_text") == 0, "follow-up picked edit label");
    CHECK(strcmp(plan.out.path, "artifacts/demo/notas.txt") == 0,
          "follow-up resolved last_file into empty path");

    /* Shared executor: dispatch once, preserve real rc, apply gates and count
     * one session turn. This is the path shared by terminal and GUI. */
    {
        struct capyai_session exec_sess;
        struct capyai_exec_result exec_result;
        memset(&exec_sess, 0, sizeof(exec_sess));
        g_dispatch_calls = 0;
        g_dispatch_rc = 0;
        g_dispatch_line[0] = '\0';
        CHECK(capyai_execute_intent(
                  MODEL, mlen, "criar uma pasta chamada teste", "capy", "capysh",
                  &write_ok, &exec_sess, fake_dispatch, NULL, &plan,
                  &exec_result) == 0,
              "shared executor returns successfully");
        CHECK(g_dispatch_calls == 1, "shared executor dispatches exactly once");
        CHECK(strcmp(g_dispatch_line, "mkdir teste") == 0,
              "shared executor builds bounded command + slot");
        CHECK(exec_result.executed == 1 && exec_result.rc == 0,
              "shared executor reports handler result");
        CHECK(exec_sess.turns == 1u, "shared executor increments one turn");

        CHECK(capyai_execute_intent(
                  MODEL, mlen, "criar uma pasta chamada bloqueada", "capy", "capysh",
                  &none, &exec_sess, fake_dispatch, NULL, &plan,
                  &exec_result) == 0,
              "shared executor handles blocked plan");
        CHECK(g_dispatch_calls == 1 && exec_result.executed == 0,
              "blocked plan never reaches dispatcher");
        CHECK(exec_sess.turns == 2u, "blocked plan still counts one turn");

        g_dispatch_rc = 9;
        CHECK(capyai_execute_intent(
                  MODEL, mlen, "listar arquivos", "capy", "capysh", &none,
                  &exec_sess, fake_dispatch, NULL, &plan, &exec_result) == 0 &&
                  exec_result.rc == 9,
              "shared executor preserves failing handler rc");
    }

    /* 1080p contract: backbuffer + taskbar + initial terminal + CapyAI must
     * fit the default heap before allocator metadata/normal runtime reserve.
     * The 32 MiB default leaves roughly 16 MiB for those remaining users. */
    {
        const uint64_t screen_w = 1920u, screen_h = 1080u;
        const uint64_t taskbar_h = 32u;
        const uint64_t terminal_w = screen_w - 2u * 52u;
        const uint64_t terminal_h = screen_h - (2u * 44u + taskbar_h);
        const uint64_t base = screen_w * screen_h * 4u +
                              screen_w * taskbar_h * 4u +
                              terminal_w * terminal_h * 4u;
        const uint64_t capyai_surface = 640u * 500u * 4u;
        CHECK(base + capyai_surface < (uint64_t)KHEAP_DEFAULT_SIZE,
              "1080p desktop + CapyAI fits default heap");
        CHECK((uint64_t)KHEAP_DEFAULT_SIZE - base - capyai_surface >=
                  8u * 1024u * 1024u,
              "1080p CapyAI leaves at least 8 MiB runtime headroom");
    }

    if (g_fail == 0) {
        printf("[test_capyai_standalone] all passed\n");
        return 0;
    }
    printf("[test_capyai_standalone] %d FAILED\n", g_fail);
    return 1;
}
