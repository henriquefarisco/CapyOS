/*
 * userland/bin/capybrowse/capybrowse_view.c — CapyBrowse Text view formatter.
 *
 * See capybrowse_view.h. Pure + self-contained (no libc, no syscalls): every
 * write is bounded by out_cap and the buffer stays NUL-terminated, so a hostile
 * document (huge title, 64 links, deep body) can never overflow the caller's
 * fixed buffer — it just truncates. Consumes only the published capy_text_doc
 * fields; it never calls back into the core.
 */

#include "capybrowse_view.h"

/* Append one byte, keeping a trailing NUL; drops the write if the buffer is
 * full (room is reserved for the NUL). */
static void vw_putc(char *out, size_t cap, size_t *pos, char c) {
  if (*pos + 1u < cap) {
    out[*pos] = c;
    (*pos)++;
    out[*pos] = '\0';
  }
}

static void vw_puts(char *out, size_t cap, size_t *pos, const char *s) {
  if (!s) {
    return;
  }
  while (*s) {
    vw_putc(out, cap, pos, *s);
    s++;
  }
}

static void vw_putu(char *out, size_t cap, size_t *pos, unsigned v) {
  char tmp[12];
  size_t n = 0u;
  if (v == 0u) {
    vw_putc(out, cap, pos, '0');
    return;
  }
  while (v > 0u && n < sizeof(tmp)) {
    tmp[n++] = (char)('0' + (int)(v % 10u));
    v /= 10u;
  }
  while (n > 0u) {
    vw_putc(out, cap, pos, tmp[--n]);
  }
}

size_t capybrowse_format_page(const struct capy_text_doc *doc, const char *body,
                              char *out, size_t out_cap) {
  size_t pos = 0u;
  if (!out || out_cap == 0u) {
    return 0u;
  }
  out[0] = '\0';

  if (doc && doc->has_title && doc->title[0] != '\0') {
    vw_puts(out, out_cap, &pos, doc->title);
    vw_puts(out, out_cap, &pos, "\n\n");
  }

  if (body && body[0] != '\0') {
    vw_puts(out, out_cap, &pos, body);
  }

  if (doc && doc->link_count > 0u) {
    vw_puts(out, out_cap, &pos, "\n\nLinks:\n");
    for (size_t i = 0u; i < doc->link_count; i++) {
      vw_puts(out, out_cap, &pos, "  [");
      vw_putu(out, out_cap, &pos, (unsigned)(i + 1u));
      vw_puts(out, out_cap, &pos, "] ");
      vw_puts(out, out_cap, &pos, doc->links[i].url);
      vw_putc(out, out_cap, &pos, '\n');
    }
  }

  if (doc && doc->warnings.count > 0u) {
    vw_puts(out, out_cap, &pos, "\n[");
    vw_putu(out, out_cap, &pos, (unsigned)doc->warnings.count);
    vw_puts(out, out_cap, &pos, " parse warning(s)]\n");
  }

  if (doc && doc->truncated) {
    vw_puts(out, out_cap, &pos, "[content truncated]\n");
  }

  return pos;
}

const char *capybrowse_session_lang_string(long lang_code) {
  switch (lang_code) {
  case 0: /* CAPY_SESSION_LANG_PT_BR */
    return "pt-BR";
  case 2: /* CAPY_SESSION_LANG_ES */
    return "es";
  case 1: /* CAPY_SESSION_LANG_EN */
  default: /* unknown -> EN universal fallback base */
    return "en";
  }
}

/* Language pick mirroring capylibc-net's net_pick (EN is the fallback base):
 * "es"/"es-*" -> ES; "pt"/"pt-*" -> PT-BR; everything else -> EN. es-before-pt
 * is irrelevant here, but es-before-en matters since both begin with 'e'. */
static const char *vw_pick(const char *lang, const char *pt, const char *en,
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

/* Short localized phrase for an HTTP error status. Specific phrases for the
 * common codes; generic 4xx vs 5xx otherwise (status is 400..599 here). */
static const char *vw_status_phrase(int status, const char *lang) {
  switch (status) {
  case 404:
    return vw_pick(lang, "Pagina nao encontrada.", "Page not found.",
                   "Pagina no encontrada.");
  case 403:
    return vw_pick(lang, "Acesso negado.", "Access denied.", "Acceso denegado.");
  case 500:
    return vw_pick(lang, "Erro interno do servidor.", "Internal server error.",
                   "Error interno del servidor.");
  case 503:
    return vw_pick(lang, "Servico indisponivel.", "Service unavailable.",
                   "Servicio no disponible.");
  default:
    break;
  }
  if (status >= 500) {
    return vw_pick(lang, "Erro do servidor.", "Server error.",
                   "Error del servidor.");
  }
  return vw_pick(lang, "Erro na requisicao.", "Request error.",
                 "Error en la solicitud.");
}

size_t capybrowse_format_status_notice(int status_code, const char *lang,
                                       char *out, size_t out_cap) {
  size_t pos = 0u;
  if (!out || out_cap == 0u) {
    return 0u;
  }
  out[0] = '\0';
  if (status_code < 400) {
    return 0u; /* success / redirect: no error notice */
  }
  vw_puts(out, out_cap, &pos, "HTTP ");
  vw_putu(out, out_cap, &pos, (unsigned)status_code);
  vw_puts(out, out_cap, &pos, ": ");
  vw_puts(out, out_cap, &pos, vw_status_phrase(status_code, lang));
  vw_putc(out, out_cap, &pos, '\n');
  vw_putc(out, out_cap, &pos, '\n');
  return pos;
}

/* Case-insensitive substring search; `needle` must be lowercase ASCII. Pure,
 * freestanding (no libc): the ring-3 app links only the minimal capylibc. */
static int vw_ci_substr(const char *hay, const char *needle) {
  if (!hay || !needle) {
    return 0;
  }
  for (const char *p = hay; *p; p++) {
    const char *h = p;
    const char *n = needle;
    while (*h && *n) {
      char a = *h;
      if (a >= 'A' && a <= 'Z') {
        a = (char)(a + 32);
      }
      if (a != *n) {
        break;
      }
      h++;
      n++;
    }
    if (*n == '\0') {
      return 1;
    }
  }
  return 0;
}

int capybrowse_content_is_text(const char *content_type) {
  if (!content_type || !content_type[0]) {
    return 1; /* absent header: stay tolerant (servers often omit it) */
  }
  if (vw_ci_substr(content_type, "text/") || vw_ci_substr(content_type, "html") ||
      vw_ci_substr(content_type, "xml") || vw_ci_substr(content_type, "json")) {
    return 1;
  }
  return 0;
}

size_t capybrowse_format_content_notice(const char *content_type,
                                        const char *lang, char *out,
                                        size_t out_cap) {
  size_t pos = 0u;
  if (!out || out_cap == 0u) {
    return 0u;
  }
  out[0] = '\0';
  vw_puts(out, out_cap, &pos,
          vw_pick(lang, "Conteudo nao-textual", "Non-text content",
                  "Contenido no textual"));
  vw_puts(out, out_cap, &pos, " (");
  vw_puts(out, out_cap, &pos,
          (content_type && content_type[0])
              ? content_type
              : vw_pick(lang, "desconhecido", "unknown", "desconocido"));
  vw_puts(out, out_cap, &pos, "): ");
  vw_puts(out, out_cap, &pos,
          vw_pick(lang, "nao exibivel em modo texto.",
                  "cannot be shown in text mode.",
                  "no se puede mostrar en modo texto."));
  vw_putc(out, out_cap, &pos, '\n');
  return pos;
}
