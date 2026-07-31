#include "internal/uefi_loader_internal.h"

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

EFI_STATUS load_kernel_streaming(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
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

static int uefi_ab_gpt_read(
    void *opaque, uint32_t lba,
    uint8_t sector[CAPYOS_GPT_IDENTITY_SECTOR_SIZE]) {
  struct uefi_boot_store_context *ctx = opaque;
  if (!ctx || !ctx->bio || !sector)
    return -1;
  return EFI_ERROR(uefi_block_io_read(ctx->bio, ctx->media_id, lba,
                                      CAPYOS_GPT_IDENTITY_SECTOR_SIZE,
                                      sector))
             ? -1
             : 0;
}

static int uefi_ab_identity_matches(
    const struct capyos_gpt_identity *identity,
    const struct boot_disk_identity *expected) {
  return identity && expected &&
         expected->flags == BOOT_HANDOFF_DISK_IDENTITY_VALID &&
         expected->esp_lba_start == identity->esp.lba &&
         expected->esp_lba_count == identity->esp.sectors &&
         expected->boot_lba_start == identity->boot.lba &&
         expected->boot_lba_count == identity->boot.sectors &&
         expected->data_lba_start == identity->data.lba &&
         expected->data_lba_count == identity->data.sectors &&
         uefi_slot_bytes_equal(expected->disk_guid, identity->disk_guid, 16u) &&
         uefi_slot_bytes_equal(expected->esp_partition_guid, identity->esp.guid,
                               16u) &&
         uefi_slot_bytes_equal(expected->boot_partition_guid,
                               identity->boot.guid, 16u) &&
         uefi_slot_bytes_equal(expected->data_partition_guid,
                               identity->data.guid, 16u);
}

int uefi_boot_store_read(void *opaque, uint32_t lba,
                                uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE]) {
  struct uefi_boot_store_context *ctx = opaque;
  if (!ctx || !ctx->bio || !sector || lba >= ctx->boot_sectors ||
      ctx->boot_lba > UINT64_MAX - lba)
    return -1;
  return EFI_ERROR(uefi_block_io_read(ctx->bio, ctx->media_id,
                                      ctx->boot_lba + lba,
                                      BOOT_SLOT_STORE_SECTOR_SIZE, sector))
             ? -1
             : 0;
}

int uefi_boot_store_write(
    void *opaque, uint32_t lba,
    const uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE]) {
  struct uefi_boot_store_context *ctx = opaque;
  if (!ctx || !ctx->bio || !sector || lba >= ctx->boot_sectors ||
      ctx->boot_lba > UINT64_MAX - lba)
    return -1;
  return EFI_ERROR(uefi_block_io_write(ctx->bio, ctx->media_id,
                                       ctx->boot_lba + lba,
                                       BOOT_SLOT_STORE_SECTOR_SIZE, sector))
             ? -1
             : 0;
}

int uefi_boot_store_flush(void *opaque) {
  struct uefi_boot_store_context *ctx = opaque;
  return !ctx || !ctx->bio ||
                 EFI_ERROR(uefi_block_io_flush(ctx->bio, ctx->media_id))
             ? -1
             : 0;
}

int uefi_slot_bytes_equal(const UINT8 *a, const UINT8 *b, UINTN len) {
  UINT8 diff = 0u;
  if (!a || !b)
    return 0;
  for (UINTN i = 0u; i < len; ++i)
    diff |= (UINT8)(a[i] ^ b[i]);
  return diff == 0u;
}

static EFI_STATUS uefi_load_selected_slot(
    EFI_SYSTEM_TABLE *st, struct uefi_boot_store_context *ctx,
    struct boot_slot_store *store, uint32_t slot,
    EFI_PHYSICAL_ADDRESS *entry_out) {
  struct boot_slot_manager manager;
  struct boot_slot_image image;
  struct sha256_ctx sha;
  UINT8 digest[SHA256_DIGEST_SIZE];
  VOID *allocation = NULL;
  VOID *buffer = NULL;
  UINTN rounded;
  UINT32 sectors;
  EFI_STATUS status;
  if (!st || !ctx || !store || !entry_out || slot >= BOOT_SLOT_COUNT ||
      boot_slot_manager_get(&manager) != 0 || manager.active_slot != slot ||
      manager.slots[slot].state != BOOT_SLOT_ACTIVE ||
      boot_slot_store_read_header(store, 0u, slot, &image) != 0 ||
      manager.slots[slot].payload_size != image.payload_size ||
      !uefi_slot_bytes_equal((const UINT8 *)manager.slots[slot].version,
                             (const UINT8 *)image.version,
                             BOOT_SLOT_VERSION_MAX) ||
      !uefi_slot_bytes_equal(manager.slots[slot].payload_sha256,
                             image.payload_sha256, BOOT_SLOT_SHA256_SIZE))
    return EFI_COMPROMISED_DATA;
  sectors = image.payload_size / BOOT_SLOT_STORE_SECTOR_SIZE;
  if ((image.payload_size % BOOT_SLOT_STORE_SECTOR_SIZE) != 0u)
    sectors++;
  if (sectors == 0u ||
      sectors > manager.slots[slot].payload_capacity_sectors ||
      sectors > (~(UINTN)0 / BOOT_SLOT_STORE_SECTOR_SIZE))
    return EFI_COMPROMISED_DATA;
  rounded = (UINTN)sectors * BOOT_SLOT_STORE_SECTOR_SIZE;
  status = uefi_block_io_allocate_aligned(ctx->bio, rounded, &allocation,
                                          &buffer);
  if (EFI_ERROR(status))
    return status;
  /* Headless phase markers: block read, padding validation and payload hash.
   * They use debugcon only and do not alter the BootServices memory map. */
  dbgcon_putc('R');
  status = uefi_block_io_read(
      ctx->bio, ctx->media_id,
      ctx->boot_lba + manager.slots[slot].payload_lba, rounded, buffer);
  if (EFI_ERROR(status)) {
    FreePool(allocation);
    return status;
  }
  dbgcon_putc('r');
  for (UINTN i = image.payload_size; i < rounded; ++i) {
    if (((UINT8 *)buffer)[i] != 0u) {
      FreePool(allocation);
      return EFI_COMPROMISED_DATA;
    }
  }
  dbgcon_putc('p');
  sha256_init(&sha);
  sha256_update(&sha, buffer, image.payload_size);
  sha256_final(&sha, digest);
  sha256_clear(&sha);
  dbgcon_putc('v');
  if (!uefi_slot_bytes_equal(digest, image.payload_sha256,
                             SHA256_DIGEST_SIZE)) {
    dbgcon_putc('!');
    Print(L"[UEFI] Integridade do kernel A/B falhou: SHA-256 divergente; "
          L"boot recusado\r\n");
    FreePool(allocation);
    return EFI_SECURITY_VIOLATION;
  }
  status = load_kernel_from_buffer(st, buffer, image.payload_size, entry_out);
  FreePool(allocation);
  return status;
}

