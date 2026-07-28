#include "internal/uefi_loader_internal.h"

static EFI_STATUS __attribute__((unused)) write_boot_partition_legacy(EFI_BLOCK_IO_PROTOCOL *bio,
                                           UINT32 expected_media_id,
                                           UINT64 boot_lba, UINT64 boot_sectors,
                                           const struct boot_manifest *mf,
                                           const UINT8 *kernel,
                                           UINTN kernel_sz) {
  if (!bio || !bio->Media || !mf || !kernel)
    return EFI_INVALID_PARAMETER;
  if (bio->Media->BlockSize != 512)
    return EFI_UNSUPPORTED;
  if (bio->Media->MediaId != expected_media_id || boot_sectors == 0u ||
      boot_lba > bio->Media->LastBlock ||
      boot_sectors - 1u > bio->Media->LastBlock - boot_lba ||
      boot_sectors > UINT64_MAX / 512ULL || kernel_sz == 0u ||
      kernel_sz > UINT32_MAX || kernel_sz > ~(UINTN)0 - 511u)
    return EFI_INVALID_PARAMETER;
  UINT64 total_bytes = boot_sectors * 512ULL;
  UINTN ksec_n = kernel_sz / 512u;
  if (kernel_sz % 512u != 0u)
    ksec_n++;
  if (ksec_n == 0u || ksec_n > UINT32_MAX)
    return EFI_INVALID_PARAMETER;
  UINT32 ksec = (UINT32)ksec_n;
  UINT64 needed = 512ULL + (UINT64)ksec * 512ULL;
  if (needed > total_bytes || boot_lba == UINT64_MAX)
    return EFI_OUT_OF_RESOURCES;

  UINT8 mfs[512];
  for (UINTN i = 0; i < 512; i++)
    mfs[i] = 0;
  const UINT8 *mfb = (const UINT8 *)mf;
  for (UINTN i = 0; i < sizeof(struct boot_manifest) && i < 512; i++)
    mfs[i] = mfb[i];
  EFI_STATUS stt = uefi_block_io_write(bio, expected_media_id, boot_lba, sizeof(mfs), mfs);
  if (EFI_ERROR(stt))
    return stt;

  UINTN kbytes = (UINTN)ksec * 512U;
  VOID *kallocation = NULL;
  VOID *kbuffer = NULL;
  stt = uefi_block_io_allocate_aligned(bio, kbytes, &kallocation, &kbuffer);
  if (EFI_ERROR(stt))
    return stt;
  UINT8 *kbuf = (UINT8 *)kbuffer;
  for (UINTN i = 0; i < kbytes; i++)
    kbuf[i] = 0;
  for (UINTN i = 0; i < kernel_sz; i++)
    kbuf[i] = kernel[i];
  stt = uefi_block_io_write(bio, expected_media_id, boot_lba + 1ULL, kbytes, kbuf);
  FreePool(kallocation);
  if (EFI_ERROR(stt))
    return stt;
  return uefi_block_io_flush(bio, expected_media_id);
}

