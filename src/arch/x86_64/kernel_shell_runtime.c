#pragma GCC optimize("O0")
#include "arch/x86_64/kernel_shell_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_shell_dispatch.h"
#include "arch/x86_64/storage_runtime.h"
#include "core/system_init.h"
#include "auth/user.h"
#include "auth/user_home.h"
#include "auth/user_prefs.h"
#include "drivers/input/keyboard.h"
#include "fs/buffer.h"
#include "fs/capyfs.h"
#include "fs/ramdisk.h"
#include "fs/vfs.h"

static void io_print(const struct x64_kernel_shell_runtime_io *io,
                     const char *message) {
  if (io && io->print && message) {
    io->print(message);
  }
}

static void io_print_hex(const struct x64_kernel_shell_runtime_io *io,
                         uint64_t value) {
  if (io && io->print_hex) {
    io->print_hex(value);
  }
}

static void io_putc(const struct x64_kernel_shell_runtime_io *io, char ch) {
  if (io && io->putc) {
    io->putc(ch);
  }
}

static void local_copy(char *dst, size_t dst_size, const char *src) {
  size_t i = 0;
  if (!dst || dst_size == 0) {
    return;
  }
  if (src) {
    while (src[i] && i + 1 < dst_size) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
}

static int session_should_autostart_desktop(const struct user_record *user) {
  if (!user || !user->username[0]) {
    return 0;
  }
  if (shell_string_equal(user->username, "maintenance")) {
    return 0;
  }
  if (shell_string_equal(user->role, "recovery")) {
    return 0;
  }
  return 1;
}

static int state_ready(const struct x64_kernel_shell_runtime_state *state) {
  return state && state->shell_ctx && state->session_ctx && state->settings &&
         state->shell_initialized && state->shell_fs_ready &&
         state->shell_persistent_storage &&
         state->shell_recovery_ram_fallback && state->data_io_probe &&
         state->data_io_probe_size > 0;
}

static int ops_ready(const struct x64_kernel_shell_runtime_ops *ops) {
  return ops && ops->mount_root_capyfs && ops->ensure_dir_recursive &&
         ops->write_text_file && ops->mount_encrypted_data_volume &&
         ops->persist_active_volume_key_hash &&
         ops->handoff_has_firmware_block_io && ops->after_native_runtime_ready;
}

static int bootstrap_ramdisk_runtime(
    struct x64_kernel_shell_runtime_state *state,
    const struct x64_kernel_shell_runtime_io *io,
    const struct x64_kernel_shell_runtime_ops *ops) {
  const char *dirs[] = {"/bin",     "/docs", "/etc",   "/home",
                               "/home/admin", "/system", "/tmp",   "/var",
                               "/var/log"};
  static const char cli_doc[] =
      "CapyCLI x64 (early)\n"
      "Comandos principais:\n"
      "  list, go, mypath, mk-file, mk-dir, kill-file, kill-dir,\n"
      "  move, clone, print-file, open, hunt-any, find,\n"
      "  help-any, help-docs, print-version, print-envs,\n"
      "  net-status, net-ip, net-gw, net-dns,\n"
      "  net-set <ip> <mask> <gw> <dns>, hey <ip>,\n"
      "  add-user <user> <pass> [role], set-pass <user> <pass>, list-users,\n"
      "  shutdown-reboot, shutdown-off, do-sync,\n"
      "  pkg-list, pkg-info, pkg-fetch, pkg-install, pkg-remove,\n"
      "  pkg-update, pkg-source-list, pkg-source-add, pkg-source-remove\n";

  ramdisk_init(512);
  struct block_device *ram = ramdisk_device();
  if (!ram) {
    io_print(io, "[fs] ERRO: ramdisk indisponivel.\n");
    return -1;
  }

  /* Reuse of the static ramdisk device across recovery retries can leave
   * stale cached blocks attached to the previous attempt. Start from a clean
   * cache view before formatting and force the mount path to re-read media
   * after the formatter flushes metadata. */
  buffer_cache_invalidate(ram);
  int fmt_rc = capyfs_format(ram, 128, ram->block_count, NULL);
  if (fmt_rc != 0) {
    io_print(io, "[fs] ERRO: falha ao formatar CAPYFS em RAM. rc=");
    io_print_hex(io, (uint64_t)(uint32_t)fmt_rc);
    io_putc(io, '\n');
    return -1;
  }
  buffer_cache_invalidate(ram);
  if (ops->mount_root_capyfs(ram, "RAM") != 0) {
    return -1;
  }

  for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
    if (ops->ensure_dir_recursive(dirs[i]) != 0) {
      io_print(io, "[fs] ERRO: falha ao criar estrutura base: ");
      io_print(io, dirs[i]);
      io_print(io, "\n");
      return -1;
    }
  }

  if (ops->write_text_file("/docs/capyos-cli-reference.txt", cli_doc) != 0) {
    io_print(
        io,
        "[fs] aviso: nao foi possivel gravar /docs/capyos-cli-reference.txt\n");
  }

  if (userdb_ensure() != 0) {
    io_print(io,
             "[fs] Aviso: /etc/users.db nao preparado; sera criado pelo "
             "assistente.\n");
  }

  /* Keep RAM fallback available, but require persistent storage before
   * the authoritative first-boot wizard can complete setup. */
  if (system_detect_first_boot() != 0) {
    io_print(io,
             "[setup] Primeira inicializacao pendente; storage persistente "
             "necessario para concluir configuracao.\n");
  }

  *state->shell_persistent_storage = 0;
  io_print(io, "[fs] CAPYFS em RAM pronto para CLI.\n");
  return 0;
}

