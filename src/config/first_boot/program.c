#include "../internal/first_boot_internal.h"

static const char *g_cli_reference_text =
    "CapyCLI - Referencia Rapida\n"
    "============================\n"
    "\n"
    "Navegacao\n"
    "  list [caminho]       - lista itens\n"
    "  go <caminho>         - altera diretorio atual\n"
    "  mypath               - mostra caminho corrente\n"
    "\n"
    "Arquivo\n"
    "  print-file <arq>     - mostra conteudo\n"
    "  page <arq>           - paginacao\n"
    "  mk-file <arq>        - cria arquivo vazio\n"
    "  mk-dir <dir>         - cria diretorio\n"
    "  kill-file <arq>      - remove arquivo\n"
    "  kill-dir <dir>       - remove diretorio vazio\n"
    "  clone <src> <dst>    - copia arquivo\n"
    "  move <src> <dst>     - move/renomeia\n"
    "  stats-file <alvo>    - exibe metadados\n"
    "  type <alvo>          - identifica tipo\n"
    "\n"
    "Busca\n"
    "  hunt-file <padrao> [onde]\n"
    "  hunt-dir  <padrao> [onde]\n"
    "  hunt-any  <padrao> [onde]\n"
    "  find "
    "texto"
    " [onde]    - procura conteudo\n"
    "\n"
    "Processos & Sistema\n"
    "  print-me             - usuario atual\n"
    "  print-id             - uid/gid\n"
    "  print-host           - hostname\n"
    "  print-time           - uptime\n"
    "  do-sync              - sincroniza discos\n"
    "  perf-boot            - metricas de boot\n"
    "  perf-net             - metricas de rede/DNS\n"
    "  perf-fs              - metricas de FS/cache\n"
    "  perf-mem             - metricas de memoria\n"
    "  net-status           - estado da rede (x64)\n"
    "  net-ip               - exibe IPv4 local e mascara\n"
    "  net-gw               - exibe gateway atual\n"
    "  net-dns              - exibe DNS atual\n"
    "  net-set <ip> <mask> <gw> <dns> - aplica IPv4 estatico\n"
    "  hey <destino>        - ping (ICMP echo)\n"
    "  config-theme [tema]  - altera tema visual\n"
    "  config-splash [modo] - altera splash no boot\n"
    "  config-language [idioma] - altera idioma do usuario\n"
    "  config-keyboard [layout] - altera layout do teclado\n"
    "  mess                 - limpa tela\n"
    "  bye                  - encerra sessao\n"
    "\n"
    "Ajuda\n"
    "  help-any             - lista comandos\n"
    "  help-docs            - exibe este resumo\n"
    "\n"
    "Observacao: caminhos relativos respeitam o diretorio ativo.\n";

int system_detect_first_boot(void) {
  struct vfs_stat st;
  int marker_exists = (vfs_stat_path("/system/first-run.done", &st) == 0);

  int has_users = 0;
  if (vfs_stat_path(USER_DB_PATH, &st) == 0) {
    has_users = (userdb_has_any_user() > 0) ? 1 : 0;
  }

  {
    int config_exists = (vfs_stat_path("/system/config.ini", &st) == 0);

    if (marker_exists && has_users && config_exists) {
      return 0;
    }
    if (marker_exists && (!has_users || !config_exists)) {
      vfs_unlink("/system/first-run.done");
      return 1;
    }
    if (!marker_exists && has_users && config_exists) {
      (void)system_mark_first_boot_complete();
      return 0;
    }
  }

  return 1;
}

int system_mark_first_boot_complete(void) {
  if (config_ensure_directory("/system") != 0) {
    return -1;
  }
  return config_write_text_file("/system/first-run.done", "completed\n");
}