static EFI_STATUS write_boot_partition_raw(EFI_BLOCK_IO_PROTOCOL *bio,
                                          UINT32 expected_media_id,
                                          UINT64 boot_lba,
                                          UINT64 boot_sectors,
                                          const UINT8 *kernel,
                                          UINTN kernel_sz) {
  struct boot_slot_layout layout;
  struct boot_slot_image image;
  struct boot_slot_store store;
  struct boot_slot_manager manager;
  struct uefi_boot_store_context ctx;
  UINT8 header[BOOT_SLOT_STORE_SECTOR_SIZE];
  UINT8 readback[BOOT_SLOT_STORE_SECTOR_SIZE];
  VOID *allocation = NULL;
  VOID *buffer = NULL;
  UINTN rounded;
  UINT32 sectors;
  EFI_STATUS status;
  if (!bio || !bio->Media || !kernel || bio->Media->BlockSize != 512u ||
      bio->Media->MediaId != expected_media_id || boot_sectors == 0u ||
      boot_sectors > UINT32_MAX || boot_lba > bio->Media->LastBlock ||
      boot_sectors - 1u > bio->Media->LastBlock - boot_lba ||
      kernel_sz == 0u || kernel_sz > UINT32_MAX ||
      kernel_sz > ~(UINTN)0 - 511u)
    return EFI_INVALID_PARAMETER;
  if (boot_slot_layout_plan((UINT32)boot_sectors, &layout) != 0 ||
      validate_kernel_buffer((VOID *)kernel, kernel_sz) != EFI_SUCCESS)
    return EFI_UNSUPPORTED;
  sectors = (UINT32)((kernel_sz + 511u) / 512u);
  if (sectors == 0u || sectors > layout.slots[0].payload_capacity_sectors)
    return EFI_OUT_OF_RESOURCES;
  rounded = (UINTN)sectors * 512u;
  for (UINTN i = 0u; i < sizeof(image); ++i)
    ((UINT8 *)&image)[i] = 0u;
  for (UINTN i = 0u; i + 1u < sizeof(image.version) && CAPYOS_VERSION_FULL[i];
       ++i)
    image.version[i] = CAPYOS_VERSION_FULL[i];
  image.payload_size = (UINT32)kernel_sz;
  sha256_hash(kernel, kernel_sz, image.payload_sha256);
  status = uefi_block_io_allocate_aligned(bio, rounded, &allocation, &buffer);
  if (EFI_ERROR(status))
    return status;
  for (UINTN i = 0u; i < rounded; ++i)
    ((UINT8 *)buffer)[i] = i < kernel_sz ? kernel[i] : 0u;
  status = uefi_block_io_write(bio, expected_media_id,
                               boot_lba + layout.slots[0].payload_lba,
                               rounded, buffer);
  if (!EFI_ERROR(status))
    status = uefi_block_io_flush(bio, expected_media_id);
  if (!EFI_ERROR(status)) {
    for (UINTN i = 0u; i < rounded; ++i)
      ((UINT8 *)buffer)[i] = 0u;
    status = uefi_block_io_read(bio, expected_media_id,
                                boot_lba + layout.slots[0].payload_lba,
                                rounded, buffer);
  }
  if (!EFI_ERROR(status)) {
    UINT8 digest[SHA256_DIGEST_SIZE];
    sha256_hash(buffer, kernel_sz, digest);
    if (!uefi_slot_bytes_equal(digest, image.payload_sha256,
                               SHA256_DIGEST_SIZE))
      status = EFI_CRC_ERROR;
    for (UINTN i = kernel_sz; !EFI_ERROR(status) && i < rounded; ++i) {
      if (((UINT8 *)buffer)[i] != 0u)
        status = EFI_CRC_ERROR;
    }
  }
  FreePool(allocation);
  if (EFI_ERROR(status))
    return status;
  if (boot_slot_store_encode_header(&layout, 0u, &image, header) != 0)
    return EFI_COMPROMISED_DATA;
  status = uefi_block_io_write(bio, expected_media_id,
                               boot_lba + layout.slots[0].header_lba,
                               sizeof(header), header);
  if (!EFI_ERROR(status))
    status = uefi_block_io_flush(bio, expected_media_id);
  if (!EFI_ERROR(status))
    status = uefi_block_io_read(bio, expected_media_id,
                                boot_lba + layout.slots[0].header_lba,
                                sizeof(readback), readback);
  if (EFI_ERROR(status) ||
      !uefi_slot_bytes_equal(header, readback, sizeof(header)))
    return EFI_ERROR(status) ? status : EFI_CRC_ERROR;
  ctx.bio = bio;
  ctx.media_id = expected_media_id;
  ctx.boot_lba = boot_lba;
  ctx.boot_sectors = (UINT32)boot_sectors;
  if (boot_slot_init() != 0 ||
      boot_slot_store_init(&store, &layout, uefi_boot_store_read,
                           uefi_boot_store_write, uefi_boot_store_flush,
                           &ctx) != 0 ||
      boot_slot_store_bind_control(&store, 0u) != BOOT_SLOT_PERSIST_EMPTY ||
      boot_slot_store_initialize_persistent(&store, 0u, &image) != 0 ||
      boot_slot_manager_get(&manager) != 0 || manager.active_slot != 0u ||
      manager.confirmed_slot != 0u || manager.pending_slot != BOOT_SLOT_NONE ||
      boot_slot_persistence_generation() != 2u)
    return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}

