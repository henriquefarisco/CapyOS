#include "core/login_runtime.h"
#include "core/localization.h"

static int ops_ready(const struct login_runtime_ops *ops) {
  return ops && ops->shell_ctx && ops->session_ctx && ops->settings &&
         ops->prepare_shell_runtime &&
         (!ops->maintenance_mode || ops->maintenance_session_start) &&
         ops->init_shell_context_user &&
         ops->dispatch_shell_command && ops->run_shell_alias && ops->is_equal &&
         ops->readline && ops->session_reset && ops->session_set_active &&
         ops->shell_context_init && ops->system_login && ops->session_user &&
         ops->session_cwd && ops->shell_context_should_logout && ops->print &&
         ops->putc && ops->clear_view && ops->show_splash && ops->ui_banner &&
         ops->cmd_info;
}

static void login_service_poll(struct login_runtime_ops *ops) {
  if (ops && ops->service_poll) {
    ops->service_poll();
  }
}

static void login_show_maintenance_notice(struct login_runtime_ops *ops,
                                          const char *language) {
  if (!ops || !ops->maintenance_mode) {
    return;
  }

  ops->print(localization_select(
      language,
      "[maintenance] Sistema iniciado em modo de recuperacao.\n",
      "[maintenance] System booted in recovery mode.\n",
      "[maintenance] Sistema iniciado en modo de recuperacion.\n"));
  if (ops->maintenance_reason && ops->maintenance_reason[0]) {
    ops->print(localization_select(language, "Motivo: ", "Reason: ",
                                   "Motivo: "));
    ops->print(ops->maintenance_reason);
    ops->putc('\n');
  }
  ops->print(localization_select(
      language,
      "Use service-status, service-target show, do-sync e shutdown-reboot para diagnostico e recuperacao.\n",
      "Use service-status, service-target show, do-sync and shutdown-reboot for diagnosis and recovery.\n",
      "Usa service-status, service-target show, do-sync y shutdown-reboot para diagnostico y recuperacion.\n"));
}

static void login_render_screen(struct login_runtime_ops *ops,
                                const char *language, int first_screen) {
  if (!ops) {
    return;
  }
  if (first_screen) {
    ops->show_splash(ops->settings);
  }
  ops->clear_view();
  ops->ui_banner();
  if (ops->maintenance_mode) {
    ops->print("\n");
    login_show_maintenance_notice(ops, language);
  }
}

int login_runtime_run(struct login_runtime_ops *ops) {
  int first_login_screen = 1;
  char line[128];
  const char *system_language = "en";

  if (!ops_ready(ops)) {
    return -1;
  }
  if (ops->settings && ops->settings->language[0]) {
    const char *normalized = localization_normalize_language(ops->settings->language);
    if (normalized) {
      system_language = normalized;
    }
  }

  if (ops->prepare_shell_runtime() != 0) {
    ops->print(localization_text_for(system_language,
                                     LOC_TEXT_PREPARE_SHELL_FAILED));
    return -1;
  }
  login_service_poll(ops);

  for (;;) {
    if (first_login_screen) {
      login_render_screen(ops, system_language, 1);
      first_login_screen = 0;
    }
    ops->print("\n");

    if (!ops->has_any_input) {
      ops->print(localization_text_for(system_language, LOC_TEXT_NO_INPUT_DEVICE));
    }

    ops->session_reset(ops->session_ctx);
    ops->session_set_active(NULL);
    ops->shell_context_init(ops->shell_ctx, ops->session_ctx, ops->settings);
    login_service_poll(ops);

    if (ops->maintenance_mode) {
      if (ops->maintenance_session_start(ops->session_ctx, ops->settings) != 0) {
        ops->print(localization_select(
            system_language,
            "[erro] Falha ao iniciar a sessao de recuperacao.\n",
            "[error] Failed to start the recovery session.\n",
            "[error] No fue posible iniciar la sesion de recuperacion.\n"));
        return -1;
      }
    } else {
      if (ops->system_login(ops->session_ctx, ops->settings) != 0) {
        ops->print(
            localization_text_for(system_language, LOC_TEXT_AUTH_FLOW_FAILED));
        return -1;
      }
    }
    login_service_poll(ops);

    {
      const struct user_record *login_user = ops->session_user(ops->session_ctx);
      const char *language = session_language(ops->session_ctx);
      if (!login_user || !login_user->username[0] ||
          ops->init_shell_context_user(login_user) != 0) {
        ops->print(
            localization_text_for(language, LOC_TEXT_SESSION_ACTIVATION_FAILED));
        ops->print(localization_text_for(language, LOC_TEXT_RETURNING_TO_LOGIN));
        continue;
      }
    }

    for (;;) {
      const struct user_record *active_user = ops->session_user(ops->session_ctx);
      const char *cwd = ops->session_cwd(ops->session_ctx);
      char prompt[128];

      login_service_poll(ops);
      shell_build_prompt(active_user, ops->settings, cwd, prompt,
                         sizeof(prompt));
      ops->print(prompt);

      ops->readline(line, sizeof(line), 0);
      login_service_poll(ops);
      if (!line[0]) {
        continue;
      }

      if (ops->dispatch_shell_command(line)) {
        login_service_poll(ops);
        if (ops->shell_context_should_logout(ops->shell_ctx)) {
          ops->session_reset(ops->session_ctx);
          ops->session_set_active(NULL);
          login_render_screen(ops, system_language, 0);
          break;
        }
        continue;
      }

      if (ops->is_equal(line, "help")) {
        (void)ops->run_shell_alias("help-any");
        login_service_poll(ops);
        continue;
      }
      if (ops->is_equal(line, "clear")) {
        (void)ops->run_shell_alias("mess");
        login_service_poll(ops);
        continue;
      }
      if (ops->is_equal(line, "reboot")) {
        (void)ops->run_shell_alias("shutdown-reboot");
        login_service_poll(ops);
        continue;
      }
      if (ops->is_equal(line, "halt")) {
        (void)ops->run_shell_alias("shutdown-off");
        login_service_poll(ops);
        continue;
      }
      if (ops->is_equal(line, "info")) {
        ops->cmd_info();
        login_service_poll(ops);
        continue;
      }

      ops->print(localization_text_for(session_language(ops->session_ctx),
                                       LOC_TEXT_UNKNOWN_COMMAND_PREFIX));
      ops->print(line);
      ops->print(localization_text_for(session_language(ops->session_ctx),
                                       LOC_TEXT_UNKNOWN_COMMAND_HINT));
      login_service_poll(ops);
    }
  }
}
