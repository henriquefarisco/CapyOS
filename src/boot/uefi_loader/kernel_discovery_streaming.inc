static EFI_STATUS try_manifest_from_gpt(EFI_BLOCK_IO_PROTOCOL *bio,
                                        struct boot_manifest **out_mf,
                                        UINTN *out_size, UINT32 *out_block_size,
                                        UINT64 *out_part_lba) {
  if (!bio || !bio->Media || !out_mf || !out_size || !out_block_size ||
      !out_part_lba)
    return EFI_INVALID_PARAMETER;
  *out_mf = NULL;
  *out_size = 0;
  *out_part_lba = 0;
  UINT32 bsz = bio->Media->BlockSize;
  *out_block_size = bsz;

  VOID *hdr_buf = AllocatePool(bsz);
  if (!hdr_buf)
    return EFI_OUT_OF_RESOURCES;
  EFI_STATUS st =
      uefi_call_wrapper(bio->ReadBlocks, 5, bio, bio->Media->MediaId,
                        GPT_HEADER_LBA, bsz, hdr_buf);
  if (EFI_ERROR(st)) {
    FreePool(hdr_buf);
    return st;
  }
  gpt_header_t *hdr = (gpt_header_t *)hdr_buf;
  if (hdr->signature != GPT_SIG) {
    FreePool(hdr_buf);
    return EFI_NOT_FOUND;
  }
  UINT32 entsz = hdr->part_entry_size;
  UINT32 entcnt = hdr->num_part_entries;
  UINT64 ent_lba = hdr->part_entry_lba;
  FreePool(hdr_buf);
  if (entsz < sizeof(gpt_entry_t) || entcnt == 0)
    return EFI_NOT_FOUND;

  UINT8 esp_guid[16] = EFI_PART_TYPE_ESP;
  UINT8 boot_guid[16] = EFI_PART_TYPE_CAPYOS_BOOT;

  UINTN ents_per_block = bsz / entsz;
  UINT64 cur_lba = ent_lba;
  UINTN read_entries = 0;
  VOID *ent_buf = AllocatePool(bsz);
  if (!ent_buf)
    return EFI_OUT_OF_RESOURCES;

  while (read_entries < entcnt) {
    st = uefi_call_wrapper(bio->ReadBlocks, 5, bio, bio->Media->MediaId,
                           cur_lba, bsz, ent_buf);
    if (EFI_ERROR(st)) {
      FreePool(ent_buf);
      return st;
    }
    UINTN max_in_block = (entcnt - read_entries) < ents_per_block
                             ? (entcnt - read_entries)
                             : ents_per_block;
    for (UINTN i = 0; i < max_in_block; i++) {
      UINT8 *ptr = (UINT8 *)ent_buf + i * entsz;
      gpt_entry_t *e = (gpt_entry_t *)ptr;
      if (e->first_lba == 0 || e->last_lba == 0)
        continue;
      if (guid_eq(e->part_type_guid, esp_guid))
        continue; // pular ESP
      if (!guid_eq(e->part_type_guid, boot_guid))
        continue; // sÃƒÂ³ BOOT
      UINT64 start_lba = e->first_lba;
      VOID *mf_buf = AllocatePool(bsz);
      if (!mf_buf) {
        FreePool(ent_buf);
        return EFI_OUT_OF_RESOURCES;
      }
      EFI_STATUS st2 = uefi_call_wrapper(
          bio->ReadBlocks, 5, bio, bio->Media->MediaId, start_lba, bsz, mf_buf);
      if (EFI_ERROR(st2)) {
        FreePool(mf_buf);
        continue;
      }
      struct boot_manifest *mf = (struct boot_manifest *)mf_buf;
      if (mf->magic == BOOT_MANIFEST_MAGIC && mf->entry_count > 0) {
        *out_mf = mf;
        *out_size = bsz;
        *out_part_lba = start_lba;
        FreePool(ent_buf);
        return EFI_SUCCESS;
      }
      FreePool(mf_buf);
    }
    read_entries += max_in_block;
    cur_lba++;
  }
  FreePool(ent_buf);
  return EFI_NOT_FOUND;
}

