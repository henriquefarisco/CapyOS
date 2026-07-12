/*
 * capyai — terminal front-end for the CapyAI assistant.
 *
 * Usage:
 *   capyai <pedido em linguagem natural>     one-shot
 *   capyai                                   interactive chat (sair para sair)
 * Flags (anywhere in argv):
 *   --yes                 libera acoes de escrita (write_file)
 *   --allow-delete        libera acoes de deletar
 *   --allow-system-change libera mudancas de sistema
 *
 * Pipeline: intent -> capyai_plan() (capy-ai-core inference + risk gates) ->
 * if ALLOWED, dispatch the predicted capysh command through the normal shell
 * command runner -> capyai_summary() prints a PT-BR summary.
 *
 * When the CapyAI sibling core is absent at build time the command compiles to
 * a stub that explains the module is unavailable (mirrors the desktop gate).
 */
#include "shell/commands.h"
#include "shell/core.h"

#ifdef CAPYOS_HAVE_CAPYAI

#include "services/capyai.h"
#include "drivers/console/tty.h"
#ifndef CAPYOS_PROFILE_CORE_ONLY
#include "apps/capyai_chat.h"
#include "gui/desktop_runtime.h"
#endif

/*
 * Built-in default model (capy-ai-core v0 artifact, keyword naive-Bayes over
 * CapyOS-native commands). Slice 4 replaces this with a trained model shipped
 * by the CapyAI sibling. All labels share prior/negdenom; each keyword adds a
 * fixed positive delta, so argmax = the label whose keywords best match.
 */
#ifndef CAPYOS_CAPYAI_EMBED_MODEL
static const char CAPYAI_DEFAULT_MODEL[] =
    "capyaicore 0\n"
    "scale 1000000\n"
    "labels 10\n"
    /* L0 list files (read_only) */
    "L 0\nprior -100000\nnegdenom -1000000\naction shell_command\ncommand list\n"
    "risk read_only\npath\nclar 0\nquestion\n"
    "T -100000 listar\nT -100000 liste\nT -100000 arquivos\nT -100000 lista\n"
    "T -100000 mostrar\nT -100000 conteudo\nE\n"
    /* L1 current path (read_only) */
    "L 1\nprior -100000\nnegdenom -1000000\naction shell_command\ncommand mypath\n"
    "risk read_only\npath\nclar 0\nquestion\n"
    "T -100000 onde\nT -100000 estou\nT -100000 caminho\nT -100000 atual\n"
    "T -100000 local\nE\n"
    /* L2 packages (read_only) */
    "L 2\nprior -100000\nnegdenom -1000000\naction shell_command\ncommand pkg-list\n"
    "risk read_only\npath\nclar 0\nquestion\n"
    "T -100000 pacotes\nT -100000 pacote\nT -100000 instalados\nT -100000 modulos\n"
    "T -100000 aplicativos\nT -100000 programas\nE\n"
    /* L3 users (read_only) */
    "L 3\nprior -100000\nnegdenom -1000000\naction shell_command\ncommand list-users\n"
    "risk read_only\npath\nclar 0\nquestion\n"
    "T -100000 usuarios\nT -100000 usuario\nT -100000 contas\nT -100000 quem\nE\n"
    /* L4 network (read_only) */
    "L 4\nprior -100000\nnegdenom -1000000\naction shell_command\ncommand net-status\n"
    "risk read_only\npath\nclar 0\nquestion\n"
    "T -100000 rede\nT -100000 internet\nT -100000 conexao\nT -100000 ip\n"
    "T -100000 conectado\nE\n"
    /* L5 clear screen (read_only) */
    "L 5\nprior -100000\nnegdenom -1000000\naction shell_command\ncommand mess\n"
    "risk read_only\npath\nclar 0\nquestion\n"
    "T -100000 limpar\nT -100000 limpe\nT -100000 limpa\nT -100000 tela\nE\n"
    /* L6 help (read_only) */
    "L 6\nprior -100000\nnegdenom -1000000\naction shell_command\ncommand help-any\n"
    "risk read_only\npath\nclar 0\nquestion\n"
    "T -100000 ajuda\nT -100000 comandos\nT -100000 socorro\nT -100000 disponiveis\nE\n"
    /* L7 make dir (write_file) */
    "L 7\nprior -100000\nnegdenom -1000000\naction dir_create\ncommand mkdir\n"
    "risk write_file\npath\nclar 0\nquestion\n"
    "T -100000 criar\nT -100000 cria\nT -100000 crie\nT -100000 pasta\n"
    "T -100000 diretorio\nT -100000 nova\nE\n"
    /* L8 make file (write_file) */
    "L 8\nprior -100000\nnegdenom -1000000\naction file_create\ncommand touch\n"
    "risk write_file\npath\nclar 0\nquestion\n"
    "T -100000 criar\nT -100000 cria\nT -100000 crie\nT -100000 arquivo\n"
    "T -100000 texto\nT -100000 novo\nE\n"
    /* L9 delete file (write_file + delete gate) */
    "L 9\nprior -100000\nnegdenom -1000000\naction file_delete\ncommand kill-file\n"
    "risk write_file\npath\nclar 0\nquestion\n"
    "T -100000 deletar\nT -100000 apagar\nT -100000 excluir\nT -100000 remova\nE\n";
