static int hv_tag_is_void(const char *tag) {
  return hv_streq_ci(tag, "br") || hv_streq_ci(tag, "img") ||
         hv_streq_ci(tag, "meta") || hv_streq_ci(tag, "link") ||
         hv_streq_ci(tag, "input") || hv_streq_ci(tag, "hr") ||
         hv_streq_ci(tag, "wbr") || hv_streq_ci(tag, "source") ||
         hv_streq_ci(tag, "track") || hv_streq_ci(tag, "area");
}

static size_t hv_skip_block(const char *html, size_t len, size_t pos,
                            const char *tag) {
  int depth = 1;
  while (pos < len) {
    char inner[32];
    size_t tag_end = pos;
    int closing = 0;
    int self_closing = 0;
    if (html[pos] != '<') {
      pos++;
      continue;
    }
    pos++;
    if (pos < len && (html[pos] == '!' || html[pos] == '?')) {
      pos = hv_skip_special_tag(html, len, pos);
      continue;
    }
    if (pos < len && html[pos] == '/') {
      closing = 1;
      pos++;
    }
    hv_read_tag_name(html, len, &pos, inner, sizeof(inner));
    pos = hv_scan_tag_end(html, len, pos, &tag_end, &self_closing);
    if (!inner[0]) continue;
    if (closing && hv_streq_ci(inner, tag)) {
      depth--;
      if (depth == 0) return pos;
      continue;
    }
    if (!closing && !self_closing && hv_streq_ci(inner, tag)) depth++;
  }
  return len;
}

static size_t hv_collect_text_until_tag(const char *html, size_t len,
                                        size_t pos, const char *tag,
                                        char *out, size_t out_len) {
  size_t out_pos = 0;
  int last_space = 1;
  int depth = 1;
  if (!out || out_len == 0) return pos;
  out[0] = '\0';
  while (pos < len) {
    if (html[pos] == '<') {
      char inner[32];
      char alt[HTML_TEXT_MAX];
      size_t attr_start = 0;
      size_t tag_end = pos;
      size_t tag_start = pos; /* position of '<' for backtracking */
      int closing = 0;
      int self_closing = 0;
      pos++;
      if (pos < len && (html[pos] == '!' || html[pos] == '?')) {
        pos = hv_skip_special_tag(html, len, pos);
        continue;
      }
      if (pos < len && html[pos] == '/') {
        closing = 1;
        pos++;
      }
      hv_read_tag_name(html, len, &pos, inner, sizeof(inner));
      attr_start = pos;
      pos = hv_scan_tag_end(html, len, pos, &tag_end, &self_closing);
      if (!inner[0]) continue;
      if (closing && hv_streq_ci(inner, tag)) {
        depth--;
        if (depth == 0) break;
        continue;
      }
      /* Non-nestable block tags: a new opening <p>/<li> implicitly closes current */
      if (!closing && !self_closing && hv_streq_ci(inner, tag) &&
          (hv_streq_ci(tag, "p") || hv_streq_ci(tag, "li") ||
           hv_streq_ci(tag, "dt") || hv_streq_ci(tag, "dd"))) {
        pos = tag_start; /* back up so the outer parser re-processes this tag */
        break;
      }
      if (!closing && !self_closing && hv_streq_ci(inner, tag)) {
        depth++;
        continue;
      }
      if (!closing &&
          (hv_streq_ci(inner, "script") || hv_streq_ci(inner, "style") ||
           hv_streq_ci(inner, "svg") || hv_streq_ci(inner, "canvas") ||
           hv_streq_ci(inner, "iframe") || hv_streq_ci(inner, "template") ||
           hv_streq_ci(inner, "noscript") || hv_streq_ci(inner, "noframes"))) {
        pos = hv_skip_block(html, len, pos, inner);
        continue;
      }
      if (!closing &&
          (hv_streq_ci(inner, "br") || hv_streq_ci(inner, "p") ||
           hv_streq_ci(inner, "div") || hv_streq_ci(inner, "section") ||
           hv_streq_ci(inner, "article") || hv_streq_ci(inner, "li") ||
           hv_streq_ci(inner, "tr") || hv_streq_ci(inner, "table"))) {
        hv_text_append_char(out, out_len, &out_pos, '\n', &last_space);
      }
      if (!closing && hv_streq_ci(inner, "img") &&
          hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                "alt", alt, sizeof(alt))) {
        hv_append_decoded_text(out, out_len, &out_pos, alt, kstrlen(alt), &last_space);
      }
      /* For <a href> inside block text: collect link text then append " [url]" if href differs */
      if (!closing && !self_closing && hv_streq_ci(inner, "a")) {
        char a_href[HTML_URL_MAX];
        char a_text[HTML_TEXT_MAX];
        a_href[0] = '\0'; a_text[0] = '\0';
        hv_extract_attr_value(html + attr_start, tag_end - attr_start, "href",
                              a_href, sizeof(a_href));
        pos = hv_collect_text_until_tag(html, len, pos, "a", a_text, sizeof(a_text));
        if (a_text[0]) {
          hv_text_append_char(out, out_len, &out_pos, ' ', &last_space);
          hv_append_decoded_text(out, out_len, &out_pos, a_text, kstrlen(a_text), &last_space);
        }
        continue;
      }
      continue;
    }
    if (html[pos] == '&') {
      size_t consumed = 0;
      char decoded = 0;
      if (hv_decode_entity_value(html + pos, len - pos, &consumed, &decoded)) {
        hv_text_append_char(out, out_len, &out_pos, decoded, &last_space);
        pos += consumed;
        continue;
      }
    }
    hv_text_append_char(out, out_len, &out_pos, html[pos], &last_space);
    pos++;
  }
  hv_trim_text(out);
  return pos;
}

