/*
 * src/auth/login_runtime/view_model.c
 *
 * Login-window entry-level view model builder — extracted
 * byte-for-byte from `src/auth/login_runtime.c` during PR C.65 of
 * the Estagio C dedicated plan.  Hosts the public function used by
 * the runtime preview path and any external caller that needs to
 * produce a localised view model from a contract:
 *
 *   - login_window_view_model_build
 *
 * This is the entry-level view model that powers the boot-time
 * preview of the loginwindow.  It is fail-closed: if any required
 * field is missing the resulting model receives empty/fallback
 * defaults.  Marks the second-to-last step of Phase 7 of the
 * Estagio C dedicated plan.
 *
 * See `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"
#include "auth/internal/login_runtime_internal.h"
#include "lang/localization.h"

int login_window_view_model_build(const struct login_window_contract *contract,
                                  const char *language,
                                  struct login_window_view_model *out) {
  const char *lang = localization_normalize_language(language);
  if (!out) return -1;
  if (!lang) lang = "en";
  out->renderable = 0;
  out->password_enabled = 0;
  out->recovery_enabled = 0;
  out->fallback_required = 1;
  out->maintenance_notice = 0;
  (void)login_window_credential_policy_from_contract(contract,
                                                     &out->credential_policy);
  out->password_submit_enabled = out->credential_policy.password_submit_allowed;
  out->password_masked = out->credential_policy.password_mask_required;
  out->credential_wipe_required = out->credential_policy.password_wipe_required;
  out->text_login_authoritative = out->credential_policy.text_login_authoritative;
  out->state = "blocked";
  out->title = localization_select(lang, "Login grafico indisponivel",
                                   "Graphical login unavailable",
                                   "Inicio grafico no disponible");
  out->message = localization_select(lang,
                                     "O login textual permanece ativo.",
                                     "Text login remains active.",
                                     "El inicio textual permanece activo.");
  out->blocked_reason = "contract-unavailable";
  if (!contract) return 0;
  out->blocked_reason = contract->blocked_reason ? contract->blocked_reason
                                                 : "blocked";
  if (contract->ready) {
    out->renderable = 1;
    out->password_enabled = out->credential_policy.password_field_allowed;
    out->fallback_required = out->credential_policy.password_submit_allowed ? 0 : 1;
    out->state = "ready";
    out->title = "CapyOS";
    out->message = localization_select(lang,
                                       "Campo de senha reservado; use o login textual.",
                                       "Password field reserved; use text login.",
                                       "Campo de contrasena reservado; usa el inicio textual.");
    return 0;
  }
  if (!contract->has_input) {
    out->state = "input-unavailable";
    out->message = localization_select(lang,
                                       "Nenhum dispositivo de entrada disponivel.",
                                       "No input device is available.",
                                       "No hay dispositivo de entrada disponible.");
    return 0;
  }
  if (!contract->session_available || !contract->settings_available ||
      !contract->shell_callbacks_ready || !contract->auth_callbacks_ready ||
      !contract->ui_callbacks_ready) {
    out->state = "runtime-incomplete";
    out->message = localization_select(lang,
                                       "Runtime de login incompleto; usando login textual.",
                                       "Login runtime incomplete; using text login.",
                                       "Runtime de inicio incompleto; usando inicio textual.");
    return 0;
  }
  if (contract->maintenance_mode) {
    out->renderable = 1;
    out->recovery_enabled = out->credential_policy.recovery_allowed;
    out->maintenance_notice = 1;
    out->state = "maintenance";
    out->title = localization_select(lang, "Modo de recuperacao",
                                     "Recovery mode", "Modo de recuperacion");
    out->message = contract->recovery_available
                       ? localization_select(lang,
                                             "Sessao textual de recuperacao disponivel.",
                                             "Text recovery session is available.",
                                             "Sesion textual de recuperacion disponible.")
                       : localization_select(lang,
                                             "Recuperacao indisponivel; use o TTY.",
                                             "Recovery unavailable; use the TTY.",
                                             "Recuperacion no disponible; usa el TTY.");
    return 0;
  }
  out->state = "blocked";
  return 0;
}
