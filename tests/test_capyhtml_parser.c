/* Host tests for libcapyhtml (F3.3c Slice 2).
 *
 * The parser TU is freestanding (no <string.h>) and lives under
 * userland/lib/capyhtml/. The host test build compiles it via the
 * standard HOST_CFLAGS so we can exercise it with the system stdio /
 * assert headers without polluting the parser source.
 *
 * Coverage locks in the Slice 2 contract documented in
 * userland/lib/capyhtml/include/capyhtml/parser.h:
 *   - rejects NULL inputs,
 *   - extracts a <title>,
 *   - falls back to first <h1> when <title> is absent,
 *   - emits the right node types for the supported tags,
 *   - decodes the supported entities,
 *   - extracts href on <a>,
 *   - skips <script> / <style> / <head> contents,
 *   - collapses internal whitespace,
 *   - handles void <br> / <hr>,
 *   - the yield callback fires.
 */
#include <stdio.h>
#include <string.h>

#include "capyhtml/parser.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                       \
    do {                                                                 \
        tests_run++;                                                     \
        printf("  %-58s ", name);                                        \
    } while (0)
#define PASS()                                                           \
    do {                                                                 \
        printf("OK\n");                                                  \
        tests_passed++;                                                  \
    } while (0)
#define FAIL(msg) printf("FAIL: %s\n", msg)

static struct capyhtml_document g_doc;

static void parse(const char *html) {
    int rc = capyhtml_parse(html, strlen(html), &g_doc, NULL, NULL);
    if (rc != 0) {
        printf("[unexpected -1 from capyhtml_parse]\n");
    }
}

static int find_node_of_type(enum capyhtml_node_type t) {
    int i;
    for (i = 0; i < g_doc.node_count; i++) {
        if (g_doc.nodes[i].type == t) return i;
    }
    return -1;
}

static int count_nodes_of_type(enum capyhtml_node_type t) {
    int i;
    int n = 0;
    for (i = 0; i < g_doc.node_count; i++) {
        if (g_doc.nodes[i].type == t) n++;
    }
    return n;
}

static unsigned g_yield_calls = 0;
static void counting_yield(void *user_data) {
    (void)user_data;
    g_yield_calls++;
}

