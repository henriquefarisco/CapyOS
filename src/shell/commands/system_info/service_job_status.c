#include "internal/system_info_internal.h"

static int service_matches_filter(const char *filter, const char *name) {
    return !filter || !filter[0] || shell_string_equal(filter, name);
}

static int work_matches_filter(const char *filter, const char *name) {
    return !filter || !filter[0] || shell_string_equal(filter, name);
}

void shell_print_signed_result(int32_t value) {
    if (value < 0) {
        shell_print("-");
        shell_print_number((uint32_t)(-value));
        return;
    }
    shell_print_number((uint32_t)value);
}

static void shell_print_service_dependencies(uint32_t dependency_mask) {
    int first = 1;
    size_t count = service_manager_count();

    if (dependency_mask == 0u) {
        shell_print("-");
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        struct system_service_status svc;
        if (service_manager_get_at(i, &svc) != 0) {
            continue;
        }
        if ((dependency_mask & (1u << svc.id)) == 0u) {
            continue;
        }
        if (!first) {
            shell_print(",");
        }
        shell_print(svc.name);
        first = 0;
    }
}

void shell_print_bool_flag(const char *label, int value) {
    shell_print(label);
    shell_print(value ? "yes" : "no");
}

void shell_print_path_status(const char *path) {
    struct vfs_stat st;
    int rc = vfs_stat_path(path, &st);
    shell_print(path);
    shell_print(" exists=");
    shell_print(rc == 0 ? "yes" : "no");
    if (rc == 0) {
        shell_print(" type=");
        shell_print((st.mode & VFS_MODE_DIR) ? "dir" : "file");
        shell_print(" size=");
        shell_print_number(st.size);
    }
    shell_newline();
}

int system_info_recovery_capyfs_check(struct capyfs_check_report *out) {
    struct super_block *root = vfs_root();
    if (!root || !root->bdev || !out) {
        return -1;
    }
    return capyfs_check(root->bdev, out);
}

int cmd_service_status(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    const char *filter = NULL;
    size_t count = 0;

    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: service-status [nome]\nMostra o estado dos servicos internos do sistema.\n",
            "Usage: service-status [name]\nShows the state of the internal system services.\n",
            "Uso: service-status [nombre]\nMuestra el estado de los servicios internos del sistema.\n"));
        return 0;
    }

    if (argc >= 2) {
        filter = argv[1];
    }

    if (!filter || !filter[0]) {
        struct system_service_target_status target;
        if (service_manager_target_current(&target) == 0) {
            shell_print("target=");
            shell_print(target.name);
            if (ctx && ctx->settings && ctx->settings->service_target[0]) {
                shell_print(" saved=");
                shell_print(ctx->settings->service_target);
            }
            shell_print(" mask=");
            shell_print_number(target.service_mask);
            shell_newline();
        }
    }

    count = service_manager_count();
    for (size_t i = 0; i < count; ++i) {
        struct system_service_status svc;
        if (service_manager_get_at(i, &svc) != 0) {
            continue;
        }
        if (!service_matches_filter(filter, svc.name)) {
            continue;
        }
        shell_print(svc.name);
        shell_print(" state=");
        shell_print(service_manager_state_label(svc.state));
        shell_print(" startup=");
        shell_print(service_manager_startup_label(svc.startup));
        shell_print(" critical=");
        shell_print(svc.critical ? "yes" : "no");
        shell_print(" rc=");
        shell_print_signed_result(svc.last_result);
        shell_print(" transitions=");
        shell_print_number(svc.transitions);
        shell_print(" polls=");
        shell_print_number(svc.polls);
        shell_print(" every=");
        if (svc.poll_interval_ticks == 0u) {
            shell_print("loop");
        } else {
            shell_print_number(svc.poll_interval_ticks);
        }
        shell_print(" failures=");
        shell_print_number(svc.failures);
        shell_print(" restarts=");
        shell_print_number(svc.restarts);
        shell_print(" backoff=");
        shell_print_number(svc.backoff_ticks);
        shell_print(" retry=");
        shell_print_number(svc.restart_limit);
        shell_newline();
        shell_print("  deps=");
        shell_print_service_dependencies(svc.dependency_mask);
        shell_print(" summary=");
        shell_print(svc.summary[0] ? svc.summary : "(no summary)");
        shell_newline();
    }
    return 0;
}

int cmd_job_status(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    const char *filter = NULL;
    size_t count = 0;
    (void)ctx;

    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: job-status [nome]\nMostra o estado dos jobs internos do kernel/work queue.\n",
            "Usage: job-status [name]\nShows the state of the internal kernel/work queue jobs.\n",
            "Uso: job-status [nombre]\nMuestra el estado de los jobs internos del kernel/work queue.\n"));
        return 0;
    }

    if (argc >= 2) {
        filter = argv[1];
    }

    count = work_queue_count();
    for (size_t i = 0; i < count; ++i) {
        struct system_work_status work;
        if (work_queue_get_at(i, &work) != 0) {
            continue;
        }
        if (!work_matches_filter(filter, work.name)) {
            continue;
        }
        shell_print(work.name);
        shell_print(" state=");
        shell_print(work_queue_state_label(work.state));
        shell_print(" rc=");
        shell_print_signed_result(work.last_result);
        shell_print(" runs=");
        shell_print_number(work.runs);
        shell_print(" failures=");
        shell_print_number(work.failures);
        shell_print(" every=");
        if (work.interval_ticks == 0u) {
            shell_print("manual");
        } else {
            shell_print_number(work.interval_ticks);
        }
        shell_print(" next=");
        if (work.state == SYSTEM_WORK_STATE_DISABLED) {
            shell_print("-");
        } else {
            shell_print_number((uint32_t)work.next_due_tick);
        }
        shell_newline();
        shell_print("  summary=");
        shell_print(work.summary[0] ? work.summary : "(no summary)");
        shell_newline();
    }
    return 0;
}

