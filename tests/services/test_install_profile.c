/*
 * tests/services/test_install_profile.c
 *
 * Host-side coverage for install_profile.c. Pure logic; no VFS or
 * network access. Runs under the standard host test aggregate via
 * `make test`.
 */

#include "services/install_profile.h"

#include <stddef.h>
#include <string.h>
#include <stdio.h>

static int g_failures = 0;
static int g_total = 0;

#define EXPECT(cond, msg)                                                    \
    do {                                                                     \
        ++g_total;                                                           \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            fprintf(stderr,                                                  \
                    "[fail] %s:%d EXPECT(%s) - %s\n",                        \
                    __FILE__, __LINE__, #cond, (msg));                       \
        }                                                                    \
    } while (0)

#define PARSE(text_literal, out_ptr)                                          \
    install_profile_parse((text_literal), sizeof(text_literal) - 1u, (out_ptr))

static void test_empty_defaults_to_basic(void) {
    struct install_profile profile;
    int rc = install_profile_parse(NULL, 0u, &profile);
    EXPECT(rc == INSTALL_PROFILE_OK, "null+empty must be OK");
    EXPECT(profile.kind == INSTALL_PROFILE_BASIC, "default kind must be BASIC");
    EXPECT(profile.valid == 1u, "default must be valid");
    EXPECT(profile.repo_name[0] == '\0', "default repo_name empty");
    EXPECT(profile.repo_url[0] == '\0', "default repo_url empty");
    EXPECT(install_profile_should_bootstrap(&profile) == 0,
           "BASIC must not bootstrap");

    rc = install_profile_parse("", 0u, &profile);
    EXPECT(rc == INSTALL_PROFILE_OK, "empty string must be OK");
    EXPECT(profile.kind == INSTALL_PROFILE_BASIC, "empty stays BASIC");
}

static void test_explicit_basic(void) {
    struct install_profile profile;
    int rc = PARSE("profile=basic\n", &profile);
    EXPECT(rc == INSTALL_PROFILE_OK, "explicit basic must parse");
    EXPECT(profile.kind == INSTALL_PROFILE_BASIC, "kind basic");
    EXPECT(install_profile_should_bootstrap(&profile) == 0, "no bootstrap");
}

static void test_full_profile_valid(void) {
    struct install_profile profile;
    int rc = PARSE(
        "# Test profile\n"
        "profile=full\n"
        "bootstrap_repo_name=modules\n"
        "bootstrap_repo_url=https://example.org/modules-index.txt\n"
        "bootstrap_repo_signed=0\n",
        &profile);
    EXPECT(rc == INSTALL_PROFILE_OK, "full profile must parse");
    EXPECT(profile.kind == INSTALL_PROFILE_FULL, "kind full");
    EXPECT(strcmp(profile.repo_name, "modules") == 0, "repo_name");
    EXPECT(strcmp(profile.repo_url,
                  "https://example.org/modules-index.txt") == 0,
           "repo_url");
    EXPECT(profile.repo_signed == 0u, "repo_signed=0");
    EXPECT(install_profile_should_bootstrap(&profile) == 1,
           "full must bootstrap");
}

static void test_full_without_repo_fails(void) {
    struct install_profile profile;
    int rc = PARSE("profile=full\n", &profile);
    EXPECT(rc == INSTALL_PROFILE_ERR_MISSING_FIELD,
           "full without repo must fail");
    EXPECT(profile.kind == INSTALL_PROFILE_BASIC,
           "reset to BASIC on rejection");
}

static void test_full_url_not_https(void) {
    struct install_profile profile;
    int rc = PARSE(
        "profile=full\n"
        "bootstrap_repo_name=modules\n"
        "bootstrap_repo_url=http://example.org/modules.txt\n",
        &profile);
    EXPECT(rc == INSTALL_PROFILE_ERR_DENIED, "http:// must be rejected");
    EXPECT(profile.kind == INSTALL_PROFILE_BASIC, "reset to BASIC");
}

static void test_custom_with_install_list(void) {
    struct install_profile profile;
    int rc = PARSE(
        "profile=custom\n"
        "bootstrap_repo_name=modules\n"
        "bootstrap_repo_url=https://example.org/modules-index.txt\n"
        "bootstrap_install=org.capyos.codecs.image-basic,"
        "org.capyos.ui.widget-core\n",
        &profile);
    EXPECT(rc == INSTALL_PROFILE_OK, "custom profile must parse");
    EXPECT(profile.kind == INSTALL_PROFILE_CUSTOM, "kind custom");
    EXPECT(install_profile_should_bootstrap(&profile) == 1,
           "custom must bootstrap");

    size_t cursor = 0u;
    char token[INSTALL_PROFILE_NAME_MAX];
    int rc1 = install_profile_install_list_next(profile.install_list,
                                                &cursor, token, sizeof(token));
    EXPECT(rc1 == 0 &&
           strcmp(token, "org.capyos.codecs.image-basic") == 0,
           "first token");
    int rc2 = install_profile_install_list_next(profile.install_list,
                                                &cursor, token, sizeof(token));
    EXPECT(rc2 == 0 &&
           strcmp(token, "org.capyos.ui.widget-core") == 0,
           "second token");
    int rc3 = install_profile_install_list_next(profile.install_list,
                                                &cursor, token, sizeof(token));
    EXPECT(rc3 == -1, "no more tokens");
}

