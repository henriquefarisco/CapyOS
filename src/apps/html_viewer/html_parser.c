#include "internal/html_viewer_internal.h"

int html_parse(const char *html, size_t len, struct html_document *doc) {
  /* All large buffers are static: CapyOS is single-threaded, html_parse is
   * never re-entrant, and the combined frame (~3 KB) was overflowing the stack. */
  static char current_form_action[HTML_URL_MAX];
  static char current_picture_source[HTML_URL_MAX];
  uint8_t current_form_method = HTML_FORM_METHOD_GET;
  int list_ordered = 0;
  int list_num = 1;
  size_t pos = 0;
  if (!html || !doc) return -1;
  kmemzero(doc, sizeof(*doc));
  current_form_action[0] = '\0';
  current_picture_source[0] = '\0';
  while (pos < len && doc->node_count < HTML_MAX_NODES) {
    if (hv_is_space(html[pos])) {
      pos++;
      continue;
    }
    if (html[pos] == '<') {
      static char tag[32];
      static char href[HTML_URL_MAX];
      static char src[HTML_URL_MAX];
      static char text[HTML_TEXT_MAX];
      static char name[HTML_COOKIE_NAME_MAX];
      char type_attr[24];
      char method_attr[16];
      size_t attr_start = 0;
      size_t tag_end = pos;
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
      hv_read_tag_name(html, len, &pos, tag, sizeof(tag));
      attr_start = pos;
      pos = hv_scan_tag_end(html, len, pos, &tag_end, &self_closing);
      if (!tag[0]) continue;
      if (closing) {
        if (hv_streq_ci(tag, "form")) {
          current_form_action[0] = '\0';
          current_form_method = HTML_FORM_METHOD_GET;
        } else if (hv_streq_ci(tag, "ol")) {
          list_ordered = 0;
          list_num = 1;
        } else if (hv_streq_ci(tag, "picture")) {
          current_picture_source[0] = '\0';
        }
        continue;
      }
      if (!self_closing && hv_tag_is_void(tag)) self_closing = 1;

      /* Collect <style> block into doc->style_text for CSS processing */
      if (hv_streq_ci(tag, "style")) {
        if (!self_closing) {
          size_t used = kstrlen(doc->style_text);
          size_t cap = HTML_STYLE_BUF_MAX - 1;
          size_t end = pos;
          while (end < len) {
            if (html[end] == '<') {
              size_t j = end + 1;
              if (j < len && html[j] == '/') j++;
              size_t k = j;
              while (k < len && html[k] != '>') k++;
              char inner[12];
              size_t copy = k - j;
              if (copy >= sizeof(inner)) copy = sizeof(inner) - 1;
              size_t m;
              for (m = 0; m < copy; m++)
                inner[m] = (char)(html[j + m] | 32);
              inner[m] = '\0';
              if (kstreq(inner, "style")) { pos = k + 1; break; }
            }
            if (used < cap) doc->style_text[used++] = html[end];
            end++;
          }
          doc->style_text[used] = '\0';
          if (pos == end) pos = end;
        }
        continue;
      }
      if (hv_streq_ci(tag, "base")) {
        if (!doc->base_url[0]) {
          hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                "href", doc->base_url, sizeof(doc->base_url));
        }
        continue;
      }
      if (hv_streq_ci(tag, "meta")) {
        static char http_equiv[64];
        static char content[HTML_URL_MAX];
        static char refresh_url[HTML_URL_MAX];
        uint32_t refresh_delay = 0;
        http_equiv[0] = '\0';
        content[0] = '\0';
        refresh_url[0] = '\0';
        hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                              "http-equiv", http_equiv, sizeof(http_equiv));
        hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                              "content", content, sizeof(content));
        if (hv_streq_ci(http_equiv, "refresh") &&
            hv_parse_meta_refresh_content(content, &refresh_delay,
                                          refresh_url, sizeof(refresh_url))) {
          kstrcpy(doc->meta_refresh_url, sizeof(doc->meta_refresh_url),
                  refresh_url);
          doc->meta_refresh_delay = refresh_delay;
        }
        continue;
      }
      /* <link rel="stylesheet" href="..."> — queue for external CSS fetch */
      if (hv_streq_ci(tag, "link")) {
        static char rel[32];
        static char as_attr[32];
        static char link_href[HTML_URL_MAX];
        rel[0] = '\0'; as_attr[0] = '\0'; link_href[0] = '\0';
        hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                              "rel", rel, sizeof(rel));
        hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                              "as", as_attr, sizeof(as_attr));
        hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                              "href", link_href, sizeof(link_href));
        if (link_href[0] && doc->css_count < HTML_MAX_PENDING_CSS) {
          if (hv_token_list_contains_ci(rel, "stylesheet") ||
              (hv_token_list_contains_ci(rel, "preload") &&
               hv_streq_ci(as_attr, "style"))) {
            hv_doc_queue_pending_css(doc, link_href);
          }
        }
        continue;
      }
      if (hv_streq_ci(tag, "script") ||
          hv_streq_ci(tag, "svg") || hv_streq_ci(tag, "template") ||
          hv_streq_ci(tag, "object") || hv_streq_ci(tag, "embed") ||
          hv_streq_ci(tag, "iframe")) {
        if (!self_closing) pos = hv_skip_block(html, len, pos, tag);
        continue;
      }
      if (hv_streq_ci(tag, "form")) {
        method_attr[0] = '\0';
        if (!hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                   "action", current_form_action,
                                   sizeof(current_form_action))) {
          current_form_action[0] = '\0';
        }
        (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                    "method", method_attr, sizeof(method_attr));
        current_form_method = hv_parse_form_method(method_attr);
        continue;
      }
      if (hv_streq_ci(tag, "title")) {
        pos = hv_collect_text_until_tag(html, len, pos, tag,
                                        doc->title, sizeof(doc->title));
        continue;
      }
      if (hv_streq_ci(tag, "hr")) {
        struct html_node *node = html_push_node(doc);
        if (node) {
          node->type = HTML_NODE_TAG_HR;
          hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        }
        continue;
      }
      if (hv_streq_ci(tag, "br")) {
        struct html_node *node = html_push_node(doc);
        if (node) node->type = HTML_NODE_TAG_BR;
        continue;
      }
      if (hv_streq_ci(tag, "ol")) { list_ordered = 1; list_num = 1; continue; }
      if (hv_streq_ci(tag, "source")) {
        static char source_type[64];
        static char source_src[HTML_URL_MAX];
        static char source_srcset[HTML_URL_MAX];
        source_type[0] = '\0';
        source_src[0] = '\0';
        source_srcset[0] = '\0';
        hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                              "type", source_type, sizeof(source_type));
        hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                              "src", source_src, sizeof(source_src));
        if (!source_src[0] &&
            hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                  "srcset", source_srcset,
                                  sizeof(source_srcset))) {
          hv_extract_srcset_first_url(source_srcset, source_src,
                                      sizeof(source_src));
        }
        if (!current_picture_source[0] && source_src[0] &&
            hv_image_type_supported_by_decoder(source_type)) {
          kstrcpy(current_picture_source, sizeof(current_picture_source),
                  source_src);
        }
        continue;
      }
      if (hv_streq_ci(tag, "picture")) {
        current_picture_source[0] = '\0';
        continue;
      }
      if (hv_streq_ci(tag, "html") || hv_streq_ci(tag, "body") ||
          hv_streq_ci(tag, "head") || hv_streq_ci(tag, "main") ||
          hv_streq_ci(tag, "header") || hv_streq_ci(tag, "footer") ||
          hv_streq_ci(tag, "nav") || hv_streq_ci(tag, "aside") ||
          hv_streq_ci(tag, "section") || hv_streq_ci(tag, "article") ||
          hv_streq_ci(tag, "figure") ||
          hv_streq_ci(tag, "ul") || hv_streq_ci(tag, "menu") ||
          hv_streq_ci(tag, "table") || hv_streq_ci(tag, "tbody") ||
          hv_streq_ci(tag, "thead") || hv_streq_ci(tag, "tfoot") ||
          hv_streq_ci(tag, "colgroup") || hv_streq_ci(tag, "col") ||
          hv_streq_ci(tag, "caption") ||
          hv_streq_ci(tag, "div") || hv_streq_ci(tag, "span") ||
          hv_streq_ci(tag, "map") || hv_streq_ci(tag, "area") ||
          hv_streq_ci(tag, "form") || hv_streq_ci(tag, "fieldset") ||
          hv_streq_ci(tag, "legend") || hv_streq_ci(tag, "optgroup") ||
          hv_streq_ci(tag, "datalist") || hv_streq_ci(tag, "dialog") ||
          hv_streq_ci(tag, "slot") || hv_streq_ci(tag, "portal")) {
        continue;
      }
      /* Table row separator */
      if (hv_streq_ci(tag, "tr")) {
        struct html_node *node = html_push_node(doc);
        if (node) {
          node->type = HTML_NODE_TAG_TR;
          hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        }
        continue;
      }
      /* Table cells */
      if (hv_streq_ci(tag, "td") || hv_streq_ci(tag, "th")) {
        struct html_node *node;
        text[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
        if (!text[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_TD;
        node->bold = hv_streq_ci(tag, "th") ? 1 : 0;
        kstrcpy(node->text, sizeof(node->text), text);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Sub-headings h4-h6 */
      if (hv_streq_ci(tag, "h4") || hv_streq_ci(tag, "h5") ||
          hv_streq_ci(tag, "h6")) {
        struct html_node *node;
        text[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
        if (!text[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = hv_streq_ci(tag, "h4") ? HTML_NODE_TAG_H4 :
                     hv_streq_ci(tag, "h5") ? HTML_NODE_TAG_H5 :
                                              HTML_NODE_TAG_H6;
        kstrcpy(node->text, sizeof(node->text), text);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Preformatted text and inline code */
      if (hv_streq_ci(tag, "pre") || hv_streq_ci(tag, "code") ||
          hv_streq_ci(tag, "samp") || hv_streq_ci(tag, "kbd")) {
        struct html_node *node;
        text[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
        if (!text[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = hv_streq_ci(tag, "pre") ? HTML_NODE_TAG_PRE
                                              : HTML_NODE_TAG_CODE;
        kstrcpy(node->text, sizeof(node->text), text);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Blockquote */
      if (hv_streq_ci(tag, "blockquote")) {
        struct html_node *node;
        text[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
        if (!text[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_BLOCKQUOTE;
        node->indent = 1;
        kstrcpy(node->text, sizeof(node->text), text);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Inline emphasis: strong, b, em, i, mark, abbr, cite, small, s, del */
      if (hv_streq_ci(tag, "strong") || hv_streq_ci(tag, "b") ||
          hv_streq_ci(tag, "em") || hv_streq_ci(tag, "i") ||
          hv_streq_ci(tag, "mark") || hv_streq_ci(tag, "abbr") ||
          hv_streq_ci(tag, "cite") || hv_streq_ci(tag, "small") ||
          hv_streq_ci(tag, "s") || hv_streq_ci(tag, "del") ||
          hv_streq_ci(tag, "ins") || hv_streq_ci(tag, "u")) {
        struct html_node *node;
        text[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
        if (!text[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = hv_streq_ci(tag, "mark") ? HTML_NODE_TAG_MARK
                                               : HTML_NODE_TEXT;
        node->bold = (hv_streq_ci(tag, "strong") || hv_streq_ci(tag, "b")) ? 1
                                                                             : 0;
        kstrcpy(node->text, sizeof(node->text), text);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Figcaption */
      if (hv_streq_ci(tag, "figcaption")) {
        struct html_node *node;
        text[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
        if (!text[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_FIGCAPTION;
        kstrcpy(node->text, sizeof(node->text), text);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Details/summary collapsible */
      if (hv_streq_ci(tag, "details") || hv_streq_ci(tag, "summary")) {
        struct html_node *node;
        text[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
        if (!text[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_DETAILS;
        kstrcpy(node->text, sizeof(node->text), text);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Video and audio: skip block content, show placeholder */
      if (hv_streq_ci(tag, "video") || hv_streq_ci(tag, "audio")) {
        struct html_node *node;
        if (!self_closing) pos = hv_skip_block(html, len, pos, tag);
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_MEDIA;
        kstrcpy(node->text, sizeof(node->text),
                hv_streq_ci(tag, "video") ? "[video]" : "[audio]");
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }

      href[0] = '\0';
      src[0] = '\0';
      text[0] = '\0';
      name[0] = '\0';
      type_attr[0] = '\0';
      (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                  "href", href, sizeof(href));
      (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                  "src", src, sizeof(src));
      if (!src[0]) {
        (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                    "data-src", src, sizeof(src));
      }
      if (!src[0]) {
        (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                    "data-lazy-src", src, sizeof(src));
      }
      if (!src[0]) {
        (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                    "data-original", src, sizeof(src));
      }
      if (!src[0]) {
        static char srcset[HTML_URL_MAX];
        srcset[0] = '\0';
        if (hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                  "srcset", srcset, sizeof(srcset))) {
          hv_extract_srcset_first_url(srcset, src, sizeof(src));
        }
      }
      if (!src[0] && current_picture_source[0]) {
        kstrcpy(src, sizeof(src), current_picture_source);
      }
      (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                  "name", name, sizeof(name));
      (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                  "type", type_attr, sizeof(type_attr));

      if (hv_streq_ci(tag, "input")) {
        struct html_node *node;
        uint8_t input_type = hv_form_input_type(type_attr);
        (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                    "value", text, sizeof(text));
        node = html_push_node(doc);
        if (!node) break;
        node->type = (input_type == HTML_INPUT_TYPE_SUBMIT ||
                      input_type == HTML_INPUT_TYPE_BUTTON)
                         ? HTML_NODE_TAG_BUTTON
                         : HTML_NODE_TAG_INPUT;
        node->input_type = input_type;
        node->hidden = (input_type == HTML_INPUT_TYPE_HIDDEN);
        node->form_method = current_form_method;
        if (node->type == HTML_NODE_TAG_BUTTON && !text[0]) {
          kstrcpy(text, sizeof(text), "Submit");
        }
        /* For checkbox/radio: store value in text, checked state in open */
        if (input_type == HTML_INPUT_TYPE_CHECKBOX ||
            input_type == HTML_INPUT_TYPE_RADIO) {
          static char chk_val[64];
          chk_val[0] = '\0';
          hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                "checked", chk_val, sizeof(chk_val));
          if (hv_has_boolean_attr(html + attr_start, tag_end - attr_start,
                                  "checked")) {
            node->open = 1;
          }
          if (!text[0]) kstrcpy(text, sizeof(text), "on");
        }
        kstrcpy(node->text, sizeof(node->text), text);
        kstrcpy(node->name, sizeof(node->name), name);
        kstrcpy(node->href, sizeof(node->href), current_form_action);
        (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                    "placeholder", node->placeholder,
                                    sizeof(node->placeholder));
        continue;
      } else if (hv_streq_ci(tag, "select")) {
        /* Collect first <option> value as the default selection */
        struct html_node *node;
        static char opt_text[HTML_TEXT_MAX];
        static char opt_val[HTML_TEXT_MAX];
        size_t scan = pos;
        opt_text[0] = '\0'; opt_val[0] = '\0';
        while (scan < len) {
          if (html[scan] != '<') { scan++; continue; }
          static char stag[32]; size_t sattr = scan + 1; size_t send = scan; int sclose = 0, ssc = 0;
          scan++;
          if (scan < len && html[scan] == '/') { sclose = 1; scan++; }
          hv_read_tag_name(html, len, &scan, stag, sizeof(stag));
          scan = hv_scan_tag_end(html, len, scan, &send, &ssc);
          if (sclose && hv_streq_ci(stag, "select")) break;
          if (!sclose && hv_streq_ci(stag, "option")) {
            hv_extract_attr_value(html + sattr, send - sattr, "value", opt_val, sizeof(opt_val));
            scan = hv_collect_text_until_tag(html, len, scan, "option", opt_text, sizeof(opt_text));
            break;
          }
        }
        pos = hv_skip_block(html, len, pos, "select");
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_INPUT;
        node->input_type = HTML_INPUT_TYPE_TEXT;
        node->form_method = current_form_method;
        kstrcpy(node->text, sizeof(node->text), opt_val[0] ? opt_val : opt_text);
        kstrcpy(node->name, sizeof(node->name), name);
        kstrcpy(node->href, sizeof(node->href), current_form_action);
        continue;
      } else if (hv_streq_ci(tag, "textarea")) {
        struct html_node *node;
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_INPUT;
        node->input_type = HTML_INPUT_TYPE_TEXTAREA;
        node->hidden = 0;
        node->form_method = current_form_method;
        kstrcpy(node->text, sizeof(node->text), text);
        kstrcpy(node->name, sizeof(node->name), name);
        kstrcpy(node->href, sizeof(node->href), current_form_action);
        (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                    "placeholder", node->placeholder,
                                    sizeof(node->placeholder));
        continue;
      } else if (hv_streq_ci(tag, "button")) {
        struct html_node *node;
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_BUTTON;
        node->input_type = hv_streq_ci(type_attr, "button")
                               ? HTML_INPUT_TYPE_BUTTON
                               : HTML_INPUT_TYPE_SUBMIT;
        node->form_method = current_form_method;
        if (!text[0]) kstrcpy(text, sizeof(text), "Submit");
        kstrcpy(node->text, sizeof(node->text), text);
        kstrcpy(node->name, sizeof(node->name), name);
        kstrcpy(node->href, sizeof(node->href), current_form_action);
        continue;
      } else if (hv_streq_ci(tag, "img")) {
        if (!hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                   "alt", text, sizeof(text))) {
          kstrcpy(text, sizeof(text), src);
        }
        if (src[0] && !href[0]) kstrcpy(href, sizeof(href), src);
      } else if (!self_closing &&
                 (hv_streq_ci(tag, "p") || hv_streq_ci(tag, "h1") ||
                  hv_streq_ci(tag, "h2") || hv_streq_ci(tag, "h3") ||
                  hv_streq_ci(tag, "h4") || hv_streq_ci(tag, "h5") ||
                  hv_streq_ci(tag, "h6") || hv_streq_ci(tag, "div") ||
                  hv_streq_ci(tag, "blockquote") || hv_streq_ci(tag, "li"))) {
        /* Use inline content parser: emits separate text + anchor nodes.
         * static: html_node is ~1800 bytes; putting it on stack would overflow
         * html_parse()'s already-heavy ~3 KB frame. Single-threaded = safe. */
        static struct html_node tmpl;
        kmemzero(&tmpl, sizeof(tmpl));
        tmpl.form_method = current_form_method;
        kstrcpy(tmpl.href, sizeof(tmpl.href), current_form_action);
        hv_apply_node_attrs(&tmpl, html + attr_start, tag_end - attr_start);
        /* Set type and style on template based on tag */
        if (hv_streq_ci(tag, "h1"))        { tmpl.type = HTML_NODE_TAG_H1; tmpl.bold = 1; }
        else if (hv_streq_ci(tag, "h2"))   { tmpl.type = HTML_NODE_TAG_H2; tmpl.bold = 1; }
        else if (hv_streq_ci(tag, "h3"))   { tmpl.type = HTML_NODE_TAG_H3; tmpl.bold = 1; }
        else if (hv_streq_ci(tag, "h4"))   { tmpl.type = HTML_NODE_TAG_H4; tmpl.bold = 1; }
        else if (hv_streq_ci(tag, "h5"))   { tmpl.type = HTML_NODE_TAG_H5; tmpl.bold = 1; }
        else if (hv_streq_ci(tag, "h6"))   { tmpl.type = HTML_NODE_TAG_H6; tmpl.bold = 1; }
        else if (hv_streq_ci(tag, "p"))    { tmpl.type = HTML_NODE_TAG_P; }
        else if (hv_streq_ci(tag, "li"))   { tmpl.type = HTML_NODE_TAG_LI; }
        else if (hv_streq_ci(tag, "blockquote")) { tmpl.type = HTML_NODE_TAG_BLOCKQUOTE; tmpl.indent = 24; }
        else                               { tmpl.type = HTML_NODE_TAG_DIV; }
        pos = hv_parse_inline_content(html, len, pos, tag, doc, &tmpl);
        continue;
      } else if (hv_streq_ci(tag, "li") && !self_closing) {
        /* Fallback (should not be reached; handled above) */
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
      } else if (!self_closing) {
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
      }

      if (text[0] || href[0] || src[0]) {
        struct html_node *node = html_push_node(doc);
        if (!node) break;
        if (hv_streq_ci(tag, "h1")) node->type = HTML_NODE_TAG_H1;
        else if (hv_streq_ci(tag, "h2")) node->type = HTML_NODE_TAG_H2;
        else if (hv_streq_ci(tag, "h3")) node->type = HTML_NODE_TAG_H3;
        else if (hv_streq_ci(tag, "p")) node->type = HTML_NODE_TAG_P;
        else if (hv_streq_ci(tag, "a")) node->type = HTML_NODE_TAG_A;
        else if (hv_streq_ci(tag, "div")) node->type = HTML_NODE_TAG_DIV;
        else if (hv_streq_ci(tag, "span")) node->type = HTML_NODE_TAG_SPAN;
        else if (hv_streq_ci(tag, "ul")) node->type = HTML_NODE_TAG_UL;
        else if (hv_streq_ci(tag, "li")) {
          node->type = HTML_NODE_TAG_LI;
          if (list_ordered) {
            static char prefix[16];
            static char new_text[HTML_TEXT_MAX];
            prefix[0] = '\0';
            kbuf_append_u32(prefix, sizeof(prefix), (uint32_t)list_num++);
            kbuf_append(prefix, sizeof(prefix), ". ");
            kstrcpy(new_text, sizeof(new_text), prefix);
            kbuf_append(new_text, sizeof(new_text), text);
            kstrcpy(text, sizeof(text), new_text);
          }
        }
        else if (hv_streq_ci(tag, "img")) node->type = HTML_NODE_TAG_IMG;
        else if (hv_streq_ci(tag, "noscript")) node->type = HTML_NODE_TAG_DIV;
        else if (hv_streq_ci(tag, "kbd") || hv_streq_ci(tag, "samp") ||
                 hv_streq_ci(tag, "var") || hv_streq_ci(tag, "tt"))
            node->type = HTML_NODE_TAG_CODE;
        else if (hv_streq_ci(tag, "abbr") || hv_streq_ci(tag, "acronym") ||
                 hv_streq_ci(tag, "cite") || hv_streq_ci(tag, "q") ||
                 hv_streq_ci(tag, "address") || hv_streq_ci(tag, "time") ||
                 hv_streq_ci(tag, "small") || hv_streq_ci(tag, "sub") ||
                 hv_streq_ci(tag, "sup") || hv_streq_ci(tag, "s") ||
                 hv_streq_ci(tag, "del") || hv_streq_ci(tag, "ins") ||
                 hv_streq_ci(tag, "u") || hv_streq_ci(tag, "output") ||
                 hv_streq_ci(tag, "label") || hv_streq_ci(tag, "legend") ||
                 hv_streq_ci(tag, "caption"))
            node->type = HTML_NODE_TAG_SPAN;
        else node->type = HTML_NODE_TEXT;
        /* Inline formatting tags that affect rendering style */
        if (hv_streq_ci(tag, "strong") || hv_streq_ci(tag, "b") ||
            hv_streq_ci(tag, "dt")) node->bold = 1;
        if (hv_streq_ci(tag, "em") || hv_streq_ci(tag, "i") ||
            hv_streq_ci(tag, "cite") || hv_streq_ci(tag, "dfn"))
            node->text_align = 3; /* repurpose: 3 = italic indicator */
        if (node->type == HTML_NODE_TAG_A) node->color = 0x89B4FA;
        kstrcpy(node->text, sizeof(node->text), text[0] ? text : href);
        kstrcpy(node->href, sizeof(node->href), href);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
      }
      continue;
    }

    {
      struct html_node *node = html_push_node(doc);
      size_t out_pos = 0;
      int last_space = 1;
      if (!node) break;
      while (pos < len && html[pos] != '<') {
        if (html[pos] == '&') {
          size_t consumed = 0;
          char decoded = 0;
          if (hv_decode_entity_value(html + pos, len - pos, &consumed, &decoded)) {
            hv_text_append_char(node->text, sizeof(node->text), &out_pos, decoded, &last_space);
            pos += consumed;
            continue;
          }
        }
        hv_text_append_char(node->text, sizeof(node->text), &out_pos, html[pos], &last_space);
        pos++;
      }
      hv_trim_text(node->text);
      if (!node->text[0]) doc->node_count--;
    }
  }
  if (!doc->title[0]) {
    for (int i = 0; i < doc->node_count; i++) {
      if (doc->nodes[i].type == HTML_NODE_TAG_H1 && doc->nodes[i].text[0]) {
        kstrcpy(doc->title, sizeof(doc->title), doc->nodes[i].text);
        break;
      }
    }
  }
  /* Fill col_index / col_count on TD nodes for side-by-side table rendering */
  {
    int i;
    for (i = 0; i < doc->node_count; i++) {
      if (doc->nodes[i].type == HTML_NODE_TAG_TR) {
        int j;
        int td_indices[64];
        int ncols = 0;
        /* Collect TD node indices until next TR or TABLE-level tag */
        for (j = i + 1; j < doc->node_count && ncols < 64; j++) {
          enum html_node_type t = doc->nodes[j].type;
          if (t == HTML_NODE_TAG_TR) break;  /* next row */
          if (t == HTML_NODE_TAG_TD) td_indices[ncols++] = j;
          /* Only skip text/span/formatting nodes; stop on block-level */
          else if (t == HTML_NODE_TAG_P || t == HTML_NODE_TAG_DIV ||
                   t == HTML_NODE_TAG_H1 || t == HTML_NODE_TAG_H2 ||
                   t == HTML_NODE_TAG_H3 || t == HTML_NODE_TAG_UL) break;
        }
        if (ncols > 255) ncols = 255;
        for (j = 0; j < ncols; j++) {
          doc->nodes[td_indices[j]].col_index = (uint8_t)j;
          doc->nodes[td_indices[j]].col_count = (uint8_t)ncols;
        }
      }
    }
  }
  /* Apply collected <style> block to all nodes */
  if (doc->style_text[0]) {
    static struct css_stylesheet sheet;
    kmemzero(&sheet, sizeof(sheet));
    if (css_parse(doc->style_text, kstrlen(doc->style_text), &sheet) == 0)
      css_apply_to_doc(&sheet, doc);
  }
  /* Propagate body/html CSS color (text color only) to nodes without explicit color.
   * Background color is handled globally in the paint function, not per-node. */
  {
    uint32_t body_color = 0;
    int i;
    for (i = 0; i < doc->node_count; i++) {
      if (doc->nodes[i].type == HTML_NODE_TAG_BODY ||
          doc->nodes[i].type == HTML_NODE_TAG_HTML) {
        if (doc->nodes[i].css_color) body_color = doc->nodes[i].css_color;
      }
    }
    if (body_color) {
      for (i = 0; i < doc->node_count; i++) {
        if (!doc->nodes[i].css_color &&
            doc->nodes[i].type != HTML_NODE_TAG_BODY &&
            doc->nodes[i].type != HTML_NODE_TAG_HTML)
          doc->nodes[i].css_color = body_color;
      }
    }
  }
  /* Flex propagation: UL/NAV/DIV with display:flex → force LI/DIV children inline.
   * The flat DOM doesn't have parent pointers, so we use the node immediately following
   * the flex container and treat the next run of sibling-candidate nodes as inline-block. */
  {
    int i;
    for (i = 0; i < doc->node_count - 1; i++) {
      struct html_node *container = &doc->nodes[i];
      enum html_node_type ct = container->type;
      if (container->css_display != 3) continue;
      /* Only propagate for list/block containers */
      if (ct != HTML_NODE_TAG_UL && ct != HTML_NODE_TAG_DIV &&
          ct != HTML_NODE_TAG_SPAN && ct != HTML_NODE_TAG_BODY &&
          ct != HTML_NODE_TAG_HTML) continue;
      /* Mark the following LI/DIV/SPAN/A run as inline-block */
      for (int j = i + 1; j < doc->node_count; j++) {
        struct html_node *child = &doc->nodes[j];
        enum html_node_type jt = child->type;
        /* Stop at another block container or at a node that changes context */
        if (jt == HTML_NODE_TAG_UL || jt == HTML_NODE_TAG_BODY ||
            jt == HTML_NODE_TAG_HTML || jt == HTML_NODE_TAG_HEAD)
          break;
        if (child->css_display == 0 &&
            (jt == HTML_NODE_TAG_LI || jt == HTML_NODE_TAG_DIV ||
             jt == HTML_NODE_TAG_SPAN || jt == HTML_NODE_TAG_P ||
             jt == HTML_NODE_TAG_A))
          child->css_display = 2; /* inline-block */
      }
    }
  }
  return 0;
}