static int shell_enter_recovery_ram_runtime(
    struct x64_kernel_shell_runtime_state *state,
    const struct x64_kernel_shell_runtime_io *io,
    const struct x64_kernel_shell_runtime_ops *ops, const char *reason) {
  if (!state || !state->allow_recovery_ram_fallback ||
      !state->shell_recovery_ram_fallback) {
    return -1;
  }

  io_print(io,
           "[recovery] Storage persistente indisponivel; iniciando runtime "
           "temporario em RAM.\n");
  if (reason && reason[0]) {
    io_print(io, "[recovery] Motivo: ");
    io_print(io, reason);
    io_putc(io, '\n');
  }
  *state->shell_recovery_ram_fallback = 1;
  *state->shell_persistent_storage = 0;
  return bootstrap_ramdisk_runtime(state, io, ops);
}

static int shell_bootstrap_filesystem(
    struct x64_kernel_shell_runtime_state *state,
    const struct x64_kernel_shell_runtime_io *io,
    const struct x64_kernel_shell_runtime_ops *ops) {
  struct block_device *data_dev = NULL;
  struct x64_storage_runtime_io storage_io;

  if (*state->shell_fs_ready) {
    return 0;
  }

  *state->shell_persistent_storage = 0;
  *state->shell_recovery_ram_fallback = 0;

  buffer_cache_init();
  vfs_init();

  storage_io.print = io ? io->print : NULL;
  storage_io.print_hex64 = io ? io->print_hex64 : NULL;
  storage_io.print_dec_u32 = io ? io->print_dec_u32 : NULL;
  storage_io.putc = io ? io->putc : NULL;

  data_dev = x64_storage_runtime_open_handoff_data_device(
      state->handoff, &storage_io, state->data_io_probe);
  if (data_dev && x64_storage_runtime_uses_firmware() &&
      x64_storage_runtime_hyperv_present()) {
    io_print(io,
             "[fs] Hyper-V DATA via EFI BlockIO; adiando ExitBootServices "
             "para preservar persistencia.\n");
  } else {
    ops->after_native_runtime_ready();
  }

  if (state->handoff &&
      (!data_dev || x64_storage_runtime_uses_firmware())) {
    int was_using_firmware = data_dev && x64_storage_runtime_uses_firmware();
    struct block_device *refreshed_dev =
        x64_storage_runtime_open_handoff_data_device(
            state->handoff, &storage_io, state->data_io_probe);
    if (refreshed_dev) {
      data_dev = refreshed_dev;
      if (!x64_storage_runtime_uses_firmware()) {
        io_print(io,
                 was_using_firmware
                     ? "[fs] Dispositivo DATA promovido via backend nativo apos preparar o runtime.\n"
                     : "[fs] Dispositivo DATA adquirido via backend nativo apos preparar o runtime.\n");
      }
    }
  }

  if (data_dev && x64_storage_runtime_uses_firmware() &&
      ops->boot_services_active && !ops->boot_services_active()) {
    io_print(io,
             "[fs] aviso: firmware indisponivel apos transicao nativa "
             "(BootServices offline).\n");
    data_dev = NULL;

    /* The native backend (e.g. Hyper-V StorVSC) may have been promoted
     * during after_native_runtime_ready().  Re-acquire the data device
     * so the persistent path can still be used. */
    data_dev = x64_storage_runtime_open_handoff_data_device(
        state->handoff, &storage_io, state->data_io_probe);
    if (data_dev) {
      io_print(io,
               "[fs] Dispositivo DATA re-adquirido via backend nativo.\n");
    }
  }

  if (data_dev && ops->mount_encrypted_data_volume(data_dev) == 0) {
    *state->shell_persistent_storage = 1;
    int is_first_boot = (system_detect_first_boot() != 0);
    if (is_first_boot) {
      io_print(io,
               "[setup] Primeira inicializacao detectada; preparando base "
               "persistente.\n");
    }
    {
      const char *base_dirs[] = {"/bin",     "/docs", "/etc",   "/home",
                                 "/system",  "/tmp",  "/var",   "/var/log"};
      for (size_t i = 0; i < sizeof(base_dirs) / sizeof(base_dirs[0]); ++i) {
        (void)ops->ensure_dir_recursive(base_dirs[i]);
      }
    }
    if (userdb_ensure() != 0) {
      io_print(io,
               "[fs] Aviso: /etc/users.db indisponivel; sera criado pelo "
               "assistente de configuracao.\n");
    }
    if (ops->persist_active_volume_key_hash() != 0) {
      io_print(
          io,
          "[fs] aviso: nao foi possivel persistir hash da chave do volume.\n");
    }
    /* alpha.241: silent provisioning retired.
     *
     * The installer no longer carries BOOT_CONFIG_FLAG_HAS_SETUP_DATA,
     * which means `system_installer_config_available()` will always
     * return 0 here. The first-boot wizard (TUI in
     * `src/config/first_boot/program.c::first_boot_setup_interactive`)
     * is now the single source of truth for admin user, language,
     * keyboard, theme, hostname, splash and module selection. The
     * previous silent path that ran before the wizard has been removed
     * to avoid two sources of truth diverging. */
    if (is_first_boot && system_installer_config_available()) {
      io_print(io,
               "[setup] Legacy installer handoff detected (HAS_SETUP_DATA);\n"
               "[setup] ignoring it and deferring to the in-kernel wizard.\n");
      system_installer_clear_password();
    }
  } else {
    /* Persistent volume unavailable — try recovery first, then always
     * fall back to a ramdisk so the VFS is available for the wizard
     * and login.  Never return -1 here: that would kill the entire
     * boot chain and prevent the first-boot wizard from running. */
    int ram_ok = 0;
    if (data_dev) {
      io_print(io,
               "[fs] Aviso: falha ao montar volume persistente CAPYFS.\n");
      io_print(io,
               "[fs] Caindo para RAM para manter o sistema acessivel.\n");
    } else if (state->handoff && state->handoff->version >= 2 &&
               ops->handoff_has_firmware_block_io()) {
      io_print(io,
               "[fs] Aviso: volume DATA nao encontrado no handoff UEFI.\n");
    }
    if (state->allow_recovery_ram_fallback) {
      ram_ok = (shell_enter_recovery_ram_runtime(
                    state, io, ops,
                    data_dev
                        ? "falha ao montar/desbloquear volume persistente"
                        : "nenhum volume persistente fornecido ao boot") ==
                0)
                   ? 1
                   : 0;
    }
    if (!ram_ok) {
      io_print(io, "[fs] Inicializando CAPYFS em RAM (ultimo recurso).\n");
      if (bootstrap_ramdisk_runtime(state, io, ops) != 0) {
        io_print(io,
                 "[fs] ERRO CRITICO: ramdisk tambem falhou. Sistema sem "
                 "filesystem.\n");
        return -1;
      }
    }
  }

  *state->shell_fs_ready = 1;
  if (*state->shell_persistent_storage) {
    io_print(io, "[fs] CAPYFS persistente ativo (dados cifrados).\n");
  }
  return 0;
}