static void test_custom_without_install_list_fails(void) {
    struct install_profile profile;
    int rc = PARSE(
        "profile=custom\n"
        "bootstrap_repo_name=modules\n"
        "bootstrap_repo_url=https://example.org/modules-index.txt\n",
        &profile);
    EXPECT(rc == INSTALL_PROFILE_ERR_MISSING_FIELD,
           "custom without install_list must fail");
}

static void test_install_list_unsafe_token_rejected(void) {
    struct install_profile profile;
    int rc = PARSE(
        "profile=custom\n"
        "bootstrap_repo_name=modules\n"
        "bootstrap_repo_url=https://example.org/modules-index.txt\n"
        "bootstrap_install=valid.name,bad/name\n",
        &profile);
    EXPECT(rc == INSTALL_PROFILE_ERR_DENIED,
           "unsafe install entry must be rejected");
}

static void test_repo_name_alphabet_enforced(void) {
    struct install_profile profile;
    int rc = PARSE(
        "profile=full\n"
        "bootstrap_repo_name=bad name\n"
        "bootstrap_repo_url=https://example.org/modules.txt\n",
        &profile);
    EXPECT(rc == INSTALL_PROFILE_ERR_DENIED,
           "space in repo name must be rejected");
}

static void test_unknown_kind_rejected(void) {
    struct install_profile profile;
    int rc = PARSE("profile=banana\n", &profile);
    EXPECT(rc == INSTALL_PROFILE_ERR_PARSE,
           "unknown profile value must fail");
}

static void test_unknown_key_tolerated(void) {
    struct install_profile profile;
    int rc = PARSE(
        "profile=full\n"
        "bootstrap_repo_name=modules\n"
        "bootstrap_repo_url=https://example.org/modules.txt\n"
        "future_field=hello\n",
        &profile);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "unknown keys must be tolerated forward-compat");
    EXPECT(profile.kind == INSTALL_PROFILE_FULL, "kind preserved");
}

static void test_malformed_line_rejected(void) {
    struct install_profile profile;
    int rc = PARSE(
        "profile=full\n"
        "this line has no equals\n",
        &profile);
    EXPECT(rc == INSTALL_PROFILE_ERR_PARSE,
           "line without '=' must reject the whole file");
}

static void test_control_byte_rejected(void) {
    struct install_profile profile;
    const char text[] =
        "profile=full\n"
        "bootstrap_repo_name=modules\n"
        "bootstrap_repo_url=https://example.org/\x1b[2J\n";
    int rc = install_profile_parse(text, sizeof(text) - 1u, &profile);
    EXPECT(rc == INSTALL_PROFILE_ERR_DENIED,
           "ANSI escape in value must reject the file");
}

static void test_signed_repo_flag(void) {
    struct install_profile profile;
    int rc = PARSE(
        "profile=full\n"
        "bootstrap_repo_name=modules\n"
        "bootstrap_repo_url=https://example.org/modules.txt\n"
        "bootstrap_repo_signed=1\n",
        &profile);
    EXPECT(rc == INSTALL_PROFILE_OK, "signed=1 must parse");
    EXPECT(profile.repo_signed == 1u, "signed flag is 1");
}

static void test_format_basic_round_trip(void) {
    struct install_profile profile;
    install_profile_reset(&profile);
    char buf[256];
    size_t len = 0;
    int rc = install_profile_format(&profile, buf, sizeof(buf), &len);
    EXPECT(rc == INSTALL_PROFILE_OK, "basic format ok");
    EXPECT(strstr(buf, "profile=basic\n") != NULL, "kind line basic present");

    struct install_profile reparsed;
    rc = install_profile_parse(buf, len, &reparsed);
    EXPECT(rc == INSTALL_PROFILE_OK, "basic round-trip parses");
    EXPECT(reparsed.kind == INSTALL_PROFILE_BASIC, "basic kind preserved");
}

