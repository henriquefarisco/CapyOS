#include "internal/uefi_loader_internal.h"

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab) {
  InitializeLib(image, systab);
  Print(L"CapyOS UEFI loader: iniciando [DBG-BUILD-V2]\r\n");
  disable_uefi_watchdog(systab);

  {
    EFI_LOADED_IMAGE *self_image = NULL;
    EFI_STATUS self_st = uefi_call_wrapper(systab->BootServices->HandleProtocol,
                                           3, image, &LoadedImageProtocol,
                                           (VOID **)&self_image);
    if (!EFI_ERROR(self_st) && self_image) {
      Print(L"[UEFI] Loader image base=0x%lx size=0x%lx\r\n",
            (UINT64)(UINTN)self_image->ImageBase, (UINT64)self_image->ImageSize);
    }
  }
  {
    EFI_STATUS reserve_st = kernel_reserve_fixed_window(systab);
    if (EFI_ERROR(reserve_st)) {
      Print(L"[UEFI] Aviso: reserva antecipada do kernel@0x%lx (%lu KiB) falhou: %r\r\n",
            (UINT64)KERNEL_FIXED_RESERVE_BASE,
            (UINT64)(KERNEL_FIXED_RESERVE_BYTES / 1024ULL), reserve_st);
    } else {
      Print(L"[UEFI] Reserva antecipada do kernel: base=0x%lx size=%lu KiB\r\n",
            (UINT64)g_kernel_reserved_base,
            (UINT64)((UINT64)g_kernel_reserved_pages << 2));
    }
  }

  // Carrega boot config cedo para detectar instalacao anterior.
  {
    EFI_HANDLE early_fs = NULL;
    EFI_FILE_HANDLE early_root = NULL;
    if (!EFI_ERROR(open_boot_volume(image, systab, &early_fs, &early_root)) &&
        early_root) {
      (void)load_boot_config_from_root(early_root);
      uefi_call_wrapper(early_root->Close, 1, early_root);
    }
  }

  // Modo instalador: ISO de instalacao contem um marcador (CAPYOS.INI).
  // Se o boot config ja possui SETUP_DATA, o sistema foi configurado por uma
  // instalacao anterior — pular modo instalador mesmo que readonly/marker.
  BOOLEAN install_marker = boot_volume_has_marker(image, systab);
  BOOLEAN install_ro = boot_volume_is_readonly(image, systab);
  BOOLEAN install_cdrom = boot_device_is_cdrom(image, systab);
  BOOLEAN already_installed = (g_runtime_boot_cfg_valid &&
      (g_runtime_boot_cfg.flags & BOOT_CONFIG_FLAG_HAS_SETUP_DATA));
  if (already_installed && (install_marker || install_ro || install_cdrom)) {
    Print(L"[UEFI] Instalacao anterior detectada; ignorando modo instalador "
          L"(marker=%d readonly=%d cdrom=%d)\r\n",
          install_marker ? 1 : 0, install_ro ? 1 : 0, install_cdrom ? 1 : 0);
  }
  if (!already_installed && (install_marker || install_ro || install_cdrom)) {
    kernel_release_fixed_window(systab);
    Print(L"[UEFI] Modo instalador detectado (marker=%d readonly=%d cdrom=%d)\r\n",
          install_marker ? 1 : 0, install_ro ? 1 : 0, install_cdrom ? 1 : 0);
    EFI_STATUS ist = installer_run(image, systab);
    if (!EFI_ERROR(ist)) {
      Print(L"[UEFI] Instalador concluiu; aguardando reinicio do firmware.\r\n");
      for (;;) {
        uefi_call_wrapper(systab->BootServices->Stall, 1, 1000 * 1000);
      }
    }
    if (ist == EFI_ABORTED) {
      Print(L"[UEFI] Instalador cancelado pelo usuario; retornando ao firmware "
            L"para a proxima opcao de boot.\r\n");
      uefi_call_wrapper(systab->BootServices->Stall, 1, 1500 * 1000);
      return ist;
    }

    Print(L"[UEFI] Falha no instalador: %r\r\n", ist);
    Print(L"[UEFI] Permanecendo na tela para diagnostico.\r\n");
    for (;;) {
      uefi_call_wrapper(systab->BootServices->Stall, 1, 1000 * 1000);
    }
  }

  EFI_PHYSICAL_ADDRESS entry = 0;
  EFI_STATUS st = load_kernel_streaming(image, systab, &entry);
  if (EFI_ERROR(st)) {
    Print(L"[UEFI] Falha ao carregar kernel: %r\r\n", st);
    // NÃƒÂ£o retorne ao firmware em caso de erro: isso vira "boot loader failed"
    // no Hyper-V. Mantemos a tela para facilitar debug.
    uefi_call_wrapper(systab->BootServices->Stall, 1, 5 * 1000 * 1000);
    for (;;) {
      uefi_call_wrapper(systab->BootServices->Stall, 1, 1000 * 1000);
    }
  }
  Print(L"[UEFI] Kernel carregado @ 0x%lx\r\n", entry);
  dbgcon_putc('L');

  // Captura RSDP e framebuffer antes de sair do BootServices
  UINT64 rsdp = 0;
  const CHAR16 *rsdp_src16 = L"none";
  const char *rsdp_src8 = "none";
  find_rsdp(systab, &rsdp);
  if (rsdp && rsdp_is_valid_ptr((const VOID *)(UINTN)rsdp)) {
    rsdp_src16 = L"cfg";
    rsdp_src8 = "cfg";
  } else {
    UINT64 mm = 0;
    if (!EFI_ERROR(find_rsdp_memmap(systab, &mm)) && mm &&
        rsdp_is_valid_ptr((const VOID *)(UINTN)mm)) {
      rsdp = mm;
      rsdp_src16 = L"memmap";
      rsdp_src8 = "memmap";
    } else {
      UINT64 scan = 0;
      if (!EFI_ERROR(scan_rsdp(&scan)) && scan) {
        rsdp = scan;
        rsdp_src16 = L"legacy";
        rsdp_src8 = "legacy";
      }
    }
  }

  BOOLEAN rsdp_ok = (rsdp && rsdp_is_valid_ptr((const VOID *)(UINTN)rsdp));
  BOOLEAN rsdp_copied = FALSE;
  UINT64 rsdp_copy = 0;
  if (rsdp_ok && !EFI_ERROR(copy_rsdp_low(systab, rsdp, &rsdp_copy)) &&
      rsdp_copy) {
    rsdp = rsdp_copy;
    rsdp_copied = TRUE;
  }
  if (rsdp_copied) {
    rsdp_ok = rsdp_is_valid_ptr((const VOID *)(UINTN)rsdp);
  }

  Print(L"[UEFI] RSDP src=%s copied=%d addr=0x%lx valid=%d\r\n", rsdp_src16,
        rsdp_copied ? 1 : 0, rsdp, rsdp_ok ? 1 : 0);
  if (rsdp_ok) {
    const acpi_rsdp_t *r = (const acpi_rsdp_t *)(UINTN)rsdp;
    Print(L"[UEFI] ACPI rev=%u rsdt=0x%x\r\n", (UINT32)r->revision,
          (UINT32)r->rsdt);
    if (r->revision >= 2) {
      Print(L"[UEFI] ACPI xsdt=0x%lx len=%u\r\n", (UINT64)r->xsdt,
            (UINT32)r->length);
    }
  }

  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
  struct boot_handoff_fb fb = {0};
  if (!EFI_ERROR(get_gop(systab, &gop)) && gop && gop->Mode &&
      gop->Mode->Info) {
    fb.base = gop->Mode->FrameBufferBase;
    fb.size = (uint32_t)gop->Mode->FrameBufferSize;
    fb.width = gop->Mode->Info->HorizontalResolution;
    fb.height = gop->Mode->Info->VerticalResolution;
    fb.pitch = gop->Mode->Info->PixelsPerScanLine * 4;
    fb.bpp = 32; // GOP usually 32 bpp in BGRA
  }

  EFI_BLOCK_IO_PROTOCOL *runtime_disk = NULL;
  UINT64 runtime_data_lba = 0;
  UINT64 runtime_data_count = 0;
  EFI_BLOCK_IO_PROTOCOL *runtime_disk_raw = NULL;
  UINT64 runtime_data_lba_raw = 0;
  UINT64 runtime_data_count_raw = 0;
  EFI_STATUS runtime_st =
      choose_runtime_disk_with_data(image, systab, &runtime_disk,
                                    &runtime_data_lba, &runtime_data_count,
                                    &runtime_disk_raw, &runtime_data_lba_raw,
                                    &runtime_data_count_raw);
  if (!EFI_ERROR(runtime_st) && runtime_disk && runtime_disk->Media) {
    Print(L"[UEFI] Runtime disk detectado: block=%u last_lba=%lu data=%lu+%lu\r\n",
          runtime_disk->Media->BlockSize,
          (UINT64)runtime_disk->Media->LastBlock, runtime_data_lba,
          runtime_data_count);
    if (runtime_disk_raw && runtime_disk_raw->Media) {
      Print(L"[UEFI] Runtime raw fallback: block=%u last_lba=%lu data=%lu+%lu\r\n",
            runtime_disk_raw->Media->BlockSize,
            (UINT64)runtime_disk_raw->Media->LastBlock, runtime_data_lba_raw,
            runtime_data_count_raw);
    }
  } else {
    Print(L"[UEFI] Runtime disk nao detectado (fallback RAM).\r\n");
  }

  // Alocar handoff e memory map abaixo de 1GiB (compatÃƒÂ­vel com identity map do
  // kernel atual)
  log_file_t logf = {0};
  EFI_STATUS logst = log_open(image, systab, &logf);
  if (EFI_ERROR(logst)) {
    Print(L"[UEFI] log_open falhou: %r\r\n", logst);
  } else {
    log_write_ascii(&logf, "\r\n[CAPYOS] boot start\r\n");
    log_write_ascii(&logf, "[CAPYOS] kernel entry=");
    log_write_u64_hex(&logf, (UINT64)entry);
    log_write_ascii(&logf, "\r\n[CAPYOS] rsdp=");
    log_write_u64_hex(&logf, rsdp);
    log_write_ascii(&logf, "\r\n[CAPYOS] rsdp.src=");
    log_write_ascii(&logf, rsdp_src8);
    log_write_ascii(&logf, "\r\n[CAPYOS] rsdp.copied=");
    log_write_u64_hex(&logf, rsdp_copied ? 1 : 0);
    log_write_ascii(&logf, "\r\n[CAPYOS] fb.base=");
    log_write_u64_hex(&logf, fb.base);
    log_write_ascii(&logf, "\r\n");

    log_write_ascii(&logf, "[CAPYOS] rsdp.valid=");
    log_write_u64_hex(&logf,
                      rsdp_is_valid_ptr((const VOID *)(UINTN)rsdp) ? 1 : 0);
    log_write_ascii(&logf, "\r\n");
    if (rsdp && rsdp_is_valid_ptr((const VOID *)(UINTN)rsdp)) {
      const UINT8 *p = (const UINT8 *)(UINTN)rsdp;
      const acpi_rsdp_t *r = (const acpi_rsdp_t *)(UINTN)rsdp;
      log_write_ascii(&logf, "[CAPYOS] rsdp.rev=");
      log_write_u64_hex(&logf, (UINT64)r->revision);
      log_write_ascii(&logf, " chk=");
      log_write_u64_hex(&logf, (UINT64)r->checksum);
      log_write_ascii(&logf, " rsdt=");
      log_write_u64_hex(&logf, (UINT64)r->rsdt);
      log_write_ascii(&logf, "\r\n");
      log_write_ascii(&logf, "[CAPYOS] rsdp.sum20=");
      log_write_u64_hex(&logf, (UINT64)sum8_bytes(p, 20));
      log_write_ascii(&logf, " bytes20=");
      log_write_bytes_hex(&logf, p, 20);

      if (r->revision >= 2) {
        UINT32 len = r->length;
        if (len < 36 || len > 4096)
          len = 36;
        log_write_ascii(&logf, "[CAPYOS] rsdp.len=");
        log_write_u64_hex(&logf, (UINT64)len);
        log_write_ascii(&logf, " xsdt=");
        log_write_u64_hex(&logf, (UINT64)r->xsdt);
        log_write_ascii(&logf, " xchk=");
        log_write_u64_hex(&logf, (UINT64)r->ext_checksum);
        log_write_ascii(&logf, " sumlen=");
        log_write_u64_hex(&logf, (UINT64)sum8_bytes(p, (UINTN)len));
        log_write_ascii(&logf, " bytes36=");
        log_write_bytes_hex(&logf, p, 36);
      }
    }
    // IMPORTANTE: feche o arquivo ANTES do GetMemoryMap/ExitBootServices.
    // Qualquer I/O/alloc apÃƒÂ³s GetMemoryMap pode alterar o map_key e causar
    // EFI_INVALID_PARAMETER.
    log_close(&logf);
  }

  EFI_PHYSICAL_ADDRESS max_low = 0x3FFFFFFF;
  struct boot_handoff *handoff = NULL;
  st = uefi_call_wrapper(systab->BootServices->AllocatePages, 4,
                         AllocateMaxAddress, EfiLoaderData, 1, &max_low);
  if (EFI_ERROR(st)) {
    Print(L"[UEFI] AllocatePages(handoff) falhou: %r\r\n", st);
    log_close(&logf);
    return st;
  }
  handoff = (struct boot_handoff *)(UINTN)max_low;
  handoff->magic = BOOT_HANDOFF_MAGIC;
  handoff->version = BOOT_HANDOFF_VERSION;
  handoff->rsdp = rsdp_ok ? rsdp : 0;
  handoff->fb = fb;
  handoff->memmap = 0;
  handoff->memmap_desc_size = 0;
  handoff->memmap_entries = 0;
  handoff->memmap_size = 0;
  handoff->memmap_capacity = 0;
  handoff->efi_block_io = 0;
  handoff->efi_disk_last_lba = 0;
  handoff->data_lba_start = 0;
  handoff->data_lba_count = 0;
  handoff->efi_block_size = 0;
  handoff->efi_media_id = 0;
  handoff->efi_block_io_raw = 0;
  handoff->efi_disk_last_lba_raw = 0;
  handoff->data_lba_start_raw = 0;
  handoff->data_lba_count_raw = 0;
  handoff->efi_media_id_raw = 0;
  handoff->runtime_flags = 0;
  handoff->boot_cfg_flags = 0;
  for (UINTN i = 0; i < sizeof(handoff->boot_keyboard_layout); ++i) {
    handoff->boot_keyboard_layout[i] = 0;
  }
  for (UINTN i = 0; i < sizeof(handoff->boot_language); ++i) {
    handoff->boot_language[i] = 0;
  }
  for (UINTN i = 0; i < sizeof(handoff->boot_volume_key); ++i) {
    handoff->boot_volume_key[i] = 0;
  }
  for (UINTN i = 0; i < sizeof(handoff->boot_hostname); ++i) {
    handoff->boot_hostname[i] = 0;
  }
  for (UINTN i = 0; i < sizeof(handoff->boot_theme); ++i) {
    handoff->boot_theme[i] = 0;
  }
  for (UINTN i = 0; i < sizeof(handoff->boot_admin_username); ++i) {
    handoff->boot_admin_username[i] = 0;
  }
  for (UINTN i = 0; i < sizeof(handoff->boot_admin_password); ++i) {
    handoff->boot_admin_password[i] = 0;
  }
  handoff->boot_splash_enabled = 0;
  handoff->efi_image_handle = 0;
  handoff->efi_map_key = 0;
  if (g_runtime_boot_cfg_valid && g_runtime_boot_cfg.magic == BOOT_CONFIG_MAGIC) {
    handoff->boot_cfg_flags = (UINT32)g_runtime_boot_cfg.flags;
    for (UINTN i = 0; i < sizeof(handoff->boot_keyboard_layout) - 1 &&
                     g_runtime_boot_cfg.keyboard_layout[i];
         ++i) {
      handoff->boot_keyboard_layout[i] = g_runtime_boot_cfg.keyboard_layout[i];
    }
    for (UINTN i = 0; i < sizeof(handoff->boot_language) - 1 &&
                     g_runtime_boot_cfg.language[i];
         ++i) {
      handoff->boot_language[i] = g_runtime_boot_cfg.language[i];
    }
    for (UINTN i = 0; i < sizeof(handoff->boot_volume_key) - 1 &&
                     g_runtime_boot_cfg.volume_key[i];
         ++i) {
      handoff->boot_volume_key[i] = g_runtime_boot_cfg.volume_key[i];
    }
    if (g_runtime_boot_cfg.flags & BOOT_CONFIG_FLAG_HAS_SETUP_DATA) {
      for (UINTN i = 0; i < sizeof(handoff->boot_hostname) - 1 &&
                       g_runtime_boot_cfg.hostname[i];
           ++i) {
        handoff->boot_hostname[i] = g_runtime_boot_cfg.hostname[i];
      }
      for (UINTN i = 0; i < sizeof(handoff->boot_theme) - 1 &&
                       g_runtime_boot_cfg.theme[i];
           ++i) {
        handoff->boot_theme[i] = g_runtime_boot_cfg.theme[i];
      }
      for (UINTN i = 0; i < sizeof(handoff->boot_admin_username) - 1 &&
                       g_runtime_boot_cfg.admin_username[i];
           ++i) {
        handoff->boot_admin_username[i] = g_runtime_boot_cfg.admin_username[i];
      }
      for (UINTN i = 0; i < sizeof(handoff->boot_admin_password) - 1 &&
                       g_runtime_boot_cfg.admin_password[i];
           ++i) {
        handoff->boot_admin_password[i] = g_runtime_boot_cfg.admin_password[i];
      }
      handoff->boot_splash_enabled = g_runtime_boot_cfg.splash_enabled;
    }
  }

  UINTN map_sz = 0, map_key = 0, desc_sz = 0;
  UINT32 desc_ver = 0;
  st = uefi_call_wrapper(systab->BootServices->GetMemoryMap, 5, &map_sz, NULL,
                         &map_key, &desc_sz, &desc_ver);
  if (st != EFI_BUFFER_TOO_SMALL) {
    Print(L"[UEFI] GetMemoryMap falhou: %r\r\n", st);
    log_close(&logf);
    return st;
  }

  VOID *map = NULL;
  EFI_PHYSICAL_ADDRESS map_addr = 0;
  UINTN pages = 0;
  for (UINTN attempt = 0; attempt < 8; attempt++) {
    UINTN req = map_sz + desc_sz * 8;
    pages = (req + 0xFFF) >> 12;
    map_addr = 0x3FFFFFFF;
    st = uefi_call_wrapper(systab->BootServices->AllocatePages, 4,
                           AllocateMaxAddress, EfiLoaderData, pages, &map_addr);
    if (EFI_ERROR(st)) {
      Print(L"[UEFI] AllocatePages(memmap) falhou: %r\r\n", st);
      log_close(&logf);
      return st;
    }
    map = (VOID *)(UINTN)map_addr;

    st = uefi_call_wrapper(systab->BootServices->GetMemoryMap, 5, &map_sz, map,
                           &map_key, &desc_sz, &desc_ver);
    if (st == EFI_BUFFER_TOO_SMALL) {
      uefi_call_wrapper(systab->BootServices->FreePages, 2, map_addr, pages);
      continue;
    }
    if (EFI_ERROR(st)) {
      Print(L"[UEFI] GetMemoryMap(2) falhou: %r\r\n", st);
      log_close(&logf);
      return st;
    }

    // Importante: nÃƒÂ£o chame mais nada que possa alocar/IO entre GetMemoryMap e
    // ExitBootServices.

    st = uefi_call_wrapper(systab->BootServices->ExitBootServices, 2, image,
                           map_key);
    if (!EFI_ERROR(st)) {
      break;
    }

    // map_key ficou invÃƒÂ¡lido (firmware mudou o memory map). Libera o buffer e
    // tenta de novo.
    uefi_call_wrapper(systab->BootServices->FreePages, 2, map_addr, pages);
    map = NULL;
    map_addr = 0;
    pages = 0;

    if (st == EFI_INVALID_PARAMETER) {
      // Recalcula tamanho do memory map e tenta novamente.
      map_sz = 0;
      EFI_STATUS stsz =
          uefi_call_wrapper(systab->BootServices->GetMemoryMap, 5, &map_sz,
                            NULL, &map_key, &desc_sz, &desc_ver);
      if (stsz != EFI_BUFFER_TOO_SMALL) {
        Print(L"[UEFI] GetMemoryMap(retry-size) falhou: %r\r\n", stsz);
        return stsz;
      }
      continue;
    }

    Print(L"[UEFI] ExitBootServices falhou: %r\r\n", st);
    // NÃƒÂ£o retorne ao firmware (isso vira PXE/boot fail confuso no Hyper-V).
    // Mantenha a tela.
    uefi_call_wrapper(systab->BootServices->Stall, 1, 5 * 1000 * 1000);
    for (;;) {
      uefi_call_wrapper(systab->BootServices->Stall, 1, 1000 * 1000);
    }
  }
  if (EFI_ERROR(st)) {
    Print(L"[UEFI] ExitBootServices falhou (tentativas excedidas): %r\r\n", st);
    uefi_call_wrapper(systab->BootServices->Stall, 1, 5 * 1000 * 1000);
    for (;;) {
      uefi_call_wrapper(systab->BootServices->Stall, 1, 1000 * 1000);
    }
  }

  handoff->memmap = (UINT64)(UINTN)map;
  handoff->memmap_desc_size = (UINT32)desc_sz;
  handoff->memmap_entries = (UINT32)(map_sz / desc_sz);
  handoff->memmap_size = (UINT64)map_sz;
  handoff->memmap_capacity = (UINT64)(pages << 12);
  handoff->efi_system_table = (UINT64)(UINTN)systab;
  handoff->efi_image_handle = (UINT64)(UINTN)image;
  handoff->efi_map_key = (UINT64)map_key;
  handoff->runtime_flags = 0;
  handoff->efi_block_io = 0;
  handoff->efi_disk_last_lba = 0;
  handoff->data_lba_start = 0;
  handoff->data_lba_count = 0;
  handoff->efi_block_size = 0;
  handoff->efi_media_id = 0;
  handoff->efi_block_io_raw = 0;
  handoff->efi_disk_last_lba_raw = 0;
  handoff->data_lba_start_raw = 0;
  handoff->data_lba_count_raw = 0;
  handoff->efi_media_id_raw = 0;

  dbgcon_putc('J');
  __asm__ __volatile__(
      "mov %0, %%rdi\n\t"
      "jmp *%1\n\t"
      :
      : "r"(handoff), "r"((UINTN)entry)
      : "rdi", "memory");
  __builtin_unreachable();

  for (;;) {
    __asm__ __volatile__("hlt");
  }
}
