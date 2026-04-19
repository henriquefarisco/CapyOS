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
      "  shutdown-reboot, shutdown-off, do-sync\n";

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

  /* Keep RAM fallback available, but let the authoritative first-boot
   * wizard run later via system_login() instead of completing setup here. */
  if (system_detect_first_boot() != 0) {
    io_print(io,
             "[setup] Primeira inicializacao detectada (RAM); o assistente "
             "sera executado no login.\n");
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
  ops->after_native_runtime_ready();

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
    /* Silent provisioning: when installer config is available and this is
       a genuine first boot, provision admin user + config + marker here
       (where ops->write_text_file works reliably) so the system boots
       directly to login with no wizard. */
    if (is_first_boot && system_installer_config_available()) {
      int provision_ok = 1;
      io_print(io,
               "[setup] Provisionamento automatico via configuracao do "
               "instalador.\n");
      const char *hostname = system_installer_hostname();
      const char *theme = system_installer_theme();
      const char *admin_user = system_installer_admin_username();
      const char *admin_pass = system_installer_admin_password();
      const char *lang = (state->handoff && state->handoff->boot_language[0])
                             ? state->handoff->boot_language
                             : "en";
      int splash = system_installer_splash_enabled();
      if (!hostname || !hostname[0]) hostname = "capyos-node";
      if (!theme || !theme[0]) theme = "capyos";
      if (!admin_user || !admin_user[0]) admin_user = "admin";
      if (splash < 0) splash = 1;
      /* Create admin home directory */
      char admin_home[64];
      admin_home[0] = '\0';
      local_copy(admin_home, sizeof(admin_home), "/home/");
      {
        size_t base = 0;
        while (admin_home[base]) ++base;
        size_t ui = 0;
        while (admin_user[ui] && base + 1 < sizeof(admin_home)) {
          admin_home[base++] = admin_user[ui++];
        }
        admin_home[base] = '\0';
      }
      if (user_home_prepare(admin_home, 1000, 1000) != 0) {
        io_print(io,
                 "[setup] Aviso: falha ao preparar diretorio pessoal do "
                 "administrador.\n");
        provision_ok = 0;
      }
      /* Create admin user record */
      if (provision_ok && admin_pass && admin_pass[0]) {
        struct user_record admin;
        struct user_record verify_admin;
        int admin_record_ready = 0;
        if (userdb_find(admin_user, &admin) == 0) {
          if (admin.home[0]) {
            local_copy(admin_home, sizeof(admin_home), admin.home);
          }
          if (user_home_prepare(admin_home, admin.uid, admin.gid) != 0) {
            io_print(io,
                     "[setup] Aviso: falha ao reparar diretorio pessoal do "
                     "administrador existente.\n");
            provision_ok = 0;
          } else {
            io_print(io,
                     "[setup] Usuario administrador ja existente; "
                     "reutilizando registro atual.\n");
            admin_record_ready = 1;
          }
        } else if (user_record_init(admin_user, admin_pass, "admin",
                                    1000, 1000, admin_home, &admin) == 0) {
          if (userdb_add(&admin) == 0) {
            io_print(io,
                     "[setup] Usuario administrador criado com sucesso.\n");
            admin_record_ready = 1;
          } else {
            io_print(io,
                     "[setup] Aviso: falha ao salvar usuario administrador.\n");
            provision_ok = 0;
          }
        } else {
          io_print(io,
                   "[setup] Aviso: falha ao construir registro do admin.\n");
          provision_ok = 0;
        }
        if (provision_ok && admin_record_ready) {
          struct super_block *root_sb = vfs_root();
          if (root_sb && root_sb->bdev) {
            buffer_cache_sync(root_sb->bdev);
          }
          if (userdb_authenticate(admin_user, admin_pass, &verify_admin) != 0) {
            if (userdb_set_password(admin_user, admin_pass) != 0) {
              io_print(io,
                       "[setup] Aviso: falha ao reparar credenciais do "
                       "administrador provisionado.\n");
              provision_ok = 0;
            } else if (root_sb && root_sb->bdev) {
              buffer_cache_sync(root_sb->bdev);
            }
          }
          if (provision_ok &&
              userdb_authenticate(admin_user, admin_pass, &verify_admin) != 0) {
            io_print(io,
                     "[setup] Aviso: falha ao validar credenciais do "
                     "administrador provisionado.\n");
            provision_ok = 0;
          } else if (user_prefs_save_language(&verify_admin, lang) != 0) {
            io_print(io,
                     "[setup] Aviso: falha ao gravar preferencias do "
                     "administrador.\n");
          }
        }
        system_installer_clear_password();
      } else if (!admin_pass || !admin_pass[0]) {
        io_print(io,
                 "[setup] Aviso: senha do administrador ausente no "
                 "provisionamento automatico.\n");
        provision_ok = 0;
      }
      /* Build and write /system/config.ini */
      {
        const char *kbd = keyboard_current_layout();
        if (!kbd || !kbd[0]) kbd = "us";
        const char *splash_str = splash ? "enabled" : "disabled";
        char cfg[512];
        cfg[0] = '\0';
        local_copy(cfg, sizeof(cfg), "hostname=");
        { size_t p = 0; while (cfg[p]) ++p;
          const char *v = hostname; size_t vi = 0;
          while (v[vi] && p + 1 < sizeof(cfg)) cfg[p++] = v[vi++];
          cfg[p] = '\0'; }
        { size_t p = 0; while (cfg[p]) ++p;
          const char *s = "\ntheme="; size_t si = 0;
          while (s[si] && p + 1 < sizeof(cfg)) cfg[p++] = s[si++];
          cfg[p] = '\0'; }
        { size_t p = 0; while (cfg[p]) ++p;
          const char *v = theme; size_t vi = 0;
          while (v[vi] && p + 1 < sizeof(cfg)) cfg[p++] = v[vi++];
          cfg[p] = '\0'; }
        { size_t p = 0; while (cfg[p]) ++p;
          const char *s = "\nkeyboard="; size_t si = 0;
          while (s[si] && p + 1 < sizeof(cfg)) cfg[p++] = s[si++];
          cfg[p] = '\0'; }
        { size_t p = 0; while (cfg[p]) ++p;
          const char *v = kbd; size_t vi = 0;
          while (v[vi] && p + 1 < sizeof(cfg)) cfg[p++] = v[vi++];
          cfg[p] = '\0'; }
        { size_t p = 0; while (cfg[p]) ++p;
          const char *s = "\nlanguage="; size_t si = 0;
          while (s[si] && p + 1 < sizeof(cfg)) cfg[p++] = s[si++];
          cfg[p] = '\0'; }
        { size_t p = 0; while (cfg[p]) ++p;
          const char *v = lang; size_t vi = 0;
          while (v[vi] && p + 1 < sizeof(cfg)) cfg[p++] = v[vi++];
          cfg[p] = '\0'; }
        { size_t p = 0; while (cfg[p]) ++p;
          const char *s = "\nupdate_channel=stable\nnetwork_mode=static\n"
                          "service_target=network\nsplash="; size_t si = 0;
          while (s[si] && p + 1 < sizeof(cfg)) cfg[p++] = s[si++];
          cfg[p] = '\0'; }
        { size_t p = 0; while (cfg[p]) ++p;
          const char *v = splash_str; size_t vi = 0;
          while (v[vi] && p + 1 < sizeof(cfg)) cfg[p++] = v[vi++];
          cfg[p] = '\0'; }
        { size_t p = 0; while (cfg[p]) ++p;
          const char *s = "\nipv4=0.0.0.0\nmask=0.0.0.0\n"
                          "gateway=0.0.0.0\ndns=0.0.0.0\n"; size_t si = 0;
          while (s[si] && p + 1 < sizeof(cfg)) cfg[p++] = s[si++];
          cfg[p] = '\0'; }
        if (ops->write_text_file("/system/config.ini", cfg) == 0) {
          io_print(io, "[setup] /system/config.ini gravado.\n");
        } else {
          io_print(io,
                   "[setup] Aviso: falha ao gravar /system/config.ini.\n");
          provision_ok = 0;
        }
      }
      /* Mark first boot complete */
      if (provision_ok &&
          ops->write_text_file("/system/first-run.done", "completed\n") == 0) {
        io_print(io, "[setup] Provisionamento automatico concluido.\n");
      } else if (provision_ok) {
        io_print(io,
                 "[setup] Aviso: falha ao marcar first-run.done.\n");
      } else {
        io_print(io,
                 "[setup] Provisionamento automatico incompleto; mantendo "
                 "first boot pendente.\n");
      }
      /* Sync */
      {
        struct super_block *rsb = vfs_root();
        if (rsb && rsb->bdev)
          buffer_cache_sync(rsb->bdev);
      }
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
  local_copy(settings->network_mode, sizeof(settings->network_mode), "static");
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