static void test_format_full_round_trip(void) {
    struct install_profile profile;
    install_profile_reset(&profile);
    profile.kind = INSTALL_PROFILE_FULL;
    snprintf(profile.repo_name, sizeof(profile.repo_name), "modules");
    snprintf(profile.repo_url, sizeof(profile.repo_url),
             "https://example.org/modules-index.txt");
    profile.repo_signed = 0u;
    profile.valid = 1u;

    char buf[512];
    size_t len = 0;
    int rc = install_profile_format(&profile, buf, sizeof(buf), &len);
    EXPECT(rc == INSTALL_PROFILE_OK, "full format ok");
    EXPECT(strstr(buf, "profile=full\n") != NULL, "kind full present");
    EXPECT(strstr(buf, "bootstrap_repo_name=modules\n") != NULL, "name line");
    EXPECT(strstr(buf, "bootstrap_repo_url=https://example.org/modules-index.txt\n") != NULL, "url line");
    EXPECT(strstr(buf, "bootstrap_repo_signed=0\n") != NULL, "signed=0 line");

    struct install_profile reparsed;
    rc = install_profile_parse(buf, len, &reparsed);
    EXPECT(rc == INSTALL_PROFILE_OK, "full round-trip parses");
    EXPECT(reparsed.kind == INSTALL_PROFILE_FULL, "full kind preserved");
    EXPECT(strcmp(reparsed.repo_name, "modules") == 0, "repo_name preserved");
}

static void test_format_custom_with_install_list(void) {
    struct install_profile profile;
    install_profile_reset(&profile);
    profile.kind = INSTALL_PROFILE_CUSTOM;
    snprintf(profile.repo_name, sizeof(profile.repo_name), "modules");
    snprintf(profile.repo_url, sizeof(profile.repo_url),
             "https://example.org/index.txt");
    snprintf(profile.install_list, sizeof(profile.install_list),
             "org.capyos.ui.desktop-session,org.capyos.browser.core");
    profile.repo_signed = 1u;
    profile.valid = 1u;

    char buf[512];
    size_t len = 0;
    int rc = install_profile_format(&profile, buf, sizeof(buf), &len);
    EXPECT(rc == INSTALL_PROFILE_OK, "custom format ok");
    EXPECT(strstr(buf, "bootstrap_repo_signed=1\n") != NULL, "signed=1");
    EXPECT(strstr(buf, "bootstrap_install=org.capyos.ui.desktop-session,org.capyos.browser.core\n") != NULL,
           "install list preserved");

    struct install_profile reparsed;
    rc = install_profile_parse(buf, len, &reparsed);
    EXPECT(rc == INSTALL_PROFILE_OK, "custom round-trip parses");
    EXPECT(reparsed.kind == INSTALL_PROFILE_CUSTOM, "custom kind preserved");
    EXPECT(reparsed.repo_signed == 1u, "signed preserved");
}

static void test_format_rejects_too_small_buffer(void) {
    struct install_profile profile;
    install_profile_reset(&profile);
    profile.kind = INSTALL_PROFILE_FULL;
    snprintf(profile.repo_name, sizeof(profile.repo_name), "modules");
    snprintf(profile.repo_url, sizeof(profile.repo_url),
             "https://example.org/modules-index.txt");
    profile.valid = 1u;

    char tiny[8];
    int rc = install_profile_format(&profile, tiny, sizeof(tiny), NULL);
    EXPECT(rc == INSTALL_PROFILE_ERR_INVALID_ARG,
           "format rejects too-small buffer");
}

static void test_labels(void) {
    EXPECT(strcmp(install_profile_kind_label(INSTALL_PROFILE_BASIC),
                  "basic") == 0, "label basic");
    EXPECT(strcmp(install_profile_kind_label(INSTALL_PROFILE_FULL),
                  "full") == 0, "label full");
    EXPECT(strcmp(install_profile_kind_label(INSTALL_PROFILE_CUSTOM),
                  "custom") == 0, "label custom");
    EXPECT(strcmp(install_profile_result_label(INSTALL_PROFILE_OK),
                  "ok") == 0, "label ok");
    EXPECT(strcmp(install_profile_result_label(
                      INSTALL_PROFILE_ERR_MISSING_FIELD),
                  "missing field") == 0, "label missing");
}

static void test_crlf_line_endings_parse_clean(void) {
    struct install_profile profile;
    const char text[] =
        "profile=full\r\n"
        "bootstrap_repo_name=modules\r\n"
        "bootstrap_repo_url=https://example.org/modules-index.txt\r\n";
    int rc = install_profile_parse(text, sizeof(text) - 1u, &profile);
    EXPECT(rc == INSTALL_PROFILE_OK, "CRLF profile must parse");
    EXPECT(profile.kind == INSTALL_PROFILE_FULL, "CRLF kind full");
    EXPECT(strcmp(profile.repo_name, "modules") == 0,
           "CRLF must not leave a trailing CR in repo_name");
    EXPECT(strcmp(profile.repo_url,
                  "https://example.org/modules-index.txt") == 0,
           "CRLF must not leave a trailing CR in repo_url");
    EXPECT(install_profile_should_bootstrap(&profile) == 1,
           "CRLF full profile must bootstrap");
}

