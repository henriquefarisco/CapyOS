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
