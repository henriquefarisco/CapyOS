#include "update_agent_fixture.h"

int run_update_agent_migration_tests(void) {
    int fails = 0;
    struct system_update_status status;
    reset_files();
    update_agent_reset();
    update_agent_set_reader(stub_read_file);
    update_agent_set_bytes_reader(stub_read_bytes);
    update_agent_set_writer(stub_write_file);
    update_agent_set_bytes_writer(stub_write_bytes);
    update_agent_set_remover(stub_remove_file);
    update_agent_set_manifest_verifier(stub_manifest_verify);
    update_agent_set_manifest_fetcher(stub_fetch_manifest);
    update_agent_set_payload_fetcher(stub_fetch_payload);
    update_agent_init("0.10.0+20260904");
    set_file_text(
        UPDATE_AGENT_REPOSITORY_PATH,
        "channel=stable\nbranch=main\nsource=github:henriquefarisco/CapyOS\n"
        "remote_manifest=https://github.com/henriquefarisco/CapyOS/releases/latest/download/bridge.ini\n");
    fails += expect_true(update_agent_poll() == 0,
                         "official bridge route should migrate to full stable catalog");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.remote_manifest_url,
                                "https://github.com/henriquefarisco/CapyOS/releases/latest/download/latest.ini") == 0,
                         "bridge route must resolve to full latest release");
    fails += expect_true(strstr(find_file(UPDATE_AGENT_REPOSITORY_PATH)->text,
                                "remote_manifest=https://github.com/henriquefarisco/CapyOS/releases/latest/download/latest.ini") != NULL,
                         "bridge route retirement must persist across reboot");
    {
        static const char *const preserved[] = {
            "channel=develop\nbranch=develop\nsource=github:henriquefarisco/CapyOS\n"
            "remote_manifest=https://github.com/henriquefarisco/CapyOS/releases/latest/download/bridge.ini\n",
            "channel=stable\nbranch=main\nsource=github:other/CapyOS\n"
            "remote_manifest=https://github.com/henriquefarisco/CapyOS/releases/latest/download/bridge.ini\n",
            "channel=stable\nbranch=main\nsource=github:henriquefarisco/CapyOS\n"
            "remote_manifest=https://github.com/henriquefarisco/CapyOS/releases/latest/download/bridge.ini.evil\n",
            "channel=stable\nbranch=main\nsource=github:henriquefarisco/CapyOS\n"
            "remote_manifest=https://github.com/other/CapyOS/releases/latest/download/bridge.ini\n"
        };
        for (size_t i = 0u; i < sizeof(preserved) / sizeof(preserved[0]); ++i) {
            set_file_text(UPDATE_AGENT_REPOSITORY_PATH, preserved[i]);
            fails += expect_true(update_agent_poll() == 0,
                                 "non-official bridge route should remain usable");
            fails += expect_true(strcmp(find_file(UPDATE_AGENT_REPOSITORY_PATH)->text,
                                        preserved[i]) == 0,
                                 "bridge migration must not rewrite custom repositories");
        }
    }
    reset_files();
    update_agent_init("0.10.0+20260904");
    set_file_text(UPDATE_AGENT_REPOSITORY_PATH,
                  "channel=stable\nbranch=main\nsource=github:test/CapyOS\n");
    const size_t large_size = 7400072u;
    uint8_t *large = malloc(large_size);
    fails += expect_true(large != NULL, "large payload allocation failed");
    if (large) {
        for (size_t i = 0; i < large_size; ++i) large[i] = (uint8_t)(i * 37u);
        uint8_t digest[32];
        char hash[65], manifest[768];
        sha256_hash(large, large_size, digest);
        sha256_hex(digest, hash);
        snprintf(manifest, sizeof(manifest),
                 "available_version=0.11.2+20261004\nchannel=stable\nbranch=main\n"
                 "source=github:test/CapyOS\npublished_at=2026-10-04\n"
                 "payload_size=%zu\npayload_sha256=%s\n"
                 UPDATE_AGENT_PAYLOAD_URL_LINE UPDATE_AGENT_SIGNATURE_LINE,
                 large_size, hash);
        set_file_text(UPDATE_AGENT_CACHE_PATH, manifest);
        g_payload_bytes = large;
        g_payload_len = large_size;
        g_payload_rc = 0;
        fails += expect_true(update_agent_download_payload() == 0,
                             "7.4 MB payload must persist through legacy-size parts");
        fails += expect_true(find_file(UPDATE_AGENT_PAYLOAD_CACHE_PATH)->len ==
                             UPDATE_CACHE_DESCRIPTOR_BYTES, "cache descriptor missing");
        fails += expect_true(update_agent_prepare_dry_run() == 0,
                             "reassembled signed payload should verify");
        uint8_t *part = g_large_files[find_file(UPDATE_AGENT_PAYLOAD_CACHE_PATH ".part1") - g_files];
        fails += expect_true(part != NULL, "segmented part1 missing");
        if (part) part[123] ^= 1;
        fails += expect_true(update_agent_prepare_dry_run() != 0,
                             "corrupt part must fail full signed payload verification");
        fails += expect_true(update_agent_clear_stage() == 0,
                             "segmented cache cleanup failed");
        for (size_t i = 0; i < sizeof(g_files) / sizeof(g_files[0]); ++i)
            if (strstr(g_files[i].path, "payload.bin"))
                fails += expect_true(!g_files[i].present, "orphaned cache part");
        free(large);
    }
    reset_files();
    update_agent_reset();
    if (!fails) puts("[tests] update_agent_migration OK");
    return fails;
}
