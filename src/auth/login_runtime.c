#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

static int login_consume_recovery_login_request(struct login_runtime_ops *ops) {
  if (!ops || !ops->consume_recovery_login_request) {
    return 0;
  }
  return ops->consume_recovery_login_request() ? 1 : 0;
}

static void login_show_maintenance_notice(struct login_runtime_ops *ops,
                                          const char *language,
                                          int maintenance_mode) {
  if (!ops || !maintenance_mode) {
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

static const char *login_view_text(const char *text, const char *fallback) {
  return (text && text[0]) ? text : fallback;
}

static void login_render_window_preview(struct login_runtime_ops *ops,
                                        const char *language) {
  struct login_window_contract contract;
  struct login_window_view_model model;
  if (!ops || !ops->print) return;
  if (login_window_contract_evaluate(ops, &contract) != 0) return;
  if (login_window_view_model_build(&contract, language, &model) != 0) return;

  ops->print("\n+------------------------------------------+\n");
  ops->print("| CapyOS loginwindow preview              |\n");
  ops->print("+------------------------------------------+\n");
  ops->print("title: ");
  ops->print(login_view_text(model.title, "CapyOS"));
  ops->print("\nstate: ");
  ops->print(login_view_text(model.state, "blocked"));
  ops->print("\nmessage: ");
  ops->print(login_view_text(model.message, "Text login remains active."));
  ops->print("\nreason: ");
  ops->print(login_view_text(model.blocked_reason, "blocked"));
  ops->print(model.password_enabled
                 ? "\npassword-field: contract-ready; masked; GUI submit disabled\n"
                 : "\npassword-field: disabled; use text login\n");
  ops->print(model.password_submit_enabled
                 ? "password-submit: enabled\n"
                 : "password-submit: disabled; text login authoritative\n");
  ops->print(model.credential_wipe_required
                 ? "credential-buffer: ephemeral; wipe-required\n"
                 : "credential-buffer: unavailable\n");
  ops->print(model.credential_policy.recovery_requires_text_session
                 ? "recovery-policy: text-session-only\n"
                 : "recovery-policy: gui-eligible\n");
  ops->print(model.recovery_enabled
                 ? "recovery-action: available through text recovery\n"
                 : "recovery-action: disabled\n");
  ops->print(model.fallback_required
                 ? "fallback: text login required\n"
                 : "fallback: text login remains authoritative\n");
  ops->print("+------------------------------------------+\n");
}

static void login_render_screen(struct login_runtime_ops *ops,
                                const char *language, int first_screen,
                                int maintenance_mode) {
  if (!ops) {
    return;
  }
  if (first_screen) {
    ops->show_splash(ops->settings);
  }
  ops->clear_view();
  ops->ui_banner();
  if (ops->render_window_layout) {
    ops->render_window_layout();
  } else {
    login_render_window_preview(ops, language);
  }
  if (maintenance_mode) {
    ops->print("\n");
    login_show_maintenance_notice(ops, language, maintenance_mode);
  }
}

int login_runtime_run(struct login_runtime_ops *ops) {
  int first_login_screen = 1;
  char line[128];
  struct login_window_contract login_window;
  const char *system_language = "en";

  dbg_login_puts("[lr] enter\n");
  if (!ops_ready(ops)) {
    dbg_login_puts("[lr] ops not ready\n");
    return -1;
  }
  if (ops->settings && ops->settings->language[0]) {
    const char *normalized = localization_normalize_language(ops->settings->language);
    if (normalized) {
      system_language = normalized;
    }
  }

  if (ops->prepare_shell_runtime() != 0) {
    dbg_login_puts("[lr] prepare_shell_runtime retry\n");
    ops->print(localization_text_for(system_language,
                                     LOC_TEXT_PREPARE_SHELL_FAILED));
    /* Do not abort — the runtime may be partially ready (enough for
     * login to function), or a second attempt may succeed.  Halting
     * here would make the system completely unusable. */
    (void)ops->prepare_shell_runtime();
  }
  dbg_login_puts("[lr] shell runtime ready\n");
  login_service_poll(ops);

  for (;;) {
    int maintenance_mode = 0;
    if (login_window_contract_evaluate(ops, &login_window) != 0) {
      return -1;
    }
    maintenance_mode = login_window.maintenance_mode;
    if (first_login_screen) {
      dbg_login_puts("[lr] render screen\n");
      login_render_screen(ops, system_language, 1, maintenance_mode);
      first_login_screen = 0;
    }
    ops->print("\n");

    if (!login_window.has_input) {
      ops->print(localization_text_for(system_language, LOC_TEXT_NO_INPUT_DEVICE));
    }

    ops->session_reset(ops->session_ctx);
    ops->session_set_active(NULL);
    ops->shell_context_init(ops->shell_ctx, ops->session_ctx, ops->settings);
    login_service_poll(ops);

    if (maintenance_mode) {
      if (ops->maintenance_session_start(ops->session_ctx, ops->settings) != 0) {
        ops->print(localization_select(
            system_language,
            "[erro] Falha ao iniciar a sessao de recuperacao.\n",
            "[error] Failed to start the recovery session.\n",
            "[error] No fue posible iniciar la sesion de recuperacion.\n"));
        return -1;
      }
    } else {
      dbg_login_puts("[lr] system_login begin\n");
      if (ops->system_login(ops->session_ctx, ops->settings) != 0) {
        dbg_login_puts("[lr] system_login failed\n");
        ops->print(
            localization_text_for(system_language, LOC_TEXT_AUTH_FLOW_FAILED));
        return -1;
      }
      dbg_login_puts("[lr] system_login ok\n");
    }
    dbg_login_puts("[lr] post-login service_poll begin\n");
    login_service_poll(ops);
    dbg_login_puts("[lr] post-login service_poll end\n");

    {
      const struct user_record *login_user = ops->session_user(ops->session_ctx);
      const char *language = session_language(ops->session_ctx);
      if (!login_user || !login_user->username[0] ||
          ops->init_shell_context_user(login_user) != 0) {
        dbg_login_puts("[lr] init shell ctx failed\n");
        ops->print(
            localization_text_for(language, LOC_TEXT_SESSION_ACTIVATION_FAILED));
        ops->print(localization_text_for(language, LOC_TEXT_RETURNING_TO_LOGIN));
        continue;
      }
      dbg_login_puts("[lr] init shell ctx ok\n");
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
        if (maintenance_mode) {
          int resume_requested = login_consume_recovery_login_request(ops);
          struct login_recovery_resume_policy resume_policy;
          if (login_recovery_resume_policy_evaluate(ops, 1, resume_requested,
                                                    &resume_policy) != 0) {
            return -1;
          }
          if (resume_policy.can_resume_normal_login) {
            ops->print(localization_select(
                system_language,
                "[recovery] Sessao de recuperacao encerrada. Voltando para o login normal.\n",
                "[recovery] Recovery session closed. Returning to the normal login.\n",
                "[recovery] Sesion de recuperacion cerrada. Volviendo al inicio de sesion normal.\n"));
            ops->session_reset(ops->session_ctx);
            ops->session_set_active(NULL);
            login_render_screen(ops, system_language, 0, 0);
            break;
          }
          if (resume_requested) {
            ops->print(localization_select(
                system_language,
                "[recovery] Retorno ao login normal bloqueado: ",
                "[recovery] Normal login return blocked: ",
                "[recovery] Retorno al inicio normal bloqueado: "));
            ops->print(resume_policy.blocked_reason);
            ops->putc('\n');
          }
        }
        if (ops->shell_context_should_logout(ops->shell_ctx)) {
          ops->session_reset(ops->session_ctx);
          ops->session_set_active(NULL);
          login_render_screen(ops, system_language, 0,
                              login_maintenance_mode_active(ops));
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
