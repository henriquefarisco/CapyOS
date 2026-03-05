#include "core/login_runtime.h"

static int ops_ready(const struct login_runtime_ops *ops) {
  return ops && ops->shell_ctx && ops->session_ctx && ops->settings &&
         ops->prepare_shell_runtime && ops->init_shell_context_user &&
         ops->dispatch_shell_command && ops->run_shell_alias && ops->is_equal &&
         ops->readline && ops->session_reset && ops->session_set_active &&
         ops->shell_context_init && ops->system_login && ops->session_user &&
         ops->session_cwd && ops->shell_context_should_logout && ops->print &&
         ops->putc && ops->clear_view && ops->ui_banner && ops->cmd_info;
}

int login_runtime_run(struct login_runtime_ops *ops) {
  int first_login_screen = 1;
  char line[128];

  if (!ops_ready(ops)) {
    return -1;
  }

  if (ops->prepare_shell_runtime() != 0) {
    ops->print("[erro] Falha ao preparar runtime do shell.\n");
    return -1;
  }

  for (;;) {
    if (first_login_screen) {
      ops->clear_view();
      ops->ui_banner();
      first_login_screen = 0;
    }
    ops->print("\n");
    ops->print("========================================\n");
    ops->print("             CapyOS 64-bit             \n");
    ops->print("========================================\n");
    ops->print("\n");

    if (!ops->has_any_input) {
      ops->print("[!] Sem dispositivo de entrada disponivel.\n\n");
    }

    ops->session_reset(ops->session_ctx);
    ops->session_set_active(NULL);
    ops->shell_context_init(ops->shell_ctx, ops->session_ctx, ops->settings);

    if (ops->system_login(ops->session_ctx, ops->settings) != 0) {
      ops->print("[erro] Falha no fluxo de autenticacao.\n");
      return -1;
    }

    {
      const struct user_record *login_user = ops->session_user(ops->session_ctx);
      if (!login_user || !login_user->username[0] ||
          ops->init_shell_context_user(login_user) != 0) {
        ops->print("[erro] Falha ao ativar sessao autenticada.\n");
        ops->print("Retornando para a tela de login.\n");
        continue;
      }
    }

    for (;;) {
      const struct user_record *active_user = ops->session_user(ops->session_ctx);
      const char *name =
          (active_user && active_user->username[0]) ? active_user->username
                                                     : "user";
      const char *host =
          ops->settings->hostname[0] ? ops->settings->hostname : "capy64";
      const char *cwd = ops->session_cwd(ops->session_ctx);

      ops->print(name);
      ops->putc('@');
      ops->print(host);
      ops->putc(':');
      ops->print(cwd);
      ops->print("> ");

      ops->readline(line, sizeof(line), 0);
      if (!line[0]) {
        continue;
      }

      if (ops->dispatch_shell_command(line)) {
        if (ops->shell_context_should_logout(ops->shell_ctx)) {
          ops->session_reset(ops->session_ctx);
          ops->session_set_active(NULL);
          ops->clear_view();
          ops->ui_banner();
          break;
        }
        continue;
      }

      if (ops->is_equal(line, "help")) {
        (void)ops->run_shell_alias("help-any");
        continue;
      }
      if (ops->is_equal(line, "clear")) {
        (void)ops->run_shell_alias("mess");
        continue;
      }
      if (ops->is_equal(line, "reboot")) {
        (void)ops->run_shell_alias("shutdown-reboot");
        continue;
      }
      if (ops->is_equal(line, "halt")) {
        (void)ops->run_shell_alias("shutdown-off");
        continue;
      }
      if (ops->is_equal(line, "info")) {
        ops->cmd_info();
        continue;
      }

      ops->print("Comando desconhecido: ");
      ops->print(line);
      ops->print("\nUse 'help-any' para listar comandos.\n");
    }
  }
}