int test_capyhtml_parser_run(void) {
    printf("[test_capyhtml_parser]\n");
    tests_run = 0;
    tests_passed = 0;

    /* 1. NULL safety. */
    TEST("capyhtml_parse(NULL, ...) returns -1");
    if (capyhtml_parse(NULL, 0, &g_doc, NULL, NULL) == -1) PASS();
    else FAIL("did not reject NULL html");

    TEST("capyhtml_parse(..., NULL doc) returns -1");
    if (capyhtml_parse("<p>x</p>", 8, NULL, NULL, NULL) == -1) PASS();
    else FAIL("did not reject NULL doc");

    /* 2. Empty input. */
    parse("");
    TEST("empty input produces zero nodes");
    if (g_doc.node_count == 0) PASS();
    else FAIL("expected 0 nodes");

    /* 3. Title extraction. */
    parse("<html><head><title>Hello</title></head><body><p>x</p></body></html>");
    TEST("<title> populates doc.title");
    if (strcmp(g_doc.title, "Hello") == 0) PASS();
    else FAIL("title mismatch");

    TEST("<title> + body emits at least one P node");
    if (find_node_of_type(CAPYHTML_NODE_TAG_P) >= 0) PASS();
    else FAIL("no P node");

    /* 4. Title fallback to first H1. */
    parse("<h1>capyland</h1><p>greetings</p>");
    TEST("title falls back to first H1 when <title> absent");
    if (strcmp(g_doc.title, "capyland") == 0) PASS();
    else FAIL("h1-fallback title mismatch");

    /* 5. Node typing and counts. */
    TEST("H1 + P emits exactly one of each");
    if (count_nodes_of_type(CAPYHTML_NODE_TAG_H1) == 1 &&
        count_nodes_of_type(CAPYHTML_NODE_TAG_P) == 1) PASS();
    else FAIL("expected 1 H1 and 1 P");

    /* 6. H1 carries bold flag. */
    {
        int i = find_node_of_type(CAPYHTML_NODE_TAG_H1);
        TEST("H1 node is flagged bold=1");
        if (i >= 0 && g_doc.nodes[i].bold == 1) PASS();
        else FAIL("h1 bold flag missing");
    }

    /* 7. <a href> extraction. */
    parse("<p>see <a href=\"https://capy.os/docs\">docs</a></p>");
    {
        int i = find_node_of_type(CAPYHTML_NODE_TAG_A);
        TEST("<a> emits href and text");
        if (i >= 0 &&
            strcmp(g_doc.nodes[i].text, "docs") == 0 &&
            strcmp(g_doc.nodes[i].href, "https://capy.os/docs") == 0) PASS();
        else FAIL("anchor href/text mismatch");
    }

    /* 8. UL/LI structure. */
    parse("<ul><li>one</li><li>two</li><li>three</li></ul>");
    TEST("UL produces UL marker + 3 LI nodes");
    if (count_nodes_of_type(CAPYHTML_NODE_TAG_UL) == 1 &&
        count_nodes_of_type(CAPYHTML_NODE_TAG_LI) == 3) PASS();
    else FAIL("UL/LI structure mismatch");

    /* 9. Void tags. */
    parse("<p>line<br>continued</p><hr>");
    TEST("<br> emits a TAG_BR node");
    if (count_nodes_of_type(CAPYHTML_NODE_TAG_BR) >= 1) PASS();
    else FAIL("no BR node");

    TEST("<hr> emits a TAG_HR node");
    if (count_nodes_of_type(CAPYHTML_NODE_TAG_HR) >= 1) PASS();
    else FAIL("no HR node");

    /* 10. Entities. */
    parse("<p>5 &lt; 10 &amp;&amp; &quot;ok&quot;</p>");
    {
        int i = find_node_of_type(CAPYHTML_NODE_TAG_P);
        TEST("entities &lt; &amp; &quot; are decoded");
        if (i >= 0 &&
            strcmp(g_doc.nodes[i].text, "5 < 10 && \"ok\"") == 0) PASS();
        else FAIL("entity decoding wrong");
    }

    /* 11. Whitespace collapsing. */
    parse("<p>  multiple   \t\nspaces  </p>");
    {
        int i = find_node_of_type(CAPYHTML_NODE_TAG_P);
        TEST("internal whitespace runs collapse to a single space");
        if (i >= 0 && strcmp(g_doc.nodes[i].text, "multiple spaces") == 0) PASS();
        else FAIL("whitespace not collapsed");
    }

    /* 12. <script> / <style> / <head> are skipped. */
    parse("<head><title>T</title>"
          "<script>var x = 1;</script>"
          "<style>p { color: red; }</style>"
          "</head>"
          "<body><p>visible</p></body>");
    TEST("contents of <script>/<style>/<head> do NOT leak as text nodes");
    {
        int i;
        int leaked = 0;
        for (i = 0; i < g_doc.node_count; i++) {
            if (strstr(g_doc.nodes[i].text, "var x") ||
                strstr(g_doc.nodes[i].text, "color: red")) {
                leaked = 1;
                break;
            }
        }
        if (!leaked) PASS();
        else FAIL("script or style content leaked");
    }

    TEST("body <p> survives the head skip");
    {
        int i = find_node_of_type(CAPYHTML_NODE_TAG_P);
        if (i >= 0 && strcmp(g_doc.nodes[i].text, "visible") == 0) PASS();
        else FAIL("body P not preserved");
    }

    /* 13. Comment skip. */
    parse("<p>before</p><!-- this should vanish --><p>after</p>");
    TEST("<!-- comments --> are skipped");
    if (count_nodes_of_type(CAPYHTML_NODE_TAG_P) == 2) PASS();
    else FAIL("comment leaked or split a P");

    /* 14. DOCTYPE skip. */
    parse("<!doctype html><p>doc</p>");
    TEST("<!doctype> is skipped without producing a node");
    if (count_nodes_of_type(CAPYHTML_NODE_TAG_P) == 1 &&
        find_node_of_type(CAPYHTML_NODE_TEXT) < 0) PASS();
    else FAIL("doctype leaked");

    /* 15. Yield callback fires. We push enough HTML to cross the
     * 1024-iteration threshold. The yield-every is internal (not
     * exposed), so we just verify it fires at least once. */
    {
        char big[8192];
        size_t i;
        for (i = 0; i < sizeof(big) - 1; i++) big[i] = 'x';
        big[sizeof(big) - 1] = '\0';
        g_yield_calls = 0;
        capyhtml_parse(big, sizeof(big) - 1, &g_doc, counting_yield, NULL);
        TEST("yield callback fires at least once on long input");
        if (g_yield_calls >= 1) PASS();
        else FAIL("yield never fired");
    }

    /* 16. Etapa 3 b-polish++ (2026-05-03): error page template that
     * the capybrowser engine builds on fetch failure. The parser
     * must accept it and produce (a) title "Pagina nao carregou"
     * via <h1> fallback, (b) 3 <p> nodes with the URL / reason /
     * hints, (c) no stray TEXT nodes outside tags. Pin this so a
     * regression in the parser that breaks the error page is
     * caught before it ships -- the user would silently see a blue
     * stub again and not know why navigation failed. */
    {
        const char *error_html =
            "<h1>Pagina nao carregou</h1>"
            "<p>Endereco: http://example.com/missing</p>"
            "<p>Motivo: fetch_status=404</p>"
            "<p>F5 recarrega, F6 volta, F7 avanca, Esc fecha.</p>";
        parse(error_html);
        TEST("error template: 1 H1 + 3 P = 4 container-level nodes");
        if (count_nodes_of_type(CAPYHTML_NODE_TAG_H1) == 1 &&
            count_nodes_of_type(CAPYHTML_NODE_TAG_P) == 3) PASS();
        else FAIL("error template shape diverged");

        TEST("error template: title fallback picks H1 text");
        if (strcmp(g_doc.title, "Pagina nao carregou") == 0) PASS();
        else FAIL("title not derived from H1");

        TEST("error template: URL survives verbatim inside P");
        {
            int found = 0;
            for (int i = 0; i < g_doc.node_count; ++i) {
                if (g_doc.nodes[i].type == CAPYHTML_NODE_TAG_P &&
                    strstr(g_doc.nodes[i].text,
                           "http://example.com/missing")) {
                    found = 1; break;
                }
            }
            if (found) PASS();
            else FAIL("URL lost during parse");
        }

        TEST("error template: reason fetch_status=404 survives");
        {
            int found = 0;
            for (int i = 0; i < g_doc.node_count; ++i) {
                if (g_doc.nodes[i].type == CAPYHTML_NODE_TAG_P &&
                    strstr(g_doc.nodes[i].text, "fetch_status=404")) {
                    found = 1; break;
                }
            }
            if (found) PASS();
            else FAIL("reason string lost during parse");
        }

        TEST("error template: hotkey hint mentions F5/F6/F7");
        {
            int found = 0;
            for (int i = 0; i < g_doc.node_count; ++i) {
                if (g_doc.nodes[i].type == CAPYHTML_NODE_TAG_P &&
                    strstr(g_doc.nodes[i].text, "F5") &&
                    strstr(g_doc.nodes[i].text, "F6") &&
                    strstr(g_doc.nodes[i].text, "F7")) {
                    found = 1; break;
                }
            }
            if (found) PASS();
            else FAIL("hotkey hint lost during parse");
        }
    }

    /* 17. Etapa 3 seção a (2026-05-03): <img> é void tag; produz
     * um NODE_TAG_IMG com src em `href` e alt em `text`. Sem src,
     * o node ainda é emitido mas com href vazio -- o renderer
     * trata como placeholder mudo. Pin estas formas para que uma
     * regressao no extract_attr ou na detecção de `<img>` seja
     * caught cedo. */
    {
        parse("<p>antes</p><img src=\"foo.png\" alt=\"logo\"><p>depois</p>");
        TEST("<img> produces TAG_IMG node");
        if (count_nodes_of_type(CAPYHTML_NODE_TAG_IMG) == 1) PASS();
        else FAIL("IMG node not produced");

        TEST("<img src=..> captures src into href field");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_IMG);
            if (idx >= 0 && strcmp(g_doc.nodes[idx].href, "foo.png") == 0)
                PASS();
            else FAIL("src not copied to href");
        }

        TEST("<img alt=..> captures alt into text field");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_IMG);
            if (idx >= 0 && strcmp(g_doc.nodes[idx].text, "logo") == 0)
                PASS();
            else FAIL("alt not copied to text");
        }

        TEST("<img> does NOT swallow surrounding <p> nodes");
        if (count_nodes_of_type(CAPYHTML_NODE_TAG_P) == 2) PASS();
        else FAIL("P count wrong after <img>");
    }
    {
        /* Sem src: node ainda é emitido para que o layout saiba que
         * há um slot de imagem, mas href fica vazio. */
        parse("<img alt=\"x\">");
        TEST("<img> without src still produces IMG node");
        if (count_nodes_of_type(CAPYHTML_NODE_TAG_IMG) == 1) PASS();
        else FAIL("IMG without src dropped");

        TEST("<img> without src has empty href");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_IMG);
            if (idx >= 0 && g_doc.nodes[idx].href[0] == '\0') PASS();
            else FAIL("missing src produced garbage href");
        }
    }
    {
        /* Self-closing form e variantes de espaço devem funcionar. */
        parse("<img src='a.jpg' />");
        TEST("<img ... /> self-closing form works");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_IMG);
            if (idx >= 0 && strcmp(g_doc.nodes[idx].href, "a.jpg") == 0)
                PASS();
            else FAIL("self-closing <img> failed");
        }
    }

    /* 17b. Etapa 3 seção a refinement (2026-05-05): <img width="W"
     * height="H"> sao parseados e empacotados em bold + reserved[3]
     * via macros CAPYHTML_IMG_GET_WIDTH/HEIGHT. 0 = atributo ausente
     * (render usa defaults 100×80). */
    {
        parse("<img src=\"x.png\" width=\"320\" height=\"240\">");
        TEST("<img width> stored in node (recoverable via macro)");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_IMG);
            if (idx >= 0 &&
                CAPYHTML_IMG_GET_WIDTH(&g_doc.nodes[idx]) == 320) {
                PASS();
            } else FAIL("img width not stored");
        }
        TEST("<img height> stored in node (recoverable via macro)");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_IMG);
            if (idx >= 0 &&
                CAPYHTML_IMG_GET_HEIGHT(&g_doc.nodes[idx]) == 240) {
                PASS();
            } else FAIL("img height not stored");
        }
    }
    {
        /* Sem width/height atributos: macros retornam 0 (sentinela
         * "usar defaults"). */
        parse("<img src=\"y.png\">");
        TEST("<img> without width/height attrs: macros return 0");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_IMG);
            if (idx >= 0 &&
                CAPYHTML_IMG_GET_WIDTH(&g_doc.nodes[idx]) == 0 &&
                CAPYHTML_IMG_GET_HEIGHT(&g_doc.nodes[idx]) == 0) {
                PASS();
            } else FAIL("missing width/height did not yield 0");
        }
    }
    {
        /* width="0" deve ser tratado como ausente (sentinela 0). */
        parse("<img src=\"z.png\" width=\"0\" height=\"50\">");
        TEST("<img width=0> -> 0 (treat as absent)");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_IMG);
            if (idx >= 0 &&
                CAPYHTML_IMG_GET_WIDTH(&g_doc.nodes[idx]) == 0 &&
                CAPYHTML_IMG_GET_HEIGHT(&g_doc.nodes[idx]) == 50) {
                PASS();
            } else FAIL("width=0 not handled as sentinel");
        }
    }
    {
        /* IMG inline dentro de <p> tambem extrai width/height. */
        parse("<p>foto: <img src=\"thumb.png\" "
              "width=\"64\" height=\"48\"> ok</p>");
        TEST("inline <img width=..> dentro de <p> tambem parseado");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_IMG);
            if (idx >= 0 &&
                CAPYHTML_IMG_GET_WIDTH(&g_doc.nodes[idx]) == 64 &&
                CAPYHTML_IMG_GET_HEIGHT(&g_doc.nodes[idx]) == 48) {
                PASS();
            } else FAIL("inline img dims not parsed");
        }
    }
    {
        /* width com sufixo px tolerado. */
        parse("<img src=\"a.png\" width=\"100px\" height=\"75px\">");
        TEST("<img width=\"100px\"> tolera trailing 'px'");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_IMG);
            if (idx >= 0 &&
                CAPYHTML_IMG_GET_WIDTH(&g_doc.nodes[idx]) == 100 &&
                CAPYHTML_IMG_GET_HEIGHT(&g_doc.nodes[idx]) == 75) {
                PASS();
            } else FAIL("trailing px not handled");
        }
    }
    {
        /* Valores absurdos clampados a 65535 (uint16 max do storage). */
        parse("<img src=\"a.png\" width=\"99999999\" height=\"5\">");
        TEST("<img width=99999999> clampa a 65535");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_IMG);
            if (idx >= 0 &&
                CAPYHTML_IMG_GET_WIDTH(&g_doc.nodes[idx]) == 65535 &&
                CAPYHTML_IMG_GET_HEIGHT(&g_doc.nodes[idx]) == 5) {
                PASS();
            } else FAIL("oversized width not clamped");
        }
    }

    /* 18. Etapa 3 seção c (2026-05-03): <form>/<input>. <form> tag
     * empurra um node FORM com action no href; <input> empurra um
     * node INPUT com name no `name`, value no `text`, e subtipo
     * codificado em `bold` (CAPYHTML_INPUT_TYPE_*). */
    {
        parse("<form action=\"/search\">"
              "<input type=\"text\" name=\"q\" value=\"hello\">"
              "<input type=\"submit\" value=\"Go\">"
              "</form>");

        TEST("<form> produces TAG_FORM node");
        if (count_nodes_of_type(CAPYHTML_NODE_TAG_FORM) == 1) PASS();
        else FAIL("FORM not produced");

        TEST("<form action=...> stores action in href");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_FORM);
            if (idx >= 0 && strcmp(g_doc.nodes[idx].href, "/search") == 0)
                PASS();
            else FAIL("form action lost");
        }

        TEST("<input> produces TAG_INPUT nodes (2 total)");
        if (count_nodes_of_type(CAPYHTML_NODE_TAG_INPUT) == 2) PASS();
        else FAIL("expected 2 INPUTs");

        TEST("<input type=text name=q value=hello> stores name+value");
        {
            int found = 0;
            for (int i = 0; i < g_doc.node_count; ++i) {
                if (g_doc.nodes[i].type != CAPYHTML_NODE_TAG_INPUT) continue;
                if (strcmp(g_doc.nodes[i].name, "q") == 0 &&
                    strcmp(g_doc.nodes[i].text, "hello") == 0 &&
                    g_doc.nodes[i].bold ==
                        (uint8_t)CAPYHTML_INPUT_TYPE_TEXT) {
                    found = 1; break;
                }
            }
            if (found) PASS();
            else FAIL("text input missing fields");
        }

        TEST("<input type=submit value=Go> stores label as text");
        {
            int found = 0;
            for (int i = 0; i < g_doc.node_count; ++i) {
                if (g_doc.nodes[i].type != CAPYHTML_NODE_TAG_INPUT) continue;
                if (g_doc.nodes[i].bold ==
                        (uint8_t)CAPYHTML_INPUT_TYPE_SUBMIT &&
                    strcmp(g_doc.nodes[i].text, "Go") == 0) {
                    found = 1; break;
                }
            }
            if (found) PASS();
            else FAIL("submit input missing label");
        }
    }
    {
        /* type=password: subtipo correto. */
        parse("<input type=\"password\" name=\"pw\">");
        TEST("<input type=password> sets PASSWORD subtype");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_INPUT);
            if (idx >= 0 &&
                g_doc.nodes[idx].bold ==
                    (uint8_t)CAPYHTML_INPUT_TYPE_PASSWORD)
                PASS();
            else FAIL("password subtype not set");
        }
    }
    {
        /* type=hidden: silently dropped. */
        parse("<input type=\"hidden\" name=\"csrf\" value=\"xyz\">");
        TEST("<input type=hidden> is silently dropped");
        if (count_nodes_of_type(CAPYHTML_NODE_TAG_INPUT) == 0) PASS();
        else FAIL("hidden input was emitted");
    }
    {
        /* <input> sem type default to text. */
        parse("<input name=\"foo\">");
        TEST("<input> without type defaults to TEXT");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_INPUT);
            if (idx >= 0 &&
                g_doc.nodes[idx].bold ==
                    (uint8_t)CAPYHTML_INPUT_TYPE_TEXT)
                PASS();
            else FAIL("default type wrong");
        }
    }
    {
        /* <input type=submit> sem value usa "Submit" como label. */
        parse("<input type=\"submit\">");
        TEST("<input type=submit> without value uses 'Submit' label");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_INPUT);
            if (idx >= 0 && strcmp(g_doc.nodes[idx].text, "Submit") == 0)
                PASS();
            else FAIL("default submit label wrong");
        }
    }

    /* === Etapa 3 seção d (2026-05-03): tabelas =========================
     *
     * Testes cobrem:
     *   - <table> emite TAG_TABLE; <tr> emite TAG_TR; <td>/<th> emitem
     *     TAG_TD/TAG_TH com texto.
     *   - <th> seta bold=1 (raster usa para HEADING color).
     *   - Tabela 2x3 emite numero correto de cada tipo.
     *   - Texto dentro de <td> e capturado e trimmed.
     */
    {
        parse("<table><tr><th>A</th><th>B</th></tr>"
              "<tr><td>1</td><td>2</td></tr></table>");
        TEST("<table> emits TAG_TABLE node");
        if (count_nodes_of_type(CAPYHTML_NODE_TAG_TABLE) == 1) PASS();
        else FAIL("expected 1 TABLE");

        TEST("two <tr> emit two TAG_TR nodes");
        if (count_nodes_of_type(CAPYHTML_NODE_TAG_TR) == 2) PASS();
        else FAIL("expected 2 TR");

        TEST("<th>x2 emits two TAG_TH nodes");
        if (count_nodes_of_type(CAPYHTML_NODE_TAG_TH) == 2) PASS();
        else FAIL("expected 2 TH");

        TEST("<td>x2 emits two TAG_TD nodes");
        if (count_nodes_of_type(CAPYHTML_NODE_TAG_TD) == 2) PASS();
        else FAIL("expected 2 TD");

        TEST("<th> nodes carry bold=1");
        {
            int ok = 1;
            for (int i = 0; i < g_doc.node_count; ++i) {
                if (g_doc.nodes[i].type == CAPYHTML_NODE_TAG_TH &&
                    g_doc.nodes[i].bold != 1) {
                    ok = 0; break;
                }
            }
            if (ok) PASS();
            else FAIL("TH bold=1 missing");
        }

        TEST("<td> nodes carry bold=0");
        {
            int ok = 1;
            for (int i = 0; i < g_doc.node_count; ++i) {
                if (g_doc.nodes[i].type == CAPYHTML_NODE_TAG_TD &&
                    g_doc.nodes[i].bold != 0) {
                    ok = 0; break;
                }
            }
            if (ok) PASS();
            else FAIL("TD bold!=0");
        }

        TEST("first <th> text == 'A'");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_TH);
            if (idx >= 0 && strcmp(g_doc.nodes[idx].text, "A") == 0) PASS();
            else FAIL("first TH text wrong");
        }

        TEST("first <td> text == '1'");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_TD);
            if (idx >= 0 && strcmp(g_doc.nodes[idx].text, "1") == 0) PASS();
            else FAIL("first TD text wrong");
        }
    }
    {
        /* Tabela com texto inline complexo na celula. */
        parse("<table><tr><td>  hello  world  </td></tr></table>");
        TEST("<td> text trimmed and whitespace collapsed");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_TD);
            if (idx >= 0 &&
                strcmp(g_doc.nodes[idx].text, "hello world") == 0) PASS();
            else FAIL("td text not trimmed");
        }
    }

    /* === Etapa 3 seção d refinement (2026-05-03): colspan ============== */
    {
        /* colspan="2" armazenado em reserved[0]. Default sem colspan
         * deixa reserved[0] = 0 (que o render trata como 1). */
        parse("<table><tr><td colspan=\"2\">wide</td><td>narrow</td></tr>"
              "<tr><td>a</td><td>b</td><td>c</td></tr></table>");
        TEST("<td colspan=\"2\"> sets reserved[0] = 2");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_TD);
            if (idx >= 0 && g_doc.nodes[idx].reserved[0] == 2) PASS();
            else FAIL("colspan not stored");
        }
        TEST("second <td> without colspan keeps reserved[0] = 0");
        {
            int found = -1;
            int seen = 0;
            for (int i = 0; i < g_doc.node_count; ++i) {
                if (g_doc.nodes[i].type == CAPYHTML_NODE_TAG_TD) {
                    if (seen == 1) { found = i; break; }
                    seen++;
                }
            }
            if (found >= 0 && g_doc.nodes[found].reserved[0] == 0) PASS();
            else FAIL("default colspan not 0");
        }
    }
    {
        /* colspan="999" deve clamp em 255 (uint8_t max). */
        parse("<table><tr><td colspan=\"999\">x</td></tr></table>");
        TEST("colspan oversized clamps to 255");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_TD);
            if (idx >= 0 && g_doc.nodes[idx].reserved[0] == 255) PASS();
            else FAIL("clamp wrong");
        }
    }
    {
        /* colspan="0" ou negativo invalido vira 1 (sentinela). */
        parse("<table><tr><td colspan=\"0\">x</td></tr></table>");
        TEST("colspan=0 promovido a 1");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_TD);
            if (idx >= 0 && g_doc.nodes[idx].reserved[0] == 1) PASS();
            else FAIL("colspan zero not promoted");
        }
    }
    {
        /* <th> tambem aceita colspan. */
        parse("<table><tr><th colspan=\"3\">header</th></tr></table>");
        TEST("<th colspan> stored in reserved[0]");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_TH);
            if (idx >= 0 && g_doc.nodes[idx].reserved[0] == 3) PASS();
            else FAIL("th colspan missing");
        }
    }

    /* === Etapa 3 seção c refinement (2026-05-03): textarea ============= */
    {
        parse("<form action=\"/post\">"
              "<textarea name=\"msg\">hello world</textarea>"
              "</form>");
        TEST("<textarea> emits TAG_INPUT with TEXTAREA subtype");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_INPUT);
            if (idx >= 0 &&
                g_doc.nodes[idx].bold ==
                    (uint8_t)CAPYHTML_INPUT_TYPE_TEXTAREA) PASS();
            else FAIL("textarea subtype missing");
        }
        TEST("<textarea> body captured as text");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_INPUT);
            if (idx >= 0 &&
                strcmp(g_doc.nodes[idx].text, "hello world") == 0) PASS();
            else FAIL("textarea body wrong");
        }
        TEST("<textarea> name captured");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_INPUT);
            if (idx >= 0 && strcmp(g_doc.nodes[idx].name, "msg") == 0) PASS();
            else FAIL("textarea name wrong");
        }
    }
    {
        /* textarea vazio: name preserved, text vazio. */
        parse("<textarea name=\"empty\"></textarea>");
        TEST("empty <textarea> -> empty text + name preserved");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_INPUT);
            if (idx >= 0 && g_doc.nodes[idx].text[0] == '\0' &&
                strcmp(g_doc.nodes[idx].name, "empty") == 0) PASS();
            else FAIL("empty textarea wrong");
        }
    }

    /* === Etapa 3 seção c refinement (2026-05-03): <select>/<option> ===== */
    {
        parse("<form action=\"/q\">"
              "<select name=\"color\">"
              "<option value=\"r\">Red</option>"
              "<option value=\"g\">Green</option>"
              "<option value=\"b\">Blue</option>"
              "</select></form>");

        TEST("<select> emits TAG_INPUT with SELECT subtype");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_INPUT);
            if (idx >= 0 &&
                g_doc.nodes[idx].bold ==
                    (uint8_t)CAPYHTML_INPUT_TYPE_SELECT) PASS();
            else FAIL("select subtype missing");
        }
        TEST("<select> name captured");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_INPUT);
            if (idx >= 0 && strcmp(g_doc.nodes[idx].name, "color") == 0) PASS();
            else FAIL("select name wrong");
        }
        TEST("<select> default text = first option label");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_INPUT);
            if (idx >= 0 && strcmp(g_doc.nodes[idx].text, "Red") == 0) PASS();
            else FAIL("select default not first option");
        }
        TEST("3 <option> tags emit 3 TAG_OPTION nodes");
        if (count_nodes_of_type(CAPYHTML_NODE_TAG_OPTION) == 3) PASS();
        else FAIL("expected 3 OPTION nodes");

        TEST("first <option> stores value in name + label in text");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_OPTION);
            if (idx >= 0 &&
                strcmp(g_doc.nodes[idx].name, "r") == 0 &&
                strcmp(g_doc.nodes[idx].text, "Red") == 0) PASS();
            else FAIL("option attrs wrong");
        }
    }
    {
        /* <option> sem value usa label como value (compat HTML). */
        parse("<select name=\"x\">"
              "<option>Plain</option>"
              "</select>");
        TEST("<option> without value uses label as value");
        {
            int idx = find_node_of_type(CAPYHTML_NODE_TAG_OPTION);
            if (idx >= 0 &&
                strcmp(g_doc.nodes[idx].name, "Plain") == 0 &&
                strcmp(g_doc.nodes[idx].text, "Plain") == 0) PASS();
            else FAIL("option fallback value wrong");
        }
    }

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