static EFI_STATUS load_kernel(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
                              EFI_PHYSICAL_ADDRESS *entry_out) {
  EFI_STATUS stt;
  EFI_LOADED_IMAGE *li = NULL;
  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, image,
                          &LoadedImageProtocol, (VOID **)&li);
  if (EFI_ERROR(stt) || li == NULL)
    return stt;

  EFI_HANDLE fs_handle = li->DeviceHandle;

  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, fs_handle,
                          &FileSystemProtocol, (VOID **)&sfs);
  if (EFI_ERROR(stt) || sfs == NULL) {
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    EFI_STATUS lh =
        uefi_call_wrapper(st->BootServices->LocateHandleBuffer, 5, ByProtocol,
                          &FileSystemProtocol, NULL, &count, &handles);
    if (!EFI_ERROR(lh) && handles) {
      for (UINTN i = 0; i < count; i++) {
        fs_handle = handles[i];
        sfs = NULL;
        stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, fs_handle,
                                &FileSystemProtocol, (VOID **)&sfs);
        if (!EFI_ERROR(stt) && sfs)
          break;
      }
      FreePool(handles);
    }
  }
  if (EFI_ERROR(stt) || sfs == NULL)
    return stt;

  EFI_FILE_HANDLE root = NULL;
  stt = uefi_call_wrapper(sfs->OpenVolume, 2, sfs, &root);
  if (EFI_ERROR(stt) || root == NULL)
    return stt;

  (void)load_boot_config_from_root(root);

  // Tentar manifest first
  VOID *manifest_buf = NULL;
  UINTN manifest_size = 0;
  struct boot_manifest *mf = NULL;
  EFI_BLOCK_IO_PROTOCOL *bio = NULL;
  UINT32 block_sz = 0;
  UINT64 boot_part_lba = 0;

  // GPT BOOT partition
  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, fs_handle,
                          &BlockIoProtocol, (VOID **)&bio);
  if (!EFI_ERROR(stt) && bio && bio->Media) {
    try_manifest_from_gpt(bio, &mf, &manifest_size, &block_sz, &boot_part_lba);
  }
  // FAT file fallback
  if (!mf) {
    EFI_STATUS mf_st =
        read_file(root, L"BOOT\\MANIFEST.BIN", &manifest_buf, &manifest_size);
    if (EFI_ERROR(mf_st) && manifest_buf) {
      FreePool(manifest_buf);
      manifest_buf = NULL;
      manifest_size = 0;
    }
    if (EFI_ERROR(mf_st))
      mf_st = read_file(root, L"\\BOOT\\MANIFEST.BIN", &manifest_buf,
                        &manifest_size);
    if (EFI_ERROR(mf_st) && manifest_buf) {
      FreePool(manifest_buf);
      manifest_buf = NULL;
      manifest_size = 0;
    }
    if (EFI_ERROR(mf_st))
      mf_st = read_file(root, L"\\boot\\manifest.bin", &manifest_buf,
                        &manifest_size);
    if (EFI_ERROR(mf_st) && manifest_buf) {
      FreePool(manifest_buf);
      manifest_buf = NULL;
      manifest_size = 0;
    }
    if (EFI_ERROR(mf_st))
      mf_st =
          read_file(root, L"boot\\manifest.bin", &manifest_buf, &manifest_size);
    if (!EFI_ERROR(mf_st) && manifest_size >= sizeof(struct boot_manifest)) {
      mf = (struct boot_manifest *)manifest_buf;
    }
  }
  // Usar manifest se vÃƒÂ¡lido
  if (boot_part_lba != 0 && mf && mf->magic == BOOT_MANIFEST_MAGIC &&
      mf->entry_count > 0 && bio && bio->Media) {
    if (block_sz == 0)
      block_sz = bio->Media->BlockSize;
    struct boot_manifest_entry *sel = NULL;
    for (UINT32 i = 0; i < mf->entry_count && i < 4; i++) {
      if (mf->entries[i].type == BOOT_ENTRY_NORMAL) {
        sel = &mf->entries[i];
        break;
      }
    }
    if (!sel)
      sel = &mf->entries[0];
    UINT64 total_bytes = (UINT64)sel->sector_count * block_sz;
    VOID *kernel_buf = AllocatePool(total_bytes);
    if (kernel_buf) {
      UINT64 lba = sel->lba_start;
      if (boot_part_lba != 0) {
        lba += boot_part_lba; // lba relativo ÃƒÂ  partiÃƒÂ§ÃƒÂ£o BOOT
      }
      EFI_STATUS rb =
          uefi_call_wrapper(bio->ReadBlocks, 5, bio, bio->Media->MediaId, lba,
                            total_bytes, kernel_buf);
      if (!EFI_ERROR(rb)) {
        Print(L"[UEFI] Kernel raw lba=%lu bytes=%lu buf=0x%lx\r\n", lba,
              total_bytes, (UINT64)(UINTN)kernel_buf);
        EFI_STATUS lkst = load_kernel_from_buffer(
            st, kernel_buf, (UINTN)total_bytes, entry_out);
        FreePool(kernel_buf);
        if (manifest_buf)
          FreePool(manifest_buf);
        return lkst;
      }
      FreePool(kernel_buf);
    }
  }
  if (manifest_buf)
    FreePool(manifest_buf);

  // Fallback: caminho fixo
  VOID *kernel_buf = NULL;
  UINTN kernel_size = 0;
  stt = read_file(root, L"BOOT\\CAPYOS64.BIN", &kernel_buf, &kernel_size);
  if (EFI_ERROR(stt) && kernel_buf) {
    FreePool(kernel_buf);
    kernel_buf = NULL;
    kernel_size = 0;
  }
  if (EFI_ERROR(stt))
    stt = read_file(root, L"\\BOOT\\CAPYOS64.BIN", &kernel_buf, &kernel_size);
  if (EFI_ERROR(stt) && kernel_buf) {
    FreePool(kernel_buf);
    kernel_buf = NULL;
    kernel_size = 0;
  }
  if (EFI_ERROR(stt))
    stt = read_file(root, L"\\boot\\capyos64.bin", &kernel_buf, &kernel_size);
  if (EFI_ERROR(stt) && kernel_buf) {
    FreePool(kernel_buf);
    kernel_buf = NULL;
    kernel_size = 0;
  }
  if (EFI_ERROR(stt))
    stt = read_file(root, L"boot\\capyos64.bin", &kernel_buf, &kernel_size);
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] Falha ao ler kernel: %r\r\n", stt);
    return stt;
  }
  EFI_STATUS lkst =
      load_kernel_from_buffer(st, kernel_buf, kernel_size, entry_out);
  FreePool(kernel_buf);
  return lkst;
}

