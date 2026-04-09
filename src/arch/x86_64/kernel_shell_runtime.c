#include "arch/x86_64/kernel_shell_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/storage_runtime.h"
#include "core/system_init.h"
#include "core/user.h"
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

  int fmt_rc = capyfs_format(ram, 128, ram->block_count, NULL);
  if (fmt_rc != 0) {
    io_print(io, "[fs] ERRO: falha ao formatar CAPYFS em RAM. rc=");
    io_print_hex(io, (uint64_t)(uint32_t)fmt_rc);
    io_putc(io, '\n');
    return -1;
  }
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
    io_print(io, "[fs] ERRO: nao foi possivel preparar /etc/users.db.\n");
    return -1;
  }
  struct user_record existing;
  if (userdb_find("admin", &existing) != 0) {
    struct user_record admin;
    if (user_record_init("admin", "admin", "admin", 0, 0, "/home/admin",
                         &admin) != 0 ||
        userdb_add(&admin) != 0) {
      io_print(io,
               "[fs] ERRO: nao foi possivel criar usuario admin padrao.\n");
      return -1;
    }
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

  if (data_dev && x64_storage_runtime_uses_firmware() &&
      ops->boot_services_active && !ops->boot_services_active()) {
    io_print(io,
             "[fs] aviso: firmware indisponivel apos transicao nativa "
             "(BootServices offline).\n");
    data_dev = NULL;
  }

  if (data_dev && ops->mount_encrypted_data_volume(data_dev) == 0) {
    *state->shell_persistent_storage = 1;
    if (system_detect_first_boot() != 0) {
      io_print(io, "[setup] Primeira inicializacao detectada.\n");
      if (system_run_first_boot_setup() != 0) {
        if (state->allow_recovery_ram_fallback) {
          io_print(io,
                   "[setup] Aviso: assistente inicial falhou; abrindo shell "
                   "de recuperacao sobre o volume montado.\n");
        } else {
          io_print(io, "[setup] ERRO: assistente inicial falhou.\n");
          return -1;
        }
      }
    }
    if (userdb_ensure() != 0) {
      if (state->allow_recovery_ram_fallback) {
        io_print(io,
                 "[fs] Aviso: /etc/users.db indisponivel; recovery shell "
                 "seguira no volume montado para reparo manual.\n");
      } else {
        io_print(
            io,
            "[fs] ERRO: /etc/users.db indisponivel no volume persistente.\n");
        return -1;
      }
    }
    struct vfs_stat userdb_stat;
    if (vfs_stat_path(USER_DB_PATH, &userdb_stat) == 0 &&
        userdb_stat.size == 0) {
      io_print(io, "[setup] users.db vazio detectado no volume persistente.\n");
      if (system_run_first_boot_setup() != 0) {
        if (state->allow_recovery_ram_fallback) {
          io_print(io,
                   "[setup] Aviso: nao foi possivel reconstruir usuarios "
                   "iniciais; recovery shell seguira para reparo manual.\n");
        } else {
          io_print(
              io,
              "[setup] ERRO: nao foi possivel reconstruir usuarios iniciais.\n");
          return -1;
        }
      }
    }
    if (ops->persist_active_volume_key_hash() != 0) {
      io_print(
          io,
          "[fs] aviso: nao foi possivel persistir hash da chave do volume.\n");
    }
  } else {
    if (data_dev) {
      if (state->allow_recovery_ram_fallback) {
        return shell_enter_recovery_ram_runtime(
            state, io, ops,
            "falha ao montar/desbloquear o volume persistente CAPYFS");
      }
      io_print(io, "[fs] ERRO: falha no volume persistente CAPYFS.\n");
      io_print(
          io,
          "[fs] Boot normal nao deve cair para RAM nem formatar automaticamente.\n");
      io_print(io,
               "[fs] Valide a chave e inicialize via ISO para recuperacao.\n");
      return -1;
    }
    if (state->handoff && state->handoff->version >= 2 &&
        ops->handoff_has_firmware_block_io()) {
      if (state->allow_recovery_ram_fallback) {
        return shell_enter_recovery_ram_runtime(
            state, io, ops, "volume DATA nao encontrado no handoff UEFI");
      }
      io_print(io, "[fs] ERRO: handoff sem volume DATA CAPYFS em boot UEFI.\n");
      io_print(io,
               "[fs] Bloqueando fallback em RAM para evitar perda de persistencia.\n");
      return -1;
    }
    if (state->allow_recovery_ram_fallback) {
      if (shell_enter_recovery_ram_runtime(
              state, io, ops, "nenhum volume persistente foi fornecido ao boot") !=
          0) {
        return -1;
      }
    } else {
      io_print(io, "[fs] Sem volume persistente no handoff; usando RAM.\n");
      if (bootstrap_ramdisk_runtime(state, io, ops) != 0) {
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
  return 0;
}