static void test_install_list_skips_empty_and_trims(void) {
    /* Direct iterator contract: empty tokens (from doubled or trailing
     * commas) are skipped and surrounding whitespace is trimmed, so the
     * bootstrap never receives an empty or padded package name. */
    const char *list = "a,,b , c,";
    size_t cursor = 0u;
    char token[INSTALL_PROFILE_NAME_MAX];
    EXPECT(install_profile_install_list_next(list, &cursor, token,
                                             sizeof(token)) == 0 &&
               strcmp(token, "a") == 0, "first token a");
    EXPECT(install_profile_install_list_next(list, &cursor, token,
                                             sizeof(token)) == 0 &&
               strcmp(token, "b") == 0, "doubled comma skipped, b returned");
    EXPECT(install_profile_install_list_next(list, &cursor, token,
                                             sizeof(token)) == 0 &&
               strcmp(token, "c") == 0, "whitespace-padded c trimmed");
    EXPECT(install_profile_install_list_next(list, &cursor, token,
                                             sizeof(token)) == -1,
           "trailing comma yields no extra token");
}

static void test_signed_invalid_value_rejected(void) {
    struct install_profile profile;
    int rc = PARSE(
        "profile=full\n"
        "bootstrap_repo_name=modules\n"
        "bootstrap_repo_url=https://example.org/m.txt\n"
        "bootstrap_repo_signed=2\n",
        &profile);
    EXPECT(rc == INSTALL_PROFILE_ERR_PARSE,
           "bootstrap_repo_signed must be exactly 0 or 1");
    EXPECT(profile.kind == INSTALL_PROFILE_BASIC,
           "reset to BASIC on rejected bool");
}

static void test_url_empty_value_rejected(void) {
    struct install_profile profile;
    int rc = PARSE(
        "profile=full\n"
        "bootstrap_repo_url=\n",
        &profile);
    EXPECT(rc == INSTALL_PROFILE_ERR_PARSE,
           "empty bootstrap_repo_url must be rejected");
}

static void test_full_with_name_but_no_url_fails(void) {
    struct install_profile profile;
    int rc = PARSE(
        "profile=full\n"
        "bootstrap_repo_name=modules\n",
        &profile);
    EXPECT(rc == INSTALL_PROFILE_ERR_MISSING_FIELD,
           "full with repo_name but no repo_url must fail");
}

static void test_should_bootstrap_rejects_non_https_defensively(void) {
    /* should_bootstrap re-checks the https prefix independently of the
     * parser, so a hand-built or future-loaded profile cannot smuggle a
     * non-https repo into the bootstrap path. */
    struct install_profile profile;
    install_profile_reset(&profile);
    profile.kind = INSTALL_PROFILE_FULL;
    profile.valid = 1u;
    snprintf(profile.repo_name, sizeof(profile.repo_name), "modules");
    snprintf(profile.repo_url, sizeof(profile.repo_url),
             "http://example.org/modules-index.txt");
    EXPECT(install_profile_should_bootstrap(&profile) == 0,
           "should_bootstrap must reject a non-HTTPS repo url defensively");
    snprintf(profile.repo_url, sizeof(profile.repo_url),
             "https://example.org/modules-index.txt");
    EXPECT(install_profile_should_bootstrap(&profile) == 1,
           "should_bootstrap accepts a well-formed https full profile");
}

int run_install_profile_tests(void) {
    test_empty_defaults_to_basic();
    test_explicit_basic();
    test_full_profile_valid();
    test_full_without_repo_fails();
    test_full_url_not_https();
    test_custom_with_install_list();
    test_custom_without_install_list_fails();
    test_install_list_unsafe_token_rejected();
    test_repo_name_alphabet_enforced();
    test_unknown_kind_rejected();
    test_unknown_key_tolerated();
    test_malformed_line_rejected();
    test_control_byte_rejected();
    test_signed_repo_flag();
    /* alpha.241: serializer round-trip tests. */
    test_format_basic_round_trip();
    test_format_full_round_trip();
    test_format_custom_with_install_list();
    test_format_rejects_too_small_buffer();
    test_labels();
    /* alpha.263 follow-up: parser robustness + defensive gates. */
    test_crlf_line_endings_parse_clean();
    test_install_list_skips_empty_and_trims();
    test_signed_invalid_value_rejected();
    test_url_empty_value_rejected();
    test_full_with_name_but_no_url_fails();
    test_should_bootstrap_rejects_non_https_defensively();
    fprintf(stderr,
            "install_profile: %d/%d tests passed (%d failures)\n",
            g_total - g_failures, g_total, g_failures);
    return g_failures == 0 ? 0 : 1;
}