static EFI_STATUS load_kernel_streaming(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
                                        EFI_PHYSICAL_ADDRESS *entry_out) {
  EFI_STATUS stt;
  EFI_LOADED_IMAGE *li = NULL;
  EFI_FILE_HANDLE root = NULL;
  VOID *manifest_buf = NULL;
  UINTN manifest_size = 0;
  struct boot_manifest *mf = NULL;
  EFI_BLOCK_IO_PROTOCOL *bio = NULL;
  UINT32 block_sz = 0;
  UINT64 boot_part_lba = 0;
  EFI_FILE_HANDLE kernel_fh = NULL;
  UINTN kernel_size = 0;

  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, image,
                          &LoadedImageProtocol, (VOID **)&li);
  if (EFI_ERROR(stt) || li == NULL)
    return stt;

  EFI_HANDLE fs_handle = li->DeviceHandle;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, fs_handle,
                          &FileSystemProtocol, (VOID **)&sfs);
  if (EFI_ERROR(stt) || sfs == NULL) {
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    EFI_STATUS lh =
        uefi_call_wrapper(st->BootServices->LocateHandleBuffer, 5, ByProtocol,
                          &FileSystemProtocol, NULL, &count, &handles);
    if (!EFI_ERROR(lh) && handles) {
      for (UINTN i = 0; i < count; i++) {
        fs_handle = handles[i];
        sfs = NULL;
        stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, fs_handle,
                                &FileSystemProtocol, (VOID **)&sfs);
        if (!EFI_ERROR(stt) && sfs)
          break;
      }
      FreePool(handles);
    }
  }
  if (EFI_ERROR(stt) || sfs == NULL)
    return stt;

  stt = uefi_call_wrapper(sfs->OpenVolume, 2, sfs, &root);
  if (EFI_ERROR(stt) || root == NULL)
    return stt;

  (void)load_boot_config_from_root(root);

  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, fs_handle,
                          &BlockIoProtocol, (VOID **)&bio);
  if (!EFI_ERROR(stt) && bio && bio->Media) {
    (void)try_manifest_from_gpt(bio, &mf, &manifest_size, &block_sz,
                                &boot_part_lba);
  }

  if (!mf) {
    EFI_STATUS mf_st =
        read_file(root, L"BOOT\\MANIFEST.BIN", &manifest_buf, &manifest_size);
    if (EFI_ERROR(mf_st) && manifest_buf) {
      FreePool(manifest_buf);
      manifest_buf = NULL;
      manifest_size = 0;
    }
    if (EFI_ERROR(mf_st))
      mf_st = read_file(root, L"\\BOOT\\MANIFEST.BIN", &manifest_buf,
                        &manifest_size);
    if (EFI_ERROR(mf_st) && manifest_buf) {
      FreePool(manifest_buf);
      manifest_buf = NULL;
      manifest_size = 0;
    }
    if (EFI_ERROR(mf_st))
      mf_st = read_file(root, L"\\boot\\manifest.bin", &manifest_buf,
                        &manifest_size);
    if (EFI_ERROR(mf_st) && manifest_buf) {
      FreePool(manifest_buf);
      manifest_buf = NULL;
      manifest_size = 0;
    }
    if (EFI_ERROR(mf_st))
      mf_st =
          read_file(root, L"boot\\manifest.bin", &manifest_buf, &manifest_size);
    if (!EFI_ERROR(mf_st) && manifest_size >= sizeof(struct boot_manifest)) {
      mf = (struct boot_manifest *)manifest_buf;
    }
  }

  if (boot_part_lba != 0 && mf && mf->magic == BOOT_MANIFEST_MAGIC &&
      mf->entry_count > 0 && bio && bio->Media) {
    if (block_sz == 0)
      block_sz = bio->Media->BlockSize;

    struct boot_manifest_entry *sel = NULL;
    for (UINT32 i = 0; i < mf->entry_count && i < 4; i++) {
      if (mf->entries[i].type == BOOT_ENTRY_NORMAL) {
        sel = &mf->entries[i];
        break;
      }
    }
    if (!sel)
      sel = &mf->entries[0];

    UINT64 total_bytes = (UINT64)sel->sector_count * block_sz;
    UINT64 lba = sel->lba_start;
    if (boot_part_lba != 0) {
      lba += boot_part_lba;
    }
    Print(L"[UEFI] Kernel raw lba=%lu bytes=%lu\r\n", lba, total_bytes);

    struct kernel_block_reader block_reader = {
        .bio = bio,
        .base_lba = lba,
        .block_size = block_sz,
        .size = total_bytes,
    };
    EFI_STATUS lkst = load_kernel_from_reader(
        st, kernel_read_from_blocks, &block_reader, total_bytes, entry_out);
    if (manifest_buf)
      FreePool(manifest_buf);
    uefi_call_wrapper(root->Close, 1, root);
    return lkst;
  }

  if (manifest_buf)
    FreePool(manifest_buf);

  stt = open_file_read(root, L"BOOT\\CAPYOS64.BIN", &kernel_fh, &kernel_size);
  if (EFI_ERROR(stt))
    stt = open_file_read(root, L"\\BOOT\\CAPYOS64.BIN", &kernel_fh,
                         &kernel_size);
  if (EFI_ERROR(stt))
    stt = open_file_read(root, L"\\boot\\capyos64.bin", &kernel_fh,
                         &kernel_size);
  if (EFI_ERROR(stt))
    stt = open_file_read(root, L"boot\\capyos64.bin", &kernel_fh,
                         &kernel_size);
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] Falha ao ler kernel: %r\r\n", stt);
    uefi_call_wrapper(root->Close, 1, root);
    return stt;
  }

  struct kernel_file_reader file_reader = {
      .file = kernel_fh,
      .size = kernel_size,
  };
  EFI_STATUS lkst =
      load_kernel_from_reader(st, kernel_read_from_file, &file_reader,
                              kernel_size, entry_out);
  uefi_call_wrapper(kernel_fh->Close, 1, kernel_fh);
  uefi_call_wrapper(root->Close, 1, root);
  return lkst;
}

// --- UEFI "installer" mode -------------------------------------------------

#define INSTALL_ALIGN_LBA 2048ULL // 1 MiB
#define INSTALL_ESP_SIZE_MIB 512ULL
#define INSTALL_BOOT_SIZE_MIB 256ULL

#define GPT_REVISION 0x00010000U
#define GPT_HEADER_SIZE 92U
#define GPT_NUM_ENTRIES 128U
#define GPT_ENTRY_SIZE 128U
#define GPT_ENTRIES_LBA 2ULL
#define GPT_ENTRIES_SECTORS ((GPT_NUM_ENTRIES * GPT_ENTRY_SIZE) / 512U) // 32

typedef enum {
  INSTALLER_LANG_EN = 0,
  INSTALLER_LANG_PT_BR = 1,
  INSTALLER_LANG_ES = 2,
} installer_language_t;