static int setup_create_directories(const char *setup_language) {
  const char *proc_dirs = "preparacao de diretorios padrao";
  config_log_process_begin(proc_dirs);
  config_log_process_begin_success(proc_dirs);

  const char *std_dirs[] = {"/bin", "/etc", "/home", "/tmp",
                            "/var", "/var/log", "/system", "/docs"};
  for (size_t i = 0; i < sizeof(std_dirs) / sizeof(std_dirs[0]); ++i) {
    config_debug_print_heap("[setup] ensure path: ", std_dirs[i]);
    if (config_ensure_directory(std_dirs[i]) != 0) {
      config_print_line("Falha ao preparar estrutura de diretorios:");
      config_print_line(std_dirs[i]);
      return -1;
    }
  }
  config_log_process_progress(proc_dirs);
  for (size_t i = 0; i < sizeof(std_dirs) / sizeof(std_dirs[0]); ++i) {
    if (config_verify_directory_exists(std_dirs[i]) != 0) {
      config_print_line("Verificacao de diretorios falhou:");
      config_print_line(std_dirs[i]);
      return -1;
    }
  }
  config_print_line("   Estrutura de diretorios pronta.");
  config_log_flush_pending();
  config_log_process_conclude(proc_dirs);
  config_log_process_finalize(proc_dirs);
  config_log_process_finalize_success(proc_dirs);

  struct vfs_metadata meta_tmp;
  meta_tmp.uid = 0;
  meta_tmp.gid = 0;
  meta_tmp.perm = 0777;
  vfs_set_metadata("/tmp", &meta_tmp);

  {
    struct vfs_metadata meta_var = {0, 0, 0755};
    vfs_set_metadata("/var", &meta_var);
  }

  {
    struct vfs_metadata meta_var_log = {0, 0, 0777};
    vfs_set_metadata("/var/log", &meta_var_log);
  }

  {
    struct vfs_metadata meta_log_file = {0, 0, 0666};
    if (config_write_text_file("/var/log/cli-selftest.log", "") == 0) {
      vfs_set_metadata("/var/log/cli-selftest.log", &meta_log_file);
    }
    if (vfs_set_metadata("/var/log/setup.log", &meta_log_file) != 0) {
      config_write_text_file("/var/log/setup.log", "");
      vfs_set_metadata("/var/log/setup.log", &meta_log_file);
    }
  }
  (void)setup_language;
  return 0;
}

static int setup_prepare_userdb(const char *setup_language) {
  const char *proc_userdb = "preparacao da base de usuarios";
  config_log_process_begin(proc_userdb);
  config_log_process_begin_success(proc_userdb);
  if (userdb_ensure() != 0) {
    config_print_line("Nao foi possivel criar /etc/users.db.");
    return -1;
  }
  if (config_verify_directory_exists("/etc") != 0) {
    config_print_line("Diretorio /etc inacessivel.");
    return -1;
  }
  config_log_process_progress(proc_userdb);
  struct vfs_stat userdb_stat;
  if (vfs_stat_path(USER_DB_PATH, &userdb_stat) != 0) {
    config_print_line("Nao foi possivel validar /etc/users.db.");
    return -1;
  }
  char userdb_size[12];
  config_u32_to_string(userdb_stat.size, userdb_size, sizeof(userdb_size));
  char size_msg[128];
  size_msg[0] = '\0';
  config_buffer_append(size_msg, sizeof(size_msg),
                       "   /etc/users.db disponivel (");
  config_buffer_append(size_msg, sizeof(size_msg), userdb_size);
  config_buffer_append(size_msg, sizeof(size_msg), " bytes).");
  config_print_line(size_msg);
  config_sync_root_device();
  config_log_process_conclude(proc_userdb);
  config_log_process_finalize(proc_userdb);
  config_log_process_finalize_success(proc_userdb);
  (void)setup_language;
  return 0;
}

static int setup_write_docs(const char *setup_language) {
  const char *proc_docs = "instalacao da referencia CapyCLI";
  config_log_process_begin(proc_docs);
  config_log_process_begin_success(proc_docs);
  if (config_write_text_file("/docs/capyos-cli-reference.txt",
                             g_cli_reference_text) != 0) {
    config_print_line(
        "   Aviso: nao foi possivel gravar referencia do CapyCLI.");
  } else {
    config_print_line(
        "   Referencia CapyCLI pronta em /docs/capyos-cli-reference.txt.");
  }
  if (system_prepare_update_catalog() != 0) {
    config_print_line(
        "   Aviso: nao foi possivel preparar /system/update.");
  }
  config_sync_root_device();
  config_log_process_conclude(proc_docs);
  config_log_process_finalize(proc_docs);
  config_log_process_finalize_success(proc_docs);
  (void)setup_language;
  return 0;
}