/* Returns 1 if tag is a block-level element that implicitly closes an open p/li */
static int hv_is_block_tag(const char *tag) {
  return hv_streq_ci(tag, "p") || hv_streq_ci(tag, "div") ||
         hv_streq_ci(tag, "h1") || hv_streq_ci(tag, "h2") ||
         hv_streq_ci(tag, "h3") || hv_streq_ci(tag, "h4") ||
         hv_streq_ci(tag, "h5") || hv_streq_ci(tag, "h6") ||
         hv_streq_ci(tag, "li") || hv_streq_ci(tag, "ul") ||
         hv_streq_ci(tag, "ol") || hv_streq_ci(tag, "blockquote") ||
         hv_streq_ci(tag, "table") || hv_streq_ci(tag, "tr") ||
         hv_streq_ci(tag, "section") || hv_streq_ci(tag, "article") ||
         hv_streq_ci(tag, "header") || hv_streq_ci(tag, "footer") ||
         hv_streq_ci(tag, "main") || hv_streq_ci(tag, "nav") ||
         hv_streq_ci(tag, "hr") || hv_streq_ci(tag, "pre");
}

/* Flush accumulated text to a new TEXT node inheriting properties from tmpl.
 * Clears buf. Returns 1 if a node was emitted. */
static int hv_flush_inline_text(struct html_document *doc,
                                const struct html_node *tmpl,
                                char *buf, size_t *buf_pos,
                                int preserve_block_type) {
  struct html_node *node;
  hv_trim_text(buf);
  if (!buf[0]) { buf[0] = '\0'; *buf_pos = 0; return 0; }
  node = html_push_node(doc);
  if (!node) { buf[0] = '\0'; *buf_pos = 0; return 0; }
  *node = *tmpl;                /* copy CSS, indent, color, etc. from parent */
  node->type = preserve_block_type ? tmpl->type : HTML_NODE_TEXT;
  node->href[0] = '\0';        /* no link */
  node->il_x_left = node->il_x_right = 0;
  kstrcpy(node->text, sizeof(node->text), buf);
  buf[0] = '\0'; *buf_pos = 0;
  return 1;
}

/* Parse inline content of a block element, emitting separate nodes for text
 * segments and <a>/<strong>/<em>/<span> children. Returns new parse position. */
