#include "internal/uefi_loader_internal.h"

static EFI_STATUS write_boot_partition_raw(EFI_BLOCK_IO_PROTOCOL *bio,
                                           UINT64 boot_lba, UINT64 boot_sectors,
                                           const struct boot_manifest *mf,
                                           const UINT8 *kernel,
                                           UINTN kernel_sz) {
  if (!bio || !bio->Media || !mf || !kernel)
    return EFI_INVALID_PARAMETER;
  if (bio->Media->BlockSize != 512)
    return EFI_UNSUPPORTED;
  UINT64 total_bytes = boot_sectors * 512ULL;
  UINT32 ksec = (UINT32)((kernel_sz + 511U) / 512U);
  UINT64 needed = 512ULL + (UINT64)ksec * 512ULL;
  if (needed > total_bytes)
    return EFI_OUT_OF_RESOURCES;

  UINT8 mfs[512];
  for (UINTN i = 0; i < 512; i++)
    mfs[i] = 0;
  const UINT8 *mfb = (const UINT8 *)mf;
  for (UINTN i = 0; i < sizeof(struct boot_manifest) && i < 512; i++)
    mfs[i] = mfb[i];
  EFI_STATUS stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio,
                                     bio->Media->MediaId, boot_lba, 512, mfs);
  if (EFI_ERROR(stt))
    return stt;

  UINTN kbytes = (UINTN)ksec * 512U;
  UINT8 *kbuf = AllocatePool(kbytes);
  if (!kbuf)
    return EFI_OUT_OF_RESOURCES;
  for (UINTN i = 0; i < kbytes; i++)
    kbuf[i] = 0;
  for (UINTN i = 0; i < kernel_sz; i++)
    kbuf[i] = kernel[i];
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          boot_lba + 1ULL, kbytes, kbuf);
  FreePool(kbuf);
  if (EFI_ERROR(stt))
    return stt;

  uefi_call_wrapper(bio->FlushBlocks, 1, bio);
  return EFI_SUCCESS;
}