static UINT32 installer_get_u32(const UINT8 *data) {
  return (UINT32)data[0] | ((UINT32)data[1] << 8) |
         ((UINT32)data[2] << 16) | ((UINT32)data[3] << 24);
}

static int installer_bootx64_valid(const UINT8 *data, UINTN size) {
  UINT32 pe_offset;
  if (!data || size < 0x40u || data[0] != 'M' || data[1] != 'Z')
    return 0;
  pe_offset = installer_get_u32(data + 0x3Cu);
  if (pe_offset > size || size - pe_offset < 26u ||
      data[pe_offset] != 'P' || data[pe_offset + 1u] != 'E' ||
      data[pe_offset + 2u] != 0u || data[pe_offset + 3u] != 0u ||
      data[pe_offset + 4u] != 0x64u || data[pe_offset + 5u] != 0x86u ||
      data[pe_offset + 24u] != 0x0Bu || data[pe_offset + 25u] != 0x02u)
    return 0;
  return 1;
}

static EFI_STATUS installer_release(EFI_FILE_HANDLE root, VOID *bootx64_buf,
                                    VOID *kernel_buf, EFI_STATUS status) {
  if (root)
    uefi_call_wrapper(root->Close, 1, root);
  if (bootx64_buf)
    FreePool(bootx64_buf);
  if (kernel_buf)
    FreePool(kernel_buf);
  return status;
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
    return installer_release(root, bootx64_buf, kernel_buf, stt);
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
    return installer_release(root, bootx64_buf, kernel_buf, stt);
  }

  installer_disk_target_t target;
  EFI_BLOCK_IO_PROTOCOL *disk = NULL;

  uefi_installer_serial_init();
  uefi_installer_serial_write("\r\n=== CapyOS Installer Wizard ===\r\n");
  Print(L"\r\n");
  Print(L"========================================\r\n");
  Print(L"      CapyOS 64-bit - Installer Wizard\r\n");
  Print(L"========================================\r\n");

  stt = choose_target_disk(st, &target);
  if (stt == EFI_ABORTED) {
    Print(L"\r\n[UEFI] Installation cancelled.\r\n");
    return installer_release(root, bootx64_buf, kernel_buf, EFI_ABORTED);
  }
  if (EFI_ERROR(stt) || !target.bio || !target.bio->Media) {
    Print(L"[UEFI] Installer: no eligible writable disk found: %r\r\n", stt);
    return installer_release(root, bootx64_buf, kernel_buf,
                             EFI_ERROR(stt) ? stt : EFI_NOT_FOUND);
  }
  disk = target.bio;

  UINT64 disk_bytes =
      target.geometry.block_count * (UINT64)target.geometry.block_size;

  Print(L"\r\n");
  Print(L"Target disk: %lu MiB\r\n", (disk_bytes / (1024ULL * 1024ULL)));
  Print(L"\r\n");
  Print(L"[WARNING] ALL DATA ON THE TARGET DISK WILL BE ERASED!\r\n");
  Print(L"\r\n");
  Print(L"Press 'I' to start or any other key to cancel: ");
  uefi_installer_serial_write("Press 'I' to start or any other key to cancel: ");

  EFI_INPUT_KEY key;
  if (uefi_installer_read_key(st, &key) != 0 ||
      (key.UnicodeChar != L'I' && key.UnicodeChar != L'i')) {
    Print(L"\r\n[UEFI] Installation cancelled.\r\n");
    return installer_release(root, bootx64_buf, kernel_buf, EFI_ABORTED);
  }
  Print(L"\r\n\r\n");

  /*
   * alpha.241: installer UEFI is intentionally minimal.
   *
   * The installer no longer asks for language, keyboard, hostname,
   * theme or admin credentials. All of those are collected by the
   * first-boot wizard inside the kernel (`first_boot_setup_interactive`)
   * the first time the freshly installed system boots. This keeps the
   * installer ISO focused on disk preparation and avoids duplicating
   * UI between two contexts (UEFI loader and the kernel TUI).
   *
   * The only thing that must happen here is generating the volume
   * recovery key — it has to be persisted to boot_cfg BEFORE the FAT32
   * write so the kernel can mount the encrypted DATA partition on first
   * boot. We print the key once so the operator can record it.
   */
  installer_language_t install_language = INSTALLER_LANG_EN;
  /* alpha.241: install_language stays at EN for this minimal installer.
   * The first-boot wizard inside the kernel collects the real language
   * choice from the operator and writes it to /system/config.ini. */
  CHAR16 keyboard_layout[16] = L"us";
  CHAR16 hostname_in[32]     = L"capyos-node";
  CHAR16 theme_in[16]        = L"capyos";
  CHAR16 admin_user_in[32]   = L"admin";
  /* admin_pass_in stays empty: the kernel wizard refuses to login
   * with the empty default and forces the operator to set a real one
   * on first boot. boot_cfg therefore carries no usable password. */
  CHAR16 admin_pass_in[64]   = L"";
  UINT8 splash_enabled = 1;