static size_t hv_parse_inline_content(const char *html, size_t len, size_t pos,
                                       const char *close_tag,
                                       struct html_document *doc,
                                       const struct html_node *tmpl) {
  /* Static buffers: CapyOS is single-threaded; avoids ~2 KB stack pressure */
  static char buf[HTML_TEXT_MAX];
  size_t start_node_count = doc ? doc->node_count : 0;
  size_t buf_pos = 0;
  int split_inline = 0;
  int last_space = 1;
  buf[0] = '\0';

  while (pos < len) {
    if (html[pos] != '<') {
      /* Plain text or entity */
      if (html[pos] == '&') {
        size_t consumed = 0; char decoded = 0;
        if (hv_decode_entity_value(html + pos, len - pos, &consumed, &decoded)) {
          hv_text_append_char(buf, sizeof(buf), &buf_pos, decoded, &last_space);
          pos += consumed; continue;
        }
      }
      hv_text_append_char(buf, sizeof(buf), &buf_pos, html[pos], &last_space);
      pos++; continue;
    }
    /* Tag */
    {
      char inner[32];
      size_t attr_start, tag_end;
      int closing = 0, self_closing = 0;
      size_t tag_start = pos;
      pos++;
      if (pos < len && (html[pos] == '!' || html[pos] == '?')) {
        pos = hv_skip_special_tag(html, len, pos); continue;
      }
      if (pos < len && html[pos] == '/') { closing = 1; pos++; }
      hv_read_tag_name(html, len, &pos, inner, sizeof(inner));
      attr_start = pos;
      pos = hv_scan_tag_end(html, len, pos, &tag_end, &self_closing);
      if (!inner[0]) continue;

      /* Closing the block */
      if (closing && hv_streq_ci(inner, close_tag)) break;
      /* Implicit close: another block tag opens */
      if (!closing && !self_closing && hv_is_block_tag(inner) &&
          !hv_streq_ci(inner, close_tag)) {
        pos = tag_start; /* back up so outer parser picks up this tag */
        break;
      }
      if (closing) continue; /* unmatched closing tag */

      /* <br>: newline */
      if (hv_streq_ci(inner, "br") || hv_streq_ci(inner, "wbr")) {
        split_inline = 1;
        hv_flush_inline_text(doc, tmpl, buf, &buf_pos, 0);
        last_space = 1; continue;
      }
      /* Skip scripts/style/svg */
      if (hv_streq_ci(inner, "script") || hv_streq_ci(inner, "style") ||
          hv_streq_ci(inner, "svg") || hv_streq_ci(inner, "canvas")) {
        if (!self_closing) pos = hv_skip_block(html, len, pos, inner);
        continue;
      }
      /* <a href="...">link text</a> — flush prior text, emit anchor node */
      if (!self_closing && hv_streq_ci(inner, "a")) {
        static char a_href[HTML_URL_MAX];
        static char a_text[HTML_TEXT_MAX];
        size_t a_tpos = 0; int a_lsp = 1;
        struct html_node *anode;
        a_href[0] = '\0'; a_text[0] = '\0';
        hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                              "href", a_href, sizeof(a_href));
        /* Collect anchor text (no nested a's) */
        while (pos < len) {
          if (html[pos] == '<') {
            char itag[16]; size_t ie = pos; int ic = 0, isc = 0;
            size_t ip = pos + 1;
            if (ip < len && html[ip] == '/') { ic = 1; ip++; }
            hv_read_tag_name(html, len, &ip, itag, sizeof(itag));
            ip = hv_scan_tag_end(html, len, ip, &ie, &isc);
            if (ic && hv_streq_ci(itag, "a")) { pos = ip; break; }
            if (hv_streq_ci(itag, "br")) {
              hv_text_append_char(a_text, sizeof(a_text), &a_tpos, '\n', &a_lsp);
              pos = ip; continue;
            }
            if (!ic && !isc &&
                (hv_streq_ci(itag, "span") || hv_streq_ci(itag, "strong") ||
                 hv_streq_ci(itag, "em") || hv_streq_ci(itag, "b") ||
                 hv_streq_ci(itag, "i"))) { pos = ip; continue; }
            if (ic && (hv_streq_ci(itag, "span") || hv_streq_ci(itag, "strong") ||
                       hv_streq_ci(itag, "em") || hv_streq_ci(itag, "b") ||
                       hv_streq_ci(itag, "i"))) { pos = ip; continue; }
            pos = ip; continue;
          }
          if (html[pos] == '&') {
            size_t consumed = 0; char decoded = 0;
            if (hv_decode_entity_value(html + pos, len - pos, &consumed, &decoded)) {
              hv_text_append_char(a_text, sizeof(a_text), &a_tpos, decoded, &a_lsp);
              pos += consumed; continue;
            }
          }
          hv_text_append_char(a_text, sizeof(a_text), &a_tpos, html[pos], &a_lsp);
          pos++;
        }
        hv_trim_text(a_text);
        if (!a_text[0] && a_href[0]) kstrcpy(a_text, sizeof(a_text), a_href);
        if (a_text[0] || a_href[0]) {
          split_inline = 1;
          hv_flush_inline_text(doc, tmpl, buf, &buf_pos, 0);
          last_space = 1;
          anode = html_push_node(doc);
          if (anode) {
            *anode = *tmpl;
            anode->type = HTML_NODE_TAG_A;
            anode->color = 0x89B4FA; /* link blue */
            anode->il_x_left = anode->il_x_right = 0;
            kstrcpy(anode->text, sizeof(anode->text), a_text);
            kstrcpy(anode->href, sizeof(anode->href), a_href);
            hv_apply_node_attrs(anode, html + attr_start, tag_end - attr_start);
          }
        }
        continue;
      }
      /* <strong>/<b>/<em>/<i>/<mark>/<span>/<small>/<s>/<u> etc.: collect text inline */
      if (!self_closing &&
          (hv_streq_ci(inner, "strong") || hv_streq_ci(inner, "b") ||
           hv_streq_ci(inner, "em") || hv_streq_ci(inner, "i") ||
           hv_streq_ci(inner, "mark") || hv_streq_ci(inner, "span") ||
           hv_streq_ci(inner, "small") || hv_streq_ci(inner, "s") ||
           hv_streq_ci(inner, "u") || hv_streq_ci(inner, "del") ||
           hv_streq_ci(inner, "code") || hv_streq_ci(inner, "kbd") ||
           hv_streq_ci(inner, "abbr") || hv_streq_ci(inner, "cite") ||
           hv_streq_ci(inner, "time") || hv_streq_ci(inner, "sub") ||
           hv_streq_ci(inner, "sup") || hv_streq_ci(inner, "label"))) {
        /* Flush current run, then read this inline element's text directly into buf */
        static char sub_text[HTML_TEXT_MAX];
        size_t sub_pos = 0; int sub_lsp = 1;
        sub_text[0] = '\0';
        /* Collect until closing tag */
        while (pos < len) {
          if (html[pos] == '<') {
            char itag[16]; size_t ie = pos; int ic = 0, isc = 0;
            size_t ip = pos + 1;
            if (ip < len && html[ip] == '/') { ic = 1; ip++; }
            hv_read_tag_name(html, len, &ip, itag, sizeof(itag));
            ip = hv_scan_tag_end(html, len, ip, &ie, &isc);
            if (ic && hv_streq_ci(itag, inner)) { pos = ip; break; }
            if (hv_streq_ci(itag, "br")) {
              hv_text_append_char(sub_text, sizeof(sub_text), &sub_pos, '\n', &sub_lsp);
            }
            pos = ip; continue;
          }
          if (html[pos] == '&') {
            size_t consumed = 0; char decoded = 0;
            if (hv_decode_entity_value(html + pos, len - pos, &consumed, &decoded)) {
              hv_text_append_char(sub_text, sizeof(sub_text), &sub_pos, decoded, &sub_lsp);
              pos += consumed; continue;
            }
          }
          hv_text_append_char(sub_text, sizeof(sub_text), &sub_pos, html[pos], &sub_lsp);
          pos++;
        }
        hv_trim_text(sub_text);
        if (sub_text[0]) {
          /* Append to current text buffer with a space separator */
          if (buf_pos > 0 && buf[buf_pos - 1] != ' ')
            hv_text_append_char(buf, sizeof(buf), &buf_pos, ' ', &last_space);
          hv_append_decoded_text(buf, sizeof(buf), &buf_pos, sub_text,
                                 kstrlen(sub_text), &last_space);
        }
        continue;
      }
      /* <img alt="..."> */
      if (hv_streq_ci(inner, "img")) {
        char alt[HTML_TEXT_MAX];
        alt[0] = '\0';
        hv_extract_attr_value(html + attr_start, tag_end - attr_start, "alt",
                              alt, sizeof(alt));
        if (alt[0]) hv_append_decoded_text(buf, sizeof(buf), &buf_pos, alt,
                                           kstrlen(alt), &last_space);
        continue;
      }
      /* Other tags: skip them */
    }
  }
  hv_flush_inline_text(doc, tmpl, buf, &buf_pos,
                       !split_inline && doc &&
                           (size_t)doc->node_count == start_node_count);
  return pos;
}

static void hv_apply_node_attrs(struct html_node *node, const char *attrs,
                               size_t len) {
  char style_attr[256];
  char wh_buf[16];
  style_attr[0] = '\0';
  if (!node || !attrs) return;
  (void)hv_extract_attr_value(attrs, len, "id", node->id, sizeof(node->id));
  (void)hv_extract_attr_value(attrs, len, "class", node->class_list,
                              sizeof(node->class_list));
  if (hv_extract_attr_value(attrs, len, "style", style_attr,
                             sizeof(style_attr)))
    css_apply_inline(style_attr, node);
  /* width= / height= HTML attributes (used on <img>, <td>, etc.) */
  if (hv_extract_attr_value(attrs, len, "width", wh_buf, sizeof(wh_buf))) {
    uint32_t px = 0; const char *p = wh_buf;
    while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
    if (px > 0 && px < 65535 && node->css_width == 0) node->css_width = (uint16_t)px;
  }
  /* Boolean attribute "open" (e.g. <details open>) */
  if (hv_has_boolean_attr(attrs, len, "open")) node->open = 1;
}