EFI_STATUS installer_run(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
  EFI_HANDLE fs_handle = NULL;
  EFI_FILE_HANDLE root = NULL;
  EFI_STATUS stt = open_boot_volume(image, st, &fs_handle, &root);
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] Instalador: falha ao abrir volume de boot: %r\r\n", stt);
    return stt;
  }

  VOID *bootx64_buf = NULL, *kernel_buf = NULL;
  UINTN bootx64_sz = 0, kernel_sz = 0;
  stt = read_file(root, L"\\EFI\\BOOT\\BOOTX64.EFI", &bootx64_buf, &bootx64_sz);
  if (EFI_ERROR(stt))
    stt = read_file(root, L"EFI\\BOOT\\BOOTX64.EFI", &bootx64_buf, &bootx64_sz);
  if (EFI_ERROR(stt)) {
    Print(
        L"[UEFI] Instalador: BOOTX64.EFI n\u00E3o encontrado no volume: %r\r\n",
        stt);
    return stt;
  }

  stt = read_file(root, L"BOOT\\CAPYOS64.BIN", &kernel_buf, &kernel_sz);
  if (EFI_ERROR(stt) && kernel_buf) {
    FreePool(kernel_buf);
    kernel_buf = NULL;
    kernel_sz = 0;
  }
  if (EFI_ERROR(stt))
    stt = read_file(root, L"\\BOOT\\CAPYOS64.BIN", &kernel_buf, &kernel_sz);
  if (EFI_ERROR(stt) && kernel_buf) {
    FreePool(kernel_buf);
    kernel_buf = NULL;
    kernel_sz = 0;
  }
  if (EFI_ERROR(stt))
    stt = read_file(root, L"\\boot\\capyos64.bin", &kernel_buf, &kernel_sz);
  if (EFI_ERROR(stt) && kernel_buf) {
    FreePool(kernel_buf);
    kernel_buf = NULL;
    kernel_sz = 0;
  }
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] Instalador: kernel n\u00E3o encontrado no volume: %r\r\n",
          stt);
    return stt;
  }

  EFI_BLOCK_IO_PROTOCOL *disk = NULL;
  stt = choose_target_disk(st, &disk);
  if (EFI_ERROR(stt) || !disk || !disk->Media) {
    Print(L"[UEFI] Instalador: nenhum disco grav\u00E1vel encontrado.\r\n");
    return EFI_NOT_FOUND;
  }

  UINT64 disk_bytes =
      ((UINT64)disk->Media->LastBlock + 1ULL) * (UINT64)disk->Media->BlockSize;

  /* ============================================================
   * INSTALLER WIZARD - Full interactive setup
   * ============================================================ */
  Print(L"\r\n");
  Print(L"========================================\r\n");
  Print(L"      CapyOS 64-bit - Installer Wizard\r\n");
  Print(L"========================================\r\n");
  Print(L"\r\n");
  Print(L"Target disk: %lu MiB\r\n", (disk_bytes / (1024ULL * 1024ULL)));
  Print(L"\r\n");
  Print(L"[WARNING] ALL DATA ON THE TARGET DISK WILL BE ERASED!\r\n");
  Print(L"\r\n");
  Print(L"Press 'I' to start or any other key to cancel: ");

  EFI_INPUT_KEY key;
  UINTN idx = 0;
  uefi_call_wrapper(st->BootServices->WaitForEvent, 3, 1,
                    &st->ConIn->WaitForKey, &idx);
  stt = uefi_call_wrapper(st->ConIn->ReadKeyStroke, 2, st->ConIn, &key);
  if (EFI_ERROR(stt) || (key.UnicodeChar != L'I' && key.UnicodeChar != L'i')) {
    Print(L"\r\n[UEFI] Installation cancelled.\r\n");
    return EFI_ABORTED;
  }
  Print(L"\r\n\r\n");

  /* --- Step 1: Installer/system language --- */
  installer_language_t install_language = INSTALLER_LANG_EN;
  CHAR16 language_in[32];
  Print(L"=== Language ===\r\n\r\n");
  Print(L"  [1] English\r\n");
  Print(L"  [2] Portugues (Brasil)\r\n");
  Print(L"  [3] Espanol\r\n\r\n");
  Print(L"Select language [1]: ");
  uefi_readline(st, language_in, 32, FALSE);
  if (language_in[0] == L'2' || language_in[0] == L'p' ||
      language_in[0] == L'P') {
    install_language = INSTALLER_LANG_PT_BR;
  } else if (language_in[0] == L'3' || language_in[0] == L'e' ||
             language_in[0] == L'E') {
    install_language = INSTALLER_LANG_ES;
  }
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"Idioma selecionado: %s\r\n\r\n",
          installer_language_name(install_language));
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"Idioma seleccionado: %s\r\n\r\n",
          installer_language_name(install_language));
  } else {
    Print(L"Selected language: %s\r\n\r\n",
          installer_language_name(install_language));
  }

  /* --- Step 2: Keyboard layout preference --- */
  CHAR16 keyboard_layout[16];
  keyboard_layout[0] = L'u';
  keyboard_layout[1] = L's';
  keyboard_layout[2] = 0;
  CHAR16 layout_in[32];
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"=== Layout de Teclado ===\r\n\r\n");
    Print(L"  [1] us        (US English)\r\n");
    Print(L"  [2] br-abnt2  (Portugues Brasil)\r\n\r\n");
    Print(L"Layout preferido [1]: ");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"=== Layout del Teclado ===\r\n\r\n");
    Print(L"  [1] us        (US English)\r\n");
    Print(L"  [2] br-abnt2  (Portugues Brasil)\r\n\r\n");
    Print(L"Layout preferido [1]: ");
  } else {
    Print(L"=== Keyboard Layout ===\r\n\r\n");
    Print(L"  [1] us        (US English)\r\n");
    Print(L"  [2] br-abnt2  (Portuguese Brazil)\r\n\r\n");
    Print(L"Preferred layout [1]: ");
  }
  uefi_readline(st, layout_in, 32, FALSE);
  if (layout_in[0] == L'2' || layout_in[0] == L'b' || layout_in[0] == L'B') {
    keyboard_layout[0] = L'b';
    keyboard_layout[1] = L'r';
    keyboard_layout[2] = L'-';
    keyboard_layout[3] = L'a';
    keyboard_layout[4] = L'b';
    keyboard_layout[5] = L'n';
    keyboard_layout[6] = L't';
    keyboard_layout[7] = L'2';
    keyboard_layout[8] = 0;
  }
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"Layout selecionado: %s\r\n\r\n", keyboard_layout);
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"Layout seleccionado: %s\r\n\r\n", keyboard_layout);
  } else {
    Print(L"Selected layout: %s\r\n\r\n", keyboard_layout);
  }

  /* --- Step 3: Hostname --- */
  CHAR16 hostname_in[32];
  hostname_in[0] = 0;
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"=== Nome do Host ===\r\n\r\n");
    Print(L"Hostname [capyos-node]: ");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"=== Nombre del Host ===\r\n\r\n");
    Print(L"Hostname [capyos-node]: ");
  } else {
    Print(L"=== Hostname ===\r\n\r\n");
    Print(L"Hostname [capyos-node]: ");
  }
  uefi_readline(st, hostname_in, 32, FALSE);
  if (hostname_in[0] == 0) {
    hostname_in[0] = L'c'; hostname_in[1] = L'a'; hostname_in[2] = L'p';
    hostname_in[3] = L'y'; hostname_in[4] = L'o'; hostname_in[5] = L's';
    hostname_in[6] = L'-'; hostname_in[7] = L'n'; hostname_in[8] = L'o';
    hostname_in[9] = L'd'; hostname_in[10] = L'e'; hostname_in[11] = 0;
  }
  Print(L"Hostname: %s\r\n\r\n", hostname_in);

  /* --- Step 4: Theme --- */
  CHAR16 theme_in[16];
  theme_in[0] = L'c'; theme_in[1] = L'a'; theme_in[2] = L'p';
  theme_in[3] = L'y'; theme_in[4] = L'o'; theme_in[5] = L's';
  theme_in[6] = 0;
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"=== Tema ===\r\n\r\n");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"=== Tema ===\r\n\r\n");
  } else {
    Print(L"=== Theme ===\r\n\r\n");
  }
  Print(L"  [1] capyos\r\n");
  Print(L"  [2] ocean\r\n");
  Print(L"  [3] forest\r\n\r\n");
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"Tema [1]: ");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"Tema [1]: ");
  } else {
    Print(L"Theme [1]: ");
  }
  {
    CHAR16 theme_pick[8];
    uefi_readline(st, theme_pick, 8, FALSE);
    if (theme_pick[0] == L'2') {
      theme_in[0] = L'o'; theme_in[1] = L'c'; theme_in[2] = L'e';
      theme_in[3] = L'a'; theme_in[4] = L'n'; theme_in[5] = 0;
    } else if (theme_pick[0] == L'3') {
      theme_in[0] = L'f'; theme_in[1] = L'o'; theme_in[2] = L'r';
      theme_in[3] = L'e'; theme_in[4] = L's'; theme_in[5] = L't';
      theme_in[6] = 0;
    }
  }
  Print(L"Theme: %s\r\n\r\n", theme_in);

  /* --- Step 5: Splash --- */
  UINT8 splash_enabled = 1;
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"Ativar splash animado? [S/n]: ");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"Activar splash animado? [S/n]: ");
  } else {
    Print(L"Enable animated splash? [Y/n]: ");
  }
  {
    CHAR16 splash_pick[8];
    uefi_readline(st, splash_pick, 8, FALSE);
    if (splash_pick[0] == L'n' || splash_pick[0] == L'N') {
      splash_enabled = 0;
    }
  }
  Print(L"\r\n");

  /* --- Step 6: Admin account --- */
  CHAR16 admin_user_in[32];
  CHAR16 admin_pass_in[64];
  admin_user_in[0] = L'a'; admin_user_in[1] = L'd'; admin_user_in[2] = L'm';
  admin_user_in[3] = L'i'; admin_user_in[4] = L'n'; admin_user_in[5] = 0;
  admin_pass_in[0] = 0;

  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"=== Conta Administrativa ===\r\n\r\n");
    Print(L"Usuario administrador [admin]: ");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"=== Cuenta Administrativa ===\r\n\r\n");
    Print(L"Usuario administrador [admin]: ");
  } else {
    Print(L"=== Administrator Account ===\r\n\r\n");
    Print(L"Administrator user [admin]: ");
  }
  {
    CHAR16 user_pick[32];
    uefi_readline(st, user_pick, 32, FALSE);
    if (user_pick[0] != 0) {
      for (UINTN i = 0; i < 31 && user_pick[i]; ++i) {
        admin_user_in[i] = user_pick[i];
        admin_user_in[i + 1] = 0;
      }
    }
  }
  Print(L"Admin user: %s\r\n", admin_user_in);

  /* Admin password with confirmation */
  while (1) {
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"Senha para %s: ", admin_user_in);
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"Contrasena para %s: ", admin_user_in);
    } else {
      Print(L"Password for %s: ", admin_user_in);
    }
    uefi_readline(st, admin_pass_in, 64, TRUE);
    if (admin_pass_in[0] == 0) {
      if (install_language == INSTALLER_LANG_PT_BR) {
        Print(L"Senha nao pode ser vazia.\r\n");
      } else if (install_language == INSTALLER_LANG_ES) {
        Print(L"La contrasena no puede estar vacia.\r\n");
      } else {
        Print(L"Password cannot be empty.\r\n");
      }
      continue;
    }
    CHAR16 admin_pass_confirm[64];
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"Confirme a senha: ");
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"Confirmar contrasena: ");
    } else {
      Print(L"Confirm password: ");
    }
    uefi_readline(st, admin_pass_confirm, 64, TRUE);
    int match = 1;
    for (UINTN i = 0; i < 64; ++i) {
      if (admin_pass_in[i] != admin_pass_confirm[i]) { match = 0; break; }
      if (admin_pass_in[i] == 0) break;
    }
    /* Zero confirm buffer */
    for (UINTN i = 0; i < 64; ++i) admin_pass_confirm[i] = 0;
    if (match) break;
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"Senhas nao conferem. Tente novamente.\r\n");
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"Las contrasenas no coinciden. Intente de nuevo.\r\n");
    } else {
      Print(L"Passwords do not match. Try again.\r\n");
    }
  }
  Print(L"\r\n");

  /* --- Step 7: Volume key guidance --- */
  CHAR16 recovery_key[64];
  char recovery_key_norm[64];
  generate_recovery_key(st, recovery_key, sizeof(recovery_key) / sizeof(recovery_key[0]));
  if (normalize_key_char16(recovery_key, recovery_key_norm,
                           sizeof(recovery_key_norm)) != 0) {
    Print(L"[UEFI] Falha ao gerar chave de volume.\r\n");
    return EFI_DEVICE_ERROR;
  }
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"\r\n=== Chave do Volume Cifrado ===\r\n\r\n");
    Print(L"Chave gerada automaticamente para o volume:\r\n");
    Print(L"  %s\r\n\r\n", recovery_key);
    Print(L"Guarde essa chave em local seguro.\r\n");
    Print(L"No primeiro boot ela sera usada para montar/inicializar o volume cifrado.\r\n");
    Print(L"Formato aceito no sistema: letras/numeros, hifens opcionais.\r\n");
    Print(L"\r\nValidacao manual da chave esta desativada nesta fase.\r\n");
    Print(L"Pressione ENTER para continuar...");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"\r\n=== Clave del Volumen Cifrado ===\r\n\r\n");
    Print(L"Clave generada automaticamente para el volumen:\r\n");
    Print(L"  %s\r\n\r\n", recovery_key);
    Print(L"Guarda esta clave en un lugar seguro.\r\n");
    Print(L"En el primer arranque sera usada para montar/inicializar el volumen cifrado.\r\n");
    Print(L"Formato aceptado: letras/numeros, guiones opcionales.\r\n");
    Print(L"\r\nLa validacion manual de la clave esta deshabilitada en esta fase.\r\n");
    Print(L"Presiona ENTER para continuar...");
  } else {
    Print(L"\r\n=== Encrypted Volume Key ===\r\n\r\n");
    Print(L"Automatically generated key for the volume:\r\n");
    Print(L"  %s\r\n\r\n", recovery_key);
    Print(L"Store this key in a safe place.\r\n");
    Print(L"It will be used on the first boot to mount/initialize the encrypted volume.\r\n");
    Print(L"Accepted format: letters/numbers, hyphens optional.\r\n");
    Print(L"\r\nManual key validation is disabled at this stage.\r\n");
    Print(L"Press ENTER to continue...");
  }
  CHAR16 continue_line[8];
  uefi_readline(st, continue_line, sizeof(continue_line) / sizeof(continue_line[0]),
                FALSE);
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"[info] Layout selecionado para o setup: %s\r\n", keyboard_layout);
    Print(L"[info] Idioma padrao do sistema: %s\r\n\r\n",
          installer_language_code(install_language));
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"[info] Layout seleccionado para el setup: %s\r\n", keyboard_layout);
    Print(L"[info] Idioma predeterminado del sistema: %s\r\n\r\n",
          installer_language_code(install_language));
  } else {
    Print(L"[info] Selected setup layout: %s\r\n", keyboard_layout);
    Print(L"[info] System default language: %s\r\n\r\n",
          installer_language_code(install_language));
  }

  /* --- Step 8: Confirm installation --- */
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"=== Confirmacao Final ===\r\n\r\n");
    Print(L"Layout teclado: %s\r\n", keyboard_layout);
    Print(L"Idioma padrao: %s\r\n", installer_language_code(install_language));
    Print(L"Hostname: %s\r\n", hostname_in);
    Print(L"Tema: %s\r\n", theme_in);
    Print(L"Usuario administrador: %s\r\n", admin_user_in);
    Print(L"Disco: %lu MiB (sera APAGADO)\r\n",
          (disk_bytes / (1024ULL * 1024ULL)));
    Print(L"\r\nConfirmar instalacao? [S/n]: ");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"=== Confirmacion Final ===\r\n\r\n");
    Print(L"Layout teclado: %s\r\n", keyboard_layout);
    Print(L"Idioma predeterminado: %s\r\n",
          installer_language_code(install_language));
    Print(L"Hostname: %s\r\n", hostname_in);
    Print(L"Tema: %s\r\n", theme_in);
    Print(L"Usuario administrador: %s\r\n", admin_user_in);
    Print(L"Disco: %lu MiB (sera BORRADO)\r\n",
          (disk_bytes / (1024ULL * 1024ULL)));
    Print(L"\r\nConfirmar instalacion? [S/n]: ");
  } else {
    Print(L"=== Final Confirmation ===\r\n\r\n");
    Print(L"Keyboard layout: %s\r\n", keyboard_layout);
    Print(L"Default language: %s\r\n", installer_language_code(install_language));
    Print(L"Hostname: %s\r\n", hostname_in);
    Print(L"Theme: %s\r\n", theme_in);
    Print(L"Administrator user: %s\r\n", admin_user_in);
    Print(L"Disk: %lu MiB (WILL BE ERASED)\r\n",
          (disk_bytes / (1024ULL * 1024ULL)));
    Print(L"\r\nConfirm installation? [Y/n]: ");
  }
  CHAR16 confirm[8];
  uefi_readline(st, confirm, 8, FALSE);
  if (confirm[0] == L'n' || confirm[0] == L'N') {
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"[UEFI] Instalacao cancelada pelo usuario.\r\n");
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"[UEFI] Instalacion cancelada por el usuario.\r\n");
    } else {
      Print(L"[UEFI] Installation cancelled by the user.\r\n");
    }
    return EFI_ABORTED;
  }
  Print(L"\r\n");

  // Clean install policy: wipe entire target disk before creating a new GPT.
  UINT64 full_disk_sectors = (UINT64)disk->Media->LastBlock + 1ULL;
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"[UEFI] Limpando disco inteiro...\r\n");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"[UEFI] Limpiando el disco completo...\r\n");
  } else {
    Print(L"[UEFI] Wiping the full disk...\r\n");
  }
  stt = wipe_blocks(disk, 0, full_disk_sectors);
  if (EFI_ERROR(stt)) {
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"[UEFI] Falha ao limpar disco: %r\r\n", stt);
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"[UEFI] Fallo al limpiar el disco: %r\r\n", stt);
    } else {
      Print(L"[UEFI] Failed to wipe the disk: %r\r\n", stt);
    }
    return stt;
  }

  UINT64 esp_lba = 0, esp_secs = 0, boot_lba = 0, boot_secs = 0;
  UINT64 data_lba = 0, data_secs = 0;
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"[UEFI] Gravando GPT...\r\n");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"[UEFI] Escribiendo GPT...\r\n");
  } else {
    Print(L"[UEFI] Writing GPT...\r\n");
  }
  stt = gpt_write_layout(st, disk, INSTALL_ESP_SIZE_MIB, INSTALL_BOOT_SIZE_MIB,
                         &esp_lba, &esp_secs, &boot_lba, &boot_secs, &data_lba,
                         &data_secs);
  if (EFI_ERROR(stt)) {
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"[UEFI] GPT falhou: %r\r\n", stt);
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"[UEFI] GPT fallo: %r\r\n", stt);
    } else {
      Print(L"[UEFI] GPT failed: %r\r\n", stt);
    }
    return stt;
  }

  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"[UEFI] Preparando particao DATA para primeiro boot...\r\n");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"[UEFI] Preparando la particion DATA para el primer arranque...\r\n");
  } else {
    Print(L"[UEFI] Preparing DATA partition for first boot...\r\n");
  }
  stt = scrub_data_partition_for_first_boot(disk, data_lba, data_secs);
  if (EFI_ERROR(stt)) {
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"[UEFI] Falha ao preparar DATA: %r\r\n", stt);
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"[UEFI] Fallo al preparar DATA: %r\r\n", stt);
    } else {
      Print(L"[UEFI] Failed to prepare DATA: %r\r\n", stt);
    }
    return stt;
  }

  // Build manifest for BOOT partition: manifest@0, kernel@+1
  struct boot_manifest mf;
  UINT32 ksec = (UINT32)((kernel_sz + 511U) / 512U);
  UINT32 cksum = checksum32_words((const UINT8 *)kernel_buf, kernel_sz);
  build_manifest(&mf, 1, ksec, cksum);

  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"[UEFI] Criando ESP (FAT32) e copiando arquivos...\r\n");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"[UEFI] Creando ESP (FAT32) y copiando archivos...\r\n");
  } else {
    Print(L"[UEFI] Creating ESP (FAT32) and copying files...\r\n");
  }
  struct boot_config_sector boot_cfg;
  bootcfg_clear(&boot_cfg);
  boot_cfg.magic = BOOT_CONFIG_MAGIC;
  boot_cfg.version = BOOT_CONFIG_VERSION;
  boot_cfg.flags = BOOT_CONFIG_FLAG_HAS_VOLUME_KEY | BOOT_CONFIG_FLAG_HAS_SETUP_DATA;
  char16_to_ascii(boot_cfg.keyboard_layout, sizeof(boot_cfg.keyboard_layout),
                  keyboard_layout);
  char16_to_ascii(boot_cfg.language, sizeof(boot_cfg.language),
                  installer_language_code(install_language));
  char16_to_ascii(boot_cfg.hostname, sizeof(boot_cfg.hostname), hostname_in);
  char16_to_ascii(boot_cfg.theme, sizeof(boot_cfg.theme), theme_in);
  char16_to_ascii(boot_cfg.admin_username, sizeof(boot_cfg.admin_username),
                  admin_user_in);
  char16_to_ascii(boot_cfg.admin_password, sizeof(boot_cfg.admin_password),
                  admin_pass_in);
  boot_cfg.splash_enabled = splash_enabled;
  /* Zero password from stack immediately */
  for (UINTN i = 0; i < sizeof(admin_pass_in) / sizeof(admin_pass_in[0]); ++i)
    ((volatile CHAR16 *)admin_pass_in)[i] = 0;
  for (UINTN i = 0; i + 1 < sizeof(boot_cfg.volume_key) && recovery_key_norm[i];
       ++i) {
    boot_cfg.volume_key[i] = recovery_key_norm[i];
  }
  UINTN key_len = 0;
  while (recovery_key_norm[key_len]) {
    ++key_len;
  }
  UINTN persisted_len = 0;
  while (persisted_len < sizeof(boot_cfg.volume_key) &&
         boot_cfg.volume_key[persisted_len]) {
    ++persisted_len;
  }
  if (key_len == 0 || key_len >= sizeof(boot_cfg.volume_key) ||
      persisted_len != key_len ||
      !ascii_streq(boot_cfg.volume_key, recovery_key_norm)) {
    Print(L"[UEFI] ERRO: chave de volume nao persistivel no BOOT config.\r\n");
    return EFI_CRC_ERROR;
  }

  stt = fat32_write_volume(disk, esp_lba, esp_secs, (const UINT8 *)bootx64_buf,
                           bootx64_sz, (const UINT8 *)kernel_buf, kernel_sz,
                           (const UINT8 *)&mf, sizeof(mf),
                           (const UINT8 *)&boot_cfg, sizeof(boot_cfg));
  if (EFI_ERROR(stt)) {
    if (stt == EFI_CRC_ERROR) {
      Print(L"[UEFI] ERRO: chave em CAPYCFG.BIN diverge da chave provisionada.\r\n");
    }
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"[UEFI] FAT32/ESP falhou: %r\r\n", stt);
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"[UEFI] FAT32/ESP fallo: %r\r\n", stt);
    } else {
      Print(L"[UEFI] FAT32/ESP failed: %r\r\n", stt);
    }
    return stt;
  }

  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"[UEFI] Gravando BOOT (manifest+kernel)...\r\n");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"[UEFI] Escribiendo BOOT (manifest+kernel)...\r\n");
  } else {
    Print(L"[UEFI] Writing BOOT (manifest+kernel)...\r\n");
  }
  stt = write_boot_partition_raw(disk, boot_lba, boot_secs, &mf,
                                 (const UINT8 *)kernel_buf, kernel_sz);
  if (EFI_ERROR(stt)) {
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"[UEFI] BOOT raw falhou: %r\r\n", stt);
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"[UEFI] BOOT raw fallo: %r\r\n", stt);
    } else {
      Print(L"[UEFI] BOOT raw failed: %r\r\n", stt);
    }
    return stt;
  }

  uefi_call_wrapper(disk->FlushBlocks, 1, disk);

  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"[UEFI] Instalacao concluida. Reiniciando...\r\n");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"[UEFI] Instalacion completada. Reiniciando...\r\n");
  } else {
    Print(L"[UEFI] Installation complete. Rebooting...\r\n");
  }
  uefi_call_wrapper(st->RuntimeServices->ResetSystem, 4, EfiResetCold,
                    EFI_SUCCESS, 0, NULL);
  return EFI_SUCCESS;
}

