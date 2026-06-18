/*
 * userland/lib/capylibc-net/capy_net_error.c (F4 seção c parte 2/2)
 *
 * Single-process error slot for libcapy-net. Every entry point
 * resets it to CAPY_NET_OK before doing real work, then writes a
 * specific code on the failure path. `capy_net_last_error()`
 * exposes it. Threading-safe variant (TLS) lands with the broader
 * pthread integration; today libcapy-net is single-threaded.
 */

#include "capylibc-net/capy_net.h"

static capy_net_err_t g_last_error = CAPY_NET_OK;

void capy_net_internal_set_error(capy_net_err_t err);
void capy_net_internal_reset_error(void);

void capy_net_internal_set_error(capy_net_err_t err) {
  g_last_error = err;
}

void capy_net_internal_reset_error(void) {
  g_last_error = CAPY_NET_OK;
}

capy_net_err_t capy_net_last_error(void) {
  return g_last_error;
}

/*
 * POSIX-strerror-style human-readable strings for the net error codes.
 * All enum values are listed (no `default:` inside the switch) so a
 * future code addition trips -Wswitch; the trailing return covers any
 * out-of-range integer cast so the result is never NULL.
 */
const char *capy_net_strerror(capy_net_err_t err) {
  switch (err) {
  case CAPY_NET_OK:
    return "no error";
  case CAPY_NET_EINVAL:
    return "invalid argument";
  case CAPY_NET_EPARSE:
    return "malformed input";
  case CAPY_NET_ESOCK:
    return "socket creation failed";
  case CAPY_NET_ECONNECT:
    return "connection failed";
  case CAPY_NET_ESEND:
    return "send failed";
  case CAPY_NET_ERECV:
    return "receive failed";
  case CAPY_NET_EBUF:
    return "buffer too small";
  case CAPY_NET_EDNS:
    return "DNS resolution failed";
  case CAPY_NET_EHTTP:
    return "malformed HTTP response";
  case CAPY_NET_EUNSUPPORTED:
    return "feature not supported";
  }
  return "unknown network error";
}

/*
 * Classify a failed fetch into a user-facing stage (Etapa 6 / Slice 6.3).
 * A TLS error state pins the stage to TLS regardless of how the net layer
 * collapsed the failure: today a handshake/cert rejection surfaces as a
 * generic CAPY_NET_EUNSUPPORTED/EINVAL (see capy_net_internal_tls_error_to_net),
 * so without the TLS state a real "certificate rejected" would be reported
 * as "feature not supported". The caller passes the TLS state for THIS
 * request (CAPY_TLS_STATE_INIT for plain HTTP). Pure, never fails.
 */
capy_net_stage_t capy_net_diagnose_stage(capy_net_err_t net_err,
                                         capy_tls_state_t tls_state) {
  if (tls_state == CAPY_TLS_STATE_ERROR) {
    return CAPY_NET_STAGE_TLS;
  }
  switch (net_err) {
  case CAPY_NET_OK:
    return CAPY_NET_STAGE_OK;
  case CAPY_NET_EDNS:
    return CAPY_NET_STAGE_DNS;
  case CAPY_NET_ESOCK:
  case CAPY_NET_ECONNECT:
  case CAPY_NET_ESEND:
  case CAPY_NET_ERECV:
    return CAPY_NET_STAGE_TCP;
  case CAPY_NET_EHTTP:
    return CAPY_NET_STAGE_HTTP;
  case CAPY_NET_EINVAL:
  case CAPY_NET_EPARSE:
  case CAPY_NET_EBUF:
  case CAPY_NET_EUNSUPPORTED:
    return CAPY_NET_STAGE_INPUT;
  }
  return CAPY_NET_STAGE_INPUT;
}

const char *capy_net_stage_name(capy_net_stage_t stage) {
  switch (stage) {
  case CAPY_NET_STAGE_OK:
    return "OK";
  case CAPY_NET_STAGE_INPUT:
    return "input";
  case CAPY_NET_STAGE_DNS:
    return "DNS";
  case CAPY_NET_STAGE_TCP:
    return "TCP";
  case CAPY_NET_STAGE_TLS:
    return "TLS";
  case CAPY_NET_STAGE_HTTP:
    return "HTTP";
  }
  return "input";
}