#endif /* !CAPYOS_CAPYAI_EMBED_MODEL */

static struct capyai_session g_capyai_session;

#ifdef CAPYOS_CAPYAI_EMBED_MODEL
/* Trained CapyOS model artifact embedded from ../CapyAI/artifacts by the
 * Makefile (objcopy -> .rodata). Accessed RIP-relative like embedded_progs. */
extern const uint8_t _binary_capyai_model_start[];
extern const uint8_t _binary_capyai_model_end[];
const char *capyai_builtin_model(size_t *out_len) {
    const uint8_t *start;
    const uint8_t *end;
    __asm__ volatile("lea _binary_capyai_model_start(%%rip), %0" : "=r"(start));
    __asm__ volatile("lea _binary_capyai_model_end(%%rip), %0" : "=r"(end));
    if (out_len) *out_len = (size_t)(end - start);
    return (const char *)start;
}
#else
const char *capyai_builtin_model(size_t *out_len) {
    if (out_len) *out_len = sizeof(CAPYAI_DEFAULT_MODEL) - 1u;
    return CAPYAI_DEFAULT_MODEL;
}
#endif

/* Dispatch an allowed command line through the normal shell command runner.
 * Returns the command's handler rc, or -1 if the command is unknown. */
static int capyai_dispatch(void *opaque, const char *command) {
    struct shell_context *ctx = (struct shell_context *)opaque;
    char line[192];
    char *argv[SHELL_MAX_ARGS];
    int argc;
    const struct shell_command *cmd;
    size_t i = 0u;
    for (; command[i] && i + 1u < sizeof(line); ++i) line[i] = command[i];
    line[i] = '\0';
    argc = shell_parse_line(line, argv, SHELL_MAX_ARGS);
    if (argc == 0) return -1;
    cmd = shell_find_command(argv[0]);
    if (!cmd) return -1;
    return cmd->handler(ctx, argc, argv);
}

static void capyai_process(struct shell_context *ctx, const char *intent,
                           const struct capyai_perms *perms) {
    struct capyai_plan plan;
    struct capyai_exec_result result;
    char summary[CAPYAI_SUMMARY_MAX];
    size_t mlen = 0u;
    const char *model = capyai_builtin_model(&mlen);
    (void)capyai_execute_intent(model, mlen, intent, "capy", "capysh", perms,
                                &g_capyai_session, capyai_dispatch, ctx,
                                &plan, &result);
    capyai_summary(&plan, &result, summary, sizeof(summary));
    shell_print("[capyai] ");
    shell_print(summary);
    shell_newline();
}

static void capyai_collect(int argc, char **argv, struct capyai_perms *perms,
                           char *intent, size_t intent_size) {
    size_t len = 0u;
    intent[0] = '\0';
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (shell_string_equal(a, "--yes")) { perms->allow_write = 1; continue; }
        if (shell_string_equal(a, "--allow-delete")) { perms->allow_delete = 1; continue; }
        if (shell_string_equal(a, "--allow-system-change")) { perms->allow_system_change = 1; continue; }
        /* append this token to the intent */
        if (len > 0u && len + 1u < intent_size) intent[len++] = ' ';
        for (size_t j = 0u; a[j] && len + 1u < intent_size; ++j) intent[len++] = a[j];
        intent[len] = '\0';
    }
}