static int setup_write_settings_and_mark(const char *setup_language,
                                         const char *hostname,
                                         const char *theme,
                                         int splash_enabled) {
  struct system_settings settings;
  cstring_copy(settings.hostname, sizeof(settings.hostname), "capyos-node");
  cstring_copy(settings.theme, sizeof(settings.theme), "capyos");
  cstring_copy(settings.keyboard_layout, sizeof(settings.keyboard_layout),
               g_boot_default_keyboard_layout);
  cstring_copy(settings.language, sizeof(settings.language),
               g_boot_default_language);
  cstring_copy(settings.update_channel, sizeof(settings.update_channel),
               system_update_channel_or_default(NULL));
  cstring_copy(settings.network_mode, sizeof(settings.network_mode),
               system_network_mode_or_default(NULL));
  cstring_copy(settings.service_target, sizeof(settings.service_target),
               system_service_target_or_default(NULL));
  settings.ipv4_addr = 0;
  settings.ipv4_mask = 0;
  settings.ipv4_gateway = 0;
  settings.ipv4_dns = 0;
  settings.splash_enabled = 1;
  settings.diagnostics_enabled = 0;

  const char *keyboard_value = keyboard_current_layout();
  if (!keyboard_value || !keyboard_value[0]) {
    keyboard_value = "us";
  }
  cstring_copy(settings.hostname, sizeof(settings.hostname), hostname);
  cstring_copy(settings.theme, sizeof(settings.theme), theme);
  cstring_copy(settings.keyboard_layout, sizeof(settings.keyboard_layout),
               keyboard_value);
  cstring_copy(settings.language, sizeof(settings.language), setup_language);
  settings.splash_enabled = splash_enabled ? 1 : 0;

  const char *proc_config = "gravacao da configuracao do sistema";
  config_log_process_begin(proc_config);
  config_log_process_begin_success(proc_config);
  if (config_write_settings_file(&settings) != 0) {
    config_print_line(
        system_ui_text(setup_language, SYS_UI_CONFIG_WRITE_FAIL));
    return -1;
  }
  config_sync_root_device();
  if (config_verify_config_file(
          settings.hostname, settings.theme, settings.keyboard_layout,
          settings.language, settings.update_channel, settings.network_mode,
          settings.service_target, settings.splash_enabled,
          settings.ipv4_addr, settings.ipv4_mask, settings.ipv4_gateway,
          settings.ipv4_dns) != 0) {
    return -1;
  }
  config_print_line(
      system_ui_text(setup_language, SYS_UI_CONFIG_VALIDATED));

  if (system_mark_first_boot_complete() != 0) {
    config_print_line(
        system_ui_text(setup_language, SYS_UI_FIRST_BOOT_COMPLETE_FAIL));
    return -1;
  }
  config_sync_root_device();
  config_log_process_conclude(proc_config);
  config_log_process_finalize(proc_config);
  config_log_process_finalize_success(proc_config);
  return 0;
}

static int first_boot_silent_provision(void) {
  config_print_line("Provisionamento automatico ja executado pelo runtime.");
  return 0;
}