/*
 * Tiny ring-3 language pick (EN is the fallback base, matching the kernel
 * localization_select rule): "es"/"es-*" -> ES; "pt"/"pt-*" -> PT-BR;
 * everything else (incl. "en", NULL, unknown) -> EN. Checked es-before-pt
 * because both "en" and "es" start with 'e'.
 */
static const char *net_pick(const char *lang, const char *pt, const char *en,
                            const char *es) {
  if (lang && (lang[0] == 'e' || lang[0] == 'E') &&
      (lang[1] == 's' || lang[1] == 'S')) {
    return es;
  }
  if (lang && (lang[0] == 'p' || lang[0] == 'P')) {
    return pt;
  }
  return en;
}

const char *capy_net_stage_message(capy_net_stage_t stage, const char *lang) {
  switch (stage) {
  case CAPY_NET_STAGE_OK:
    return net_pick(lang, "Sem erro.", "No error.", "Sin error.");
  case CAPY_NET_STAGE_INPUT:
    return net_pick(lang, "Endereco invalido.", "Invalid address.",
                    "Direccion no valida.");
  case CAPY_NET_STAGE_DNS:
    return net_pick(lang, "Nao foi possivel resolver o endereco (DNS).",
                    "Could not resolve the address (DNS).",
                    "No se pudo resolver la direccion (DNS).");
  case CAPY_NET_STAGE_TCP:
    return net_pick(lang, "Nao foi possivel conectar ao servidor.",
                    "Could not connect to the server.",
                    "No se pudo conectar al servidor.");
  case CAPY_NET_STAGE_TLS:
    return net_pick(lang,
                    "A conexao segura falhou (certificado rejeitado).",
                    "Secure connection failed (certificate rejected).",
                    "La conexion segura fallo (certificado rechazado).");
  case CAPY_NET_STAGE_HTTP:
    return net_pick(lang, "O servidor retornou uma resposta invalida.",
                    "The server returned an invalid response.",
                    "El servidor devolvio una respuesta no valida.");
  }
  return net_pick(lang, "Erro de rede.", "Network error.", "Error de red.");
}

/*
 * Actionable follow-up hint for a diagnostic stage (Etapa 6 UX maturity).
 * `capy_net_stage_message` says WHAT failed; this says WHAT THE USER CAN DO,
 * so the non-technical target user gets a clear next step. Same language rule
 * and ASCII-only convention as `capy_net_stage_message`. CAPY_NET_STAGE_OK has
 * no hint (empty string); the CapyBrowse Text app prints the hint as a second
 * line under the message only when it is non-empty. Pure, always non-NULL.
 */
const char *capy_net_stage_hint(capy_net_stage_t stage, const char *lang) {
  switch (stage) {
  case CAPY_NET_STAGE_OK:
    return "";
  case CAPY_NET_STAGE_INPUT:
    return net_pick(lang, "Verifique o endereco e tente novamente.",
                    "Check the address and try again.",
                    "Verifique la direccion e intente de nuevo.");
  case CAPY_NET_STAGE_DNS:
    return net_pick(
        lang, "Verifique sua conexao de rede ou a grafia do endereco.",
        "Check your network connection or the spelling of the address.",
        "Verifique su conexion de red o la escritura de la direccion.");
  case CAPY_NET_STAGE_TCP:
    return net_pick(
        lang, "O servidor pode estar fora do ar; tente novamente mais tarde.",
        "The server may be down; try again later.",
        "El servidor puede estar caido; intente de nuevo mas tarde.");
  case CAPY_NET_STAGE_TLS:
    return net_pick(lang, "O certificado de seguranca do site nao e confiavel.",
                    "The site's security certificate is not trusted.",
                    "El certificado de seguridad del sitio no es de confianza.");
  case CAPY_NET_STAGE_HTTP:
    return net_pick(lang, "Pode nao ser um site de texto simples.",
                    "It may not be a plain-text site.",
                    "Puede no ser un sitio de texto simple.");
  }
  return net_pick(lang, "Tente novamente.", "Try again.", "Intente de nuevo.");
}