static void apply_default_settings(struct system_settings *settings) {
  local_copy(settings->hostname, sizeof(settings->hostname), "capyos64");
  local_copy(settings->theme, sizeof(settings->theme), "capyos");
  local_copy(settings->keyboard_layout, sizeof(settings->keyboard_layout), "us");
  local_copy(settings->language, sizeof(settings->language), "en");
  local_copy(settings->update_channel, sizeof(settings->update_channel),
             "stable");
  local_copy(settings->network_mode, sizeof(settings->network_mode), "dhcp");
  local_copy(settings->service_target, sizeof(settings->service_target),
             "network");
  settings->ipv4_addr = ((uint32_t)10u << 24) | ((uint32_t)0u << 16) |
                        ((uint32_t)2u << 8) | 15u;
  settings->ipv4_mask = ((uint32_t)255u << 24) | ((uint32_t)255u << 16) |
                        ((uint32_t)255u << 8) | 0u;
  settings->ipv4_gateway = ((uint32_t)10u << 24) | ((uint32_t)0u << 16) |
                           ((uint32_t)2u << 8) | 2u;
  settings->ipv4_dns = ((uint32_t)1u << 24) | ((uint32_t)1u << 16) |
                       ((uint32_t)1u << 8) | 1u;
  settings->splash_enabled = 0;
  settings->diagnostics_enabled = 0;
  /* Etapa F4 homepage (2026-05-03): default browser homepage para
   * boots em ambientes onde /system/config.ini nao existe ainda
   * (first-boot raw, instalador). Mantem alinhamento com o default
   * em src/config/system_settings.c (system_settings_set_defaults). */
  local_copy(settings->browser_homepage, sizeof(settings->browser_homepage),
             "https://wikipedia.org");
}