static int first_boot_setup_interactive(void) {
  const char *setup_language =
      system_language_or_default(g_boot_default_language);
  char hostname[TTY_BUFFER_MAX];
  const char *theme = "capyos";
  int splash_enabled = 1;
  char layout_choice[32];
  char admin_username[USER_NAME_MAX];
  char admin_password[TTY_BUFFER_MAX];

  config_first_boot_log_reset();

  if (strings_equal(setup_language, "en")) {
    config_print_line("=== CapyOS Initial Setup ===");
    config_print_line(
        "This wizard prepares users, settings, and the base system structure.");
  } else if (strings_equal(setup_language, "es")) {
    config_print_line("=== Configuracion Inicial de CapyOS ===");
    config_print_line(
        "Este asistente prepara usuarios, configuracion y la estructura "
        "basica.");
  } else {
    config_print_line("=== Assistente CapyOS - Configuracao Inicial ===");
    config_print_line(
        "Este assistente prepara usuarios, configuracao e estrutura basica.");
  }
  vga_newline();

  if (setup_create_directories(setup_language) != 0) return -1;
  if (setup_prepare_userdb(setup_language) != 0) return -1;
  if (setup_write_docs(setup_language) != 0) return -1;

  memory_zero(layout_choice, sizeof(layout_choice));
  {
    size_t layout_count = keyboard_layout_count();
    size_t selected_layout = 0;
    char layout_labels[16][96];
    const char *layout_items[16];

    if (layout_count > 16u) {
      layout_count = 16u;
    }
    for (size_t i = 0; i < layout_count; ++i) {
      layout_labels[i][0] = '\0';
      config_buffer_append(layout_labels[i], sizeof(layout_labels[i]),
                           keyboard_layout_name(i));
      config_buffer_append(layout_labels[i], sizeof(layout_labels[i]),
                           " - ");
      config_buffer_append(layout_labels[i], sizeof(layout_labels[i]),
                           keyboard_layout_description(i));
      layout_items[i] = layout_labels[i];
      if (strings_equal(keyboard_layout_name(i),
                        g_boot_default_keyboard_layout)) {
        selected_layout = i;
      }
    }

    selected_layout = (size_t)wizard_menu_select_setup(
        20u, system_ui_text(setup_language, SYS_UI_LAYOUTS_AVAILABLE),
        setup_language, layout_items, layout_count, selected_layout);
    cstring_copy(layout_choice, sizeof(layout_choice),
                 keyboard_layout_name(selected_layout));
    if (keyboard_set_layout_by_name(layout_choice) != 0) {
      cstring_copy(layout_choice, sizeof(layout_choice), "us");
      keyboard_set_layout_by_name(layout_choice);
      config_print_line(
          system_ui_text(setup_language, SYS_UI_LAYOUT_UNKNOWN));
    }
  }

  memory_zero(hostname, sizeof(hostname));
  const char *proc_settings = "coleta de configuracoes basicas";
  config_log_process_begin(proc_settings);
  config_log_process_begin_success(proc_settings);
  size_t hlen = wizard_prompt_setup(
      40u, "Hostname",
      system_ui_text(setup_language, SYS_UI_HOSTNAME_PROMPT), hostname,
      sizeof(hostname), 0);
  if (hlen == 0) {
    cstring_copy(hostname, sizeof(hostname), "capyos-node");
  }

  {
    const char *theme_items[3];
    if (strings_equal(setup_language, "en")) {
      theme_items[0] = "CapyOS - default";
      theme_items[1] = "Ocean - blue accents";
      theme_items[2] = "Forest - green accents";
    } else if (strings_equal(setup_language, "es")) {
      theme_items[0] = "CapyOS - predeterminado";
      theme_items[1] = "Ocean - tonos azules";
      theme_items[2] = "Forest - tonos verdes";
    } else {
      theme_items[0] = "CapyOS - padrao";
      theme_items[1] = "Ocean - tons azuis";
      theme_items[2] = "Forest - tons verdes";
    }
    int theme_pick = wizard_menu_select_setup(
        55u, system_ui_text(setup_language, SYS_UI_THEMES_AVAILABLE),
        setup_language, theme_items,
        sizeof(theme_items) / sizeof(theme_items[0]), 0);
    if (theme_pick == 0) {
      theme = config_validate_theme("capyos");
    } else if (theme_pick == 1) {
      theme = config_validate_theme("ocean");
    } else {
      theme = config_validate_theme("forest");
    }
  }

  {
    const char *splash_items[] = {
        system_ui_menu_enabled(setup_language),
        system_ui_menu_disabled(setup_language)};
    int splash_pick = wizard_menu_select_setup(
        70u, system_ui_splash_menu_title(setup_language), setup_language,
        splash_items, 2, 0);
    splash_enabled = (splash_pick == 0) ? 1 : 0;
  }

  memory_zero(admin_username, sizeof(admin_username));
  size_t ulen = wizard_prompt_setup(
      85u, "Administrator",
      system_ui_text(setup_language, SYS_UI_ADMIN_USER_PROMPT),
      admin_username, sizeof(admin_username), 0);
  if (ulen == 0) {
    cstring_copy(admin_username, sizeof(admin_username), "admin");
  }
  if (!config_validate_admin_username(admin_username)) {
    config_print_line(
        system_ui_text(setup_language, SYS_UI_ADMIN_USER_INVALID));
    cstring_copy(admin_username, sizeof(admin_username), "admin");
  }

  uint32_t admin_uid = 1000;
  uint32_t admin_gid = 1000;
  memory_zero(admin_password, sizeof(admin_password));
  config_log_process_progress(proc_settings);
  const char *proc_admin = "provisionamento do usuario administrador";
  config_log_dependency_wait(proc_settings, proc_admin);
  config_log_process_begin(proc_admin);
  config_log_process_begin_success(proc_admin);
  char admin_home[USER_HOME_MAX];
  memory_zero(admin_home, sizeof(admin_home));
  config_build_home_path(admin_username, admin_home, sizeof(admin_home));
  if (config_ensure_directory(admin_home) != 0) {
    config_print_line(
        system_ui_text(setup_language, SYS_UI_ADMIN_HOME_CREATE_FAIL));
    memory_zero(admin_password, sizeof(admin_password));
    return -1;
  }
  if (config_verify_directory_exists(admin_home) != 0) {
    config_print_line(
        system_ui_text(setup_language, SYS_UI_ADMIN_HOME_UNAVAILABLE));
    memory_zero(admin_password, sizeof(admin_password));
    return -1;
  }
  {
    struct vfs_metadata home_meta = {admin_uid, admin_gid, 0700};
    if (vfs_set_metadata(admin_home, &home_meta) != 0) {
      config_print_line(
          system_ui_text(setup_language, SYS_UI_ADMIN_HOME_PERM_WARNING));
    }
  }
  config_sync_root_device();

  {
    int admin_ready = 0;
    struct user_record existing;
    if (userdb_find(admin_username, &existing) == 0) {
      config_print_line(system_ui_text(setup_language, SYS_UI_ADMIN_EXISTS));
      if (config_verify_directory_exists(existing.home) != 0) {
        char rebuild_msg[128];
        rebuild_msg[0] = '\0';
        config_buffer_append(
            rebuild_msg, sizeof(rebuild_msg),
            system_ui_text(setup_language,
                           SYS_UI_ADMIN_HOME_REBUILD_PREFIX));
        config_buffer_append(rebuild_msg, sizeof(rebuild_msg), existing.home);
        config_buffer_append(rebuild_msg, sizeof(rebuild_msg), ".");
        config_print_line(rebuild_msg);
        if (config_ensure_directory(existing.home) != 0 ||
            config_verify_directory_exists(existing.home) != 0) {
          config_print_line(system_ui_text(
              setup_language, SYS_UI_ADMIN_HOME_REBUILD_FAIL));
          return -1;
        }
      }
      admin_uid = existing.uid;
      admin_gid = existing.gid;
      (void)user_prefs_save_language(&existing, setup_language);
      admin_ready = 1;
    }

    char password_prompt[96];
    password_prompt[0] = '\0';
    config_buffer_append(
        password_prompt, sizeof(password_prompt),
        system_ui_text(setup_language, SYS_UI_ADMIN_PASSWORD_PROMPT_PREFIX));
    config_buffer_append(password_prompt, sizeof(password_prompt),
                         admin_username);
    config_buffer_append(password_prompt, sizeof(password_prompt), ": ");

    while (!admin_ready) {
      wizard_draw_header(95u, "Administrator password");
      if (prompt_password_pair(password_prompt, admin_password,
                               sizeof(admin_password), setup_language) != 0) {
        config_print_line(
            system_ui_text(setup_language, SYS_UI_ADMIN_REGISTER_FAIL));
        memory_zero(admin_password, sizeof(admin_password));
        continue;
      }

      struct user_record admin;
      if (user_record_init(admin_username, admin_password, "admin", admin_uid,
                           admin_gid, admin_home, &admin) != 0) {
        config_print_line(system_ui_text(setup_language,
                                         SYS_UI_ADMIN_RECORD_BUILD_FAIL));
        memory_zero(admin_password, sizeof(admin_password));
        continue;
      }

      if (userdb_add(&admin) != 0) {
        config_print_line(
            system_ui_text(setup_language, SYS_UI_ADMIN_SAVE_FAIL));
        memory_zero(admin_password, sizeof(admin_password));
        continue;
      }

      config_sync_root_device();

      {
        struct user_record verify_rec;
        if (userdb_authenticate(admin_username, admin_password, &verify_rec) !=
            0) {
          config_print_line(system_ui_text(
              setup_language, SYS_UI_ADMIN_AUTH_REBUILD_FAIL));
          vfs_unlink(USER_DB_PATH);
          if (userdb_ensure() != 0) {
            config_print_line(system_ui_text(
                setup_language, SYS_UI_ADMIN_USERDB_REBUILD_FAIL));
            memory_zero(admin_password, sizeof(admin_password));
            return -1;
          }
          config_sync_root_device();
          memory_zero(admin_password, sizeof(admin_password));
          continue;
        }

        (void)user_prefs_save_language(&verify_rec, setup_language);
      }
      admin_uid = admin.uid;
      admin_gid = admin.gid;
      config_print_line(
          system_ui_text(setup_language, SYS_UI_ADMIN_VALIDATED));
      memory_zero(admin_password, sizeof(admin_password));
      admin_ready = 1;
    }
  }

  memory_zero(admin_password, sizeof(admin_password));
  config_log_process_conclude(proc_admin);
  config_log_process_finalize(proc_admin);
  config_log_process_finalize_success(proc_admin);

  config_log_process_progress(proc_settings);
  if (setup_write_settings_and_mark(setup_language, hostname, theme,
                                    splash_enabled) != 0) {
    return -1;
  }

  {
    char uid_buf[12];
    char gid_buf[12];
    config_u32_to_string(admin_uid, uid_buf, sizeof(uid_buf));
    config_u32_to_string(admin_gid, gid_buf, sizeof(gid_buf));
    char admin_summary[160];
    admin_summary[0] = '\0';
    config_buffer_append(admin_summary, sizeof(admin_summary),
                         "Administrador configurado: ");
    config_buffer_append(admin_summary, sizeof(admin_summary), admin_username);
    config_buffer_append(admin_summary, sizeof(admin_summary), " (UID ");
    config_buffer_append(admin_summary, sizeof(admin_summary), uid_buf);
    config_buffer_append(admin_summary, sizeof(admin_summary), ", GID ");
    config_buffer_append(admin_summary, sizeof(admin_summary), gid_buf);
    config_buffer_append(admin_summary, sizeof(admin_summary), ")");
    config_print_line(admin_summary);
  }
  vga_newline();

  {
    struct user_record final_rec;
    if (userdb_find(admin_username, &final_rec) == 0) {
      config_print_line(
          "   Validacao final do registro do administrador concluida.");
      config_log_user_record_state(&final_rec);
    } else {
      config_print_line(
          "   Aviso: nao foi possivel reler registro do administrador "
          "apos configuracao.");
    }
  }

  config_log_flush_pending();
  config_log_process_conclude(proc_settings);
  config_log_process_finalize(proc_settings);
  config_log_process_finalize_success(proc_settings);
  return 0;
}

static int first_boot_setup_impl(void) {
  if (system_installer_config_available()) {
    return first_boot_silent_provision();
  }
  return first_boot_setup_interactive();
}

int system_run_first_boot_setup(void) { return first_boot_setup_impl(); }

int system_run_first_boot_setup_with_password(const char *admin_password) {
  (void)admin_password;
  return first_boot_setup_impl();
}
