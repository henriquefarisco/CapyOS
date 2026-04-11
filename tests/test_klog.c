#include <stdio.h>
#include <string.h>

#include "core/klog.h"

static char g_flush_capture[65536];
static int g_flush_calls = 0;
static int g_path_ok = 1;

static void reset_capture(void) {
    memset(g_flush_capture, 0, sizeof(g_flush_capture));
    g_flush_calls = 0;
    g_path_ok = 1;
}

static int capture_append(const char *path, const char *text) {
    size_t used = strlen(g_flush_capture);
    size_t len = text ? strlen(text) : 0;
    if (!path || strcmp(path, "/var/log/capyos_klog.txt") != 0) {
        g_path_ok = 0;
        return -1;
    }
    if (used + len + 1 >= sizeof(g_flush_capture)) {
        return -1;
    }
    if (len > 0) {
        memcpy(&g_flush_capture[used], text, len);
        g_flush_capture[used + len] = '\0';
    }
    g_flush_calls++;
    return 0;
}

static int expect_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[klog] %s\n", message);
        return 1;
    }
    return 0;
}

int run_klog_tests(void) {
    int failures = 0;
    char msg[32];

    klog_reset();
    reset_capture();

    klog(KLOG_INFO, "boot started");
    failures += expect_true(klog_flush(capture_append) == 0,
                            "first flush should succeed");
    failures += expect_true(g_flush_calls == 1, "first flush should write once");
    failures += expect_true(g_path_ok, "flush path should match default klog path");
    failures += expect_true(strstr(g_flush_capture, "boot started") != NULL,
                            "flush should contain first message");

    failures += expect_true(klog_flush(capture_append) == 0,
                            "second flush without new data should be a no-op");
    failures += expect_true(g_flush_calls == 1,
                            "second flush without new data must not append");

    klog(KLOG_WARN, "network pending");
    failures += expect_true(klog_flush(capture_append) == 0,
                            "flush with delta should succeed");
    failures += expect_true(g_flush_calls == 2,
                            "delta flush should append only once");
    failures += expect_true(strstr(g_flush_capture, "network pending") != NULL,
                            "delta flush should contain new message");

    klog_reset();
    reset_capture();
    for (int i = 0; i < (int)KLOG_RING_LINES + 5; ++i) {
        snprintf(msg, sizeof(msg), "entry-%d", i);
        klog(KLOG_INFO, msg);
    }
    failures += expect_true(klog_flush(capture_append) == 0,
                            "flush after ring overwrite should still succeed");
    failures += expect_true(strstr(g_flush_capture, "klog lost 5 entries before flush") != NULL,
                            "flush should report overwritten entries");
    failures += expect_true(strstr(g_flush_capture, "entry-260") != NULL,
                            "flush should contain newest entry after overwrite");

    if (failures == 0) {
        printf("[ok] klog\n");
    }
    return failures;
}