#if 0 /* alpha.241: removed installer-side prompts (Steps 1-6). */
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
  Print(L"  [3] forest\r\n");
  Print(L"  [4] love\r\n\r\n");
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
    } else if (theme_pick[0] == L'4') {
      theme_in[0] = L'l'; theme_in[1] = L'o'; theme_in[2] = L'v';
      theme_in[3] = L'e'; theme_in[4] = 0;
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
#endif /* alpha.241: end of removed installer prompts (Steps 1-6). */

  /* --- Step 7: Volume key guidance --- */
  CHAR16 recovery_key[64];
  char recovery_key_display[64];
  char recovery_key_norm[64];
  generate_recovery_key(st, recovery_key, sizeof(recovery_key) / sizeof(recovery_key[0]));
  char16_to_ascii(recovery_key_display, sizeof(recovery_key_display), recovery_key);
  if (normalize_key_char16(recovery_key, recovery_key_norm,
                           sizeof(recovery_key_norm)) != 0) {
    Print(L"[UEFI] Falha ao gerar chave de volume.\r\n");
    return installer_release(root, bootx64_buf, kernel_buf, EFI_DEVICE_ERROR);
  }
  /* alpha.241: minimal volume-key disclosure and confirmation.
   *
   * The recovery key MUST be shown here because the operator has no
   * other opportunity to record it before the kernel mounts the
   * encrypted DATA partition. Everything else (language, keyboard,
   * hostname, theme, admin credentials, module selection) is now
   * collected by the in-kernel wizard, not here. */
  Print(L"\r\n=== Volume Recovery Key ===\r\n\r\n");
  Print(L"  %s\r\n\r\n", recovery_key);
  uefi_installer_serial_write("\r\n=== Volume Recovery Key ===\r\n\r\n  ");
  uefi_installer_serial_write(recovery_key_display);
  uefi_installer_serial_write("\r\n\r\n");
  Print(L"Record this key. The first-boot wizard inside CapyOS will\r\n");
  Print(L"collect language, keyboard, hostname, theme, admin user,\r\n");
  Print(L"password and module selection on the installed system.\r\n\r\n");
  Print(L"Target disk: PathId %016lx, MediaId %u, %lu MiB "
        L"(WILL BE ERASED)\r\n",
        target.path_id, target.media_id,
        (disk_bytes / (1024ULL * 1024ULL)));
  Print(L"Type ERASE and press ENTER to confirm: ");
  uefi_installer_serial_write("Type ERASE and press ENTER to confirm: ");
  CHAR16 confirm[8];
  char confirm_ascii[8];
  uefi_readline(st, confirm, 8, FALSE);
  char16_to_ascii(confirm_ascii, sizeof(confirm_ascii), confirm);
  if (!installer_disk_confirmation_valid(confirm_ascii)) {
    Print(L"[UEFI] Installation cancelled: confirmation did not match.\r\n");
    return installer_release(root, bootx64_buf, kernel_buf, EFI_ABORTED);
  }
  stt = installer_revalidate_target(st, &target);
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] Target disk changed or failed preflight: %r\r\n", stt);
    return installer_release(root, bootx64_buf, kernel_buf, stt);
  }
  Print(L"\r\n");
  {
    struct boot_slot_layout preflight_layout;
    UINTN preflight_sectors = (kernel_sz + 511u) / 512u;
    if (!installer_bootx64_valid(bootx64_buf, bootx64_sz) ||
        kernel_sz == 0u || kernel_sz > UINT32_MAX ||
        kernel_sz > ~(UINTN)0 - 511u ||
        target.layout.boot_sectors > UINT32_MAX || preflight_sectors == 0u ||
        boot_slot_layout_plan((UINT32)target.layout.boot_sectors,
                              &preflight_layout) != 0 ||
        preflight_sectors > preflight_layout.slots[0].payload_capacity_sectors ||
        validate_kernel_buffer(kernel_buf, kernel_sz) != EFI_SUCCESS) {
      Print(L"[UEFI] Kernel/BOOT A/B preflight failed before erase.\r\n");
      return installer_release(root, bootx64_buf, kernel_buf,
                               EFI_LOAD_ERROR);
    }
  }

  // Clean install policy: wipe entire target disk before creating a new GPT.
  UINT64 full_disk_sectors = target.layout.total_sectors;
  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"[UEFI] Limpando disco inteiro...\r\n");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"[UEFI] Limpiando el disco completo...\r\n");
  } else {
    Print(L"[UEFI] Wiping the full disk...\r\n");
  }
  stt = wipe_blocks(disk, target.media_id, 0, full_disk_sectors);
  if (EFI_ERROR(stt)) {
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"[UEFI] Falha ao limpar disco: %r\r\n", stt);
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"[UEFI] Fallo al limpiar el disco: %r\r\n", stt);
    } else {
      Print(L"[UEFI] Failed to wipe the disk: %r\r\n", stt);
    }
    return installer_release(root, bootx64_buf, kernel_buf, stt);
  }
  stt = installer_revalidate_target(st, &target);
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] Target disk changed after wipe: %r\r\n", stt);
    return installer_release(root, bootx64_buf, kernel_buf, stt);
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
  stt = gpt_write_layout(st, disk, target.media_id, &target.layout, &esp_lba, &esp_secs,
                         &boot_lba, &boot_secs, &data_lba, &data_secs);
  if (EFI_ERROR(stt)) {
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"[UEFI] GPT falhou: %r\r\n", stt);
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"[UEFI] GPT fallo: %r\r\n", stt);
    } else {
      Print(L"[UEFI] GPT failed: %r\r\n", stt);
    }
    return installer_release(root, bootx64_buf, kernel_buf, stt);
  }

  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"[UEFI] Preparando particao DATA para primeiro boot...\r\n");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"[UEFI] Preparando la particion DATA para el primer arranque...\r\n");
  } else {
    Print(L"[UEFI] Preparing DATA partition for first boot...\r\n");
  }
  stt = scrub_data_partition_for_first_boot(disk, target.media_id, data_lba,
                                             data_secs);
  if (EFI_ERROR(stt)) {
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"[UEFI] Falha ao preparar DATA: %r\r\n", stt);
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"[UEFI] Fallo al preparar DATA: %r\r\n", stt);
    } else {
      Print(L"[UEFI] Failed to prepare DATA: %r\r\n", stt);
    }
    return installer_release(root, bootx64_buf, kernel_buf, stt);
  }

  // Build manifest for BOOT partition: manifest@0, kernel@+1
  struct boot_manifest mf;
  if (kernel_sz == 0u || kernel_sz > UINT32_MAX ||
      kernel_sz > ~(UINTN)0 - 511u)
    return installer_release(root, bootx64_buf, kernel_buf,
                             EFI_INVALID_PARAMETER);
  UINTN manifest_ksec = kernel_sz / 512u;
  if (kernel_sz % 512u != 0u)
    manifest_ksec++;
  if (manifest_ksec == 0u || manifest_ksec > UINT32_MAX)
    return installer_release(root, bootx64_buf, kernel_buf,
                             EFI_INVALID_PARAMETER);
  UINT32 ksec = (UINT32)manifest_ksec;
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
  /* alpha.241: BOOT_CONFIG_FLAG_HAS_SETUP_DATA intentionally omitted.
   * The kernel's silent-provisioning path is being retired; the
   * first-boot wizard now collects user/hostname/theme/etc. directly.
   * Defaults below stay for backward compatibility with older boot
   * config consumers but the kernel ignores them when the flag is off. */
  boot_cfg.flags = BOOT_CONFIG_FLAG_HAS_VOLUME_KEY;
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
    return installer_release(root, bootx64_buf, kernel_buf, EFI_CRC_ERROR);
  }

  stt = fat32_write_volume(disk, target.media_id, esp_lba, esp_secs, (const UINT8 *)bootx64_buf,
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
    return installer_release(root, bootx64_buf, kernel_buf, stt);
  }

  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"[UEFI] Gravando BOOT (manifest+kernel)...\r\n");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"[UEFI] Escribiendo BOOT (manifest+kernel)...\r\n");
  } else {
    Print(L"[UEFI] Writing BOOT (manifest+kernel)...\r\n");
  }
  stt = write_boot_partition_raw(disk, target.media_id, boot_lba, boot_secs,
                                 (const UINT8 *)kernel_buf, kernel_sz);
  if (EFI_ERROR(stt)) {
    if (install_language == INSTALLER_LANG_PT_BR) {
      Print(L"[UEFI] BOOT raw falhou: %r\r\n", stt);
    } else if (install_language == INSTALLER_LANG_ES) {
      Print(L"[UEFI] BOOT raw fallo: %r\r\n", stt);
    } else {
      Print(L"[UEFI] BOOT raw failed: %r\r\n", stt);
    }
    return installer_release(root, bootx64_buf, kernel_buf, stt);
  }

  stt = uefi_block_io_flush(disk, target.media_id);
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] Final disk flush failed: %r\r\n", stt);
    return installer_release(root, bootx64_buf, kernel_buf, stt);
  }
  stt = installer_revalidate_target(st, &target);
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] Target disk changed before reboot: %r\r\n", stt);
    return installer_release(root, bootx64_buf, kernel_buf, stt);
  }
  if (!st->RuntimeServices || !st->RuntimeServices->ResetSystem)
    return installer_release(root, bootx64_buf, kernel_buf, EFI_UNSUPPORTED);
  (void)installer_release(root, bootx64_buf, kernel_buf, EFI_SUCCESS);
  root = NULL;
  bootx64_buf = NULL;
  kernel_buf = NULL;

  if (install_language == INSTALLER_LANG_PT_BR) {
    Print(L"[UEFI] Instalacao concluida. Reiniciando...\r\n");
  } else if (install_language == INSTALLER_LANG_ES) {
    Print(L"[UEFI] Instalacion completada. Reiniciando...\r\n");
  } else {
    Print(L"[UEFI] Installation complete. Rebooting...\r\n");
  }
  uefi_installer_serial_write("[UEFI] Installation complete. Rebooting...\r\n");
  uefi_call_wrapper(st->RuntimeServices->ResetSystem, 4, EfiResetCold,
                    EFI_SUCCESS, 0, NULL);
  return EFI_DEVICE_ERROR;
}

