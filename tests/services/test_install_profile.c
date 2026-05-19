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
    test_labels();
    fprintf(stderr,
            "install_profile: %d/%d tests passed (%d failures)\n",
            g_total - g_failures, g_total, g_failures);
    return g_failures == 0 ? 0 : 1;
}