EFI_STATUS load_kernel_from_ab_store(
    EFI_SYSTEM_TABLE *st, EFI_BLOCK_IO_PROTOCOL *bio,
    UINT32 expected_media_id,
    const struct boot_disk_identity *identity, EFI_PHYSICAL_ADDRESS *entry_out,
    struct boot_slot_attempt_handoff *out_attempt) {
  static struct uefi_boot_store_context ctx;
  static struct boot_slot_store store;
  struct boot_slot_layout layout;
  uint32_t slot = BOOT_SLOT_NONE;
  uint64_t generation = 0u;
  int selection;
  EFI_STATUS status;
  if (out_attempt)
    *out_attempt = (struct boot_slot_attempt_handoff){0};
  if (!st || !bio || !bio->Media || !identity || !entry_out || !out_attempt ||
      bio->Media->BlockSize != BOOT_SLOT_STORE_SECTOR_SIZE ||
      identity->flags != BOOT_HANDOFF_DISK_IDENTITY_VALID ||
      identity->boot_lba_count == 0u || identity->boot_lba_count > UINT32_MAX ||
      identity->boot_lba_start > bio->Media->LastBlock ||
      identity->boot_lba_count - 1u >
          bio->Media->LastBlock - identity->boot_lba_start)
    return EFI_INVALID_PARAMETER;
  ctx.bio = bio;
  if (bio->Media->MediaId != expected_media_id)
    return EFI_MEDIA_CHANGED;
  ctx.media_id = expected_media_id;
  ctx.boot_lba = identity->boot_lba_start;
  ctx.boot_sectors = (UINT32)identity->boot_lba_count;
  {
    struct capyos_gpt_identity current_identity;
    if (bio->Media->LastBlock >= UINT32_MAX ||
        capyos_gpt_identity_read(uefi_ab_gpt_read, &ctx,
                                 bio->Media->BlockSize,
                                 (UINT32)bio->Media->LastBlock + 1u,
                                 &current_identity) != 0 ||
        !uefi_ab_identity_matches(&current_identity, identity))
      return EFI_MEDIA_CHANGED;
  }
  if (boot_slot_layout_plan(ctx.boot_sectors, &layout) != 0 ||
      boot_slot_init() != 0 ||
      boot_slot_store_init(&store, &layout, uefi_boot_store_read,
                           uefi_boot_store_write, uefi_boot_store_flush,
                           &ctx) != 0 ||
      boot_slot_store_bind_control(&store, 0u) != 0)
    return EFI_COMPROMISED_DATA;
  selection = boot_slot_select_for_boot(&slot, &generation);
  if (selection < 0 || slot >= BOOT_SLOT_COUNT || generation == 0u)
    return EFI_COMPROMISED_DATA;
  status = uefi_load_selected_slot(st, &ctx, &store, slot, entry_out);
  if (EFI_ERROR(status) && selection == 1) {
    selection = boot_slot_select_for_boot(&slot, &generation);
    if (selection != 2 || slot >= BOOT_SLOT_COUNT || generation == 0u)
      return EFI_COMPROMISED_DATA;
    status = uefi_load_selected_slot(st, &ctx, &store, slot, entry_out);
  }
  if (EFI_ERROR(status))
    return status;
  out_attempt->flags = BOOT_HANDOFF_SLOT_ATTEMPT_VALID;
  if (selection == 1)
    out_attempt->flags |= BOOT_HANDOFF_SLOT_ATTEMPT_PENDING;
  else if (selection == 2)
    out_attempt->flags |= BOOT_HANDOFF_SLOT_ATTEMPT_ROLLBACK;
  out_attempt->slot = slot;
  out_attempt->generation = generation;
  return EFI_SUCCESS;
}