int x64_kernel_prepare_shell_runtime(
    struct x64_kernel_shell_runtime_state *state,
    const struct x64_kernel_shell_runtime_io *io,
    const struct x64_kernel_shell_runtime_ops *ops) {
  if (!state_ready(state) || !ops_ready(ops)) {
    return -1;
  }

  if (!*state->shell_initialized) {
    if (shell_bootstrap_filesystem(state, io, ops) != 0) {
      return -1;
    }
    if (system_load_settings(state->settings) != 0) {
      apply_default_settings(state->settings);
    }
    system_apply_theme(state->settings);
    system_apply_keyboard_layout(state->settings);
    session_reset(state->session_ctx);
    session_set_active(NULL);
    shell_context_init(state->shell_ctx, state->session_ctx, state->settings);
    *state->shell_initialized = 1;
  }

  return 0;
}

int x64_kernel_begin_shell_session(
    struct x64_kernel_shell_runtime_state *state,
    const struct user_record *user) {
  struct user_record session_user_copy;

  if (!state_ready(state) || !user || !user->username[0]) {
    return -1;
  }

  session_user_copy = *user;
  if (session_begin(state->session_ctx, &session_user_copy,
                    state->settings->language) != 0) {
    session_reset(state->session_ctx);
    session_set_cwd(state->session_ctx, "/");
    return -1;
  }
  session_set_active(state->session_ctx);
  shell_context_init(state->shell_ctx, state->session_ctx, state->settings);
  if (session_should_autostart_desktop(user)) {
    state->desktop_autostart_pending = 1;
  }
  return 0;
}