static int cmd_capyai(struct shell_context *ctx, int argc, char **argv) {
    struct capyai_perms perms = {0, 0, 0};
    char intent[256];

    if (shell_help_requested(argc, argv)) {
        shell_print("Uso: capyai <pedido>            (uma pergunta)\n");
        shell_print("     capyai                     (chat interativo; 'sair' encerra)\n");
        shell_print("Flags: --yes (escrita), --allow-delete, --allow-system-change\n");
        return 0;
    }

    capyai_collect(argc, argv, &perms, intent, sizeof(intent));

    if (intent[0] != '\0') {
        capyai_process(ctx, intent, &perms);
        return 0;
    }

#ifndef CAPYOS_PROFILE_CORE_ONLY
    /* The graphical terminal invokes command handlers synchronously from a
     * window callback while the compositor frame guard is held. Entering the
     * blocking TTY readline loop here would prevent that callback from ever
     * returning and pin the entire desktop. Open/focus the graphical chat and
     * return instead; interactive readline remains valid on the text console. */
    if (desktop_is_active()) {
        if (desktop_launch_capyai() != 0) {
            shell_print_error("CapyAI nao abriu: memoria de superficie insuficiente.");
            return 1;
        }
        shell_print("CapyAI grafico aberto.\n");
        return 0;
    }
#endif

    /* interactive chat */
    shell_print("CapyAI: assistente do sistema. Escreva um pedido ('sair' encerra).\n");
    for (;;) {
        char line[256];
        size_t n;
        shell_print("capyai> ");
        n = tty_readline(line, sizeof(line));
        if (n == 0u) continue;
        if (shell_string_equal(line, "sair") || shell_string_equal(line, "exit") ||
            shell_string_equal(line, "bye")) {
            break;
        }
        capyai_process(ctx, line, &perms);
    }
    return 0;
}

/* ── boot self-test (smoke) ────────────────────────────────────────────
 * Emits markers to the kernel debug console (port 0xE9) proving the full
 * pipeline runs at runtime: inference (capy-ai-core) + risk gates + PT-BR
 * summary. Driven by the `smoke-x64-capyai` target (-DCAPYOS_SMOKE_CAPYAI).
 * Self-contained port write so it needs no arch-internal header. */
static void capyai_dbg(const char *s) {
    for (; s && *s; ++s) {
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)*s), "Nd"((uint16_t)0xE9));
    }
}

void capyai_selftest_run(void) {
    struct capyai_plan plan;
    struct capyai_perms none = {0, 0, 0};
    struct capyai_perms wr = {1, 0, 0};
    struct capyai_session sess;
    char summary[CAPYAI_SUMMARY_MAX];
    size_t mlen = 0u;
    const char *model = capyai_builtin_model(&mlen);
    size_t i;
    for (i = 0u; i < sizeof(sess); ++i) ((char *)&sess)[i] = 0;

    capyai_dbg("[capyai-smoke] begin\n");

    /* read-only intent -> ALLOWED regardless of perms, command = list */
    capyai_plan(model, mlen, "liste os arquivos da pasta",
                "capy", "capysh", &none, &sess, &plan);
    capyai_dbg("[capyai-smoke] ro command=");
    capyai_dbg(plan.out.command);
    capyai_dbg(plan.decision == CAPYAI_DECISION_ALLOWED ? " decision=allowed\n"
                                                        : " decision=other\n");
    capyai_summary(&plan, NULL, summary, sizeof(summary));
    capyai_dbg("[capyai-smoke] summary=");
    capyai_dbg(summary);
    capyai_dbg("\n");

    /* write intent, no perms -> BLOCKED_RISK */
    capyai_plan(model, mlen, "criar uma pasta chamada teste",
                "capy", "capysh", &none, &sess, &plan);
    capyai_dbg(plan.decision == CAPYAI_DECISION_BLOCKED_RISK
                   ? "[capyai-smoke] write-noperm=blocked\n"
                   : "[capyai-smoke] write-noperm=other\n");

    /* write intent, --yes -> ALLOWED, command = mkdir */
    capyai_plan(model, mlen, "criar uma pasta chamada teste",
                "capy", "capysh", &wr, &sess, &plan);
    capyai_dbg("[capyai-smoke] write-perm command=");
    capyai_dbg(plan.out.command);
    capyai_dbg(plan.decision == CAPYAI_DECISION_ALLOWED ? " decision=allowed\n"
                                                        : " decision=other\n");
    /* v1 slot extraction: "chamada teste" -> path "teste" */
    capyai_dbg("[capyai-smoke] slot path=");
    capyai_dbg(plan.out.path[0] ? plan.out.path : "(none)");
    capyai_dbg("\n");

    capyai_dbg("[capyai-smoke] OK\n");
}

#else /* !CAPYOS_HAVE_CAPYAI */

static int cmd_capyai(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    (void)argc;
    (void)argv;
    shell_print_error("Modulo de IA (capyai) nao esta disponivel nesta build.");
    shell_print("Compile com o repo irmao ../CapyAI presente para habilitar.\n");
    return 1;
}

#endif /* CAPYOS_HAVE_CAPYAI */

int capyai_command_register(struct shell_command *slot) {
    if (!slot) return -1;
    slot->name = "capyai";
    slot->handler = cmd_capyai;
    return 0;
}
