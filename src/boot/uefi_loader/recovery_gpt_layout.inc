static void disable_uefi_watchdog(EFI_SYSTEM_TABLE *st) {
  if (!st || !st->BootServices || !st->BootServices->SetWatchdogTimer) {
    return;
  }
  (void)uefi_call_wrapper(st->BootServices->SetWatchdogTimer, 4, 0, 0, 0, NULL);
}

/* UEFI readline with optional password masking */
static UINTN uefi_readline(EFI_SYSTEM_TABLE *st, CHAR16 *buf, UINTN maxlen,
                           BOOLEAN mask) {
  if (!st || !st->ConIn || !buf || maxlen < 2)
    return 0;
  UINTN len = 0;
  buf[0] = 0;

  for (;;) {
    UINTN idx = 0;
    uefi_call_wrapper(st->BootServices->WaitForEvent, 3, 1,
                      &st->ConIn->WaitForKey, &idx);
    EFI_INPUT_KEY key;
    EFI_STATUS stt =
        uefi_call_wrapper(st->ConIn->ReadKeyStroke, 2, st->ConIn, &key);
    if (EFI_ERROR(stt))
      continue;

    /* Enter - finish */
    if (key.UnicodeChar == L'\r' || key.UnicodeChar == L'\n') {
      buf[len] = 0;
      Print(L"\r\n");
      return len;
    }

    /* Backspace */
    if (key.UnicodeChar == 0x08 || key.ScanCode == 0x08) {
      if (len > 0) {
        len--;
        buf[len] = 0;
        Print(L"\b \b"); /* Erase character */
      }
      continue;
    }

    /* Printable character */
    if (key.UnicodeChar >= 0x20 && len + 1 < maxlen) {
      buf[len++] = key.UnicodeChar;
      buf[len] = 0;
      if (mask) {
        Print(L"*");
      } else {
        Print(L"%c", key.UnicodeChar);
      }
    }
  }
}

/* Generate random recovery key shown during ISO install. */
static void generate_recovery_key(EFI_SYSTEM_TABLE *st, CHAR16 *key_out,
                                  UINTN key_len) {
  if (!key_out || key_len < 8) {
    return;
  }

  UINTN groups = 6; /* 24 hex chars => 96 bits */
  const UINTN chars_per_group = 4;
  UINTN max_chars = key_len - 1;
  while (groups > 1 &&
         (groups * chars_per_group + (groups - 1)) > max_chars) {
    --groups;
  }
  UINTN nibble_count = groups * chars_per_group;
  UINTN rnd_need = (nibble_count + 1) / 2;

  UINT8 rnd[32];
  for (UINTN i = 0; i < sizeof(rnd); ++i) {
    rnd[i] = 0;
  }

  BOOLEAN have_rng = FALSE;
  if (st && st->BootServices) {
    EFI_GUID rng_guid = EFI_RNG_PROTOCOL_GUID;
    EFI_RNG_PROTOCOL *rng = NULL;
    EFI_STATUS rng_st = uefi_call_wrapper(st->BootServices->LocateProtocol, 3,
                                          &rng_guid, NULL, (VOID **)&rng);
    if (!EFI_ERROR(rng_st) && rng && rng->GetRNG) {
      rng_st = uefi_call_wrapper(rng->GetRNG, 4, rng, NULL, rnd_need, rnd);
      if (!EFI_ERROR(rng_st)) {
        have_rng = TRUE;
      }
    }
  }

  if (!have_rng) {
    UINT64 tsc = 0;
    __asm__ volatile("rdtsc" : "=A"(tsc));
    UINT64 seed = tsc ^ (UINT64)(UINTN)st ^ (UINT64)(UINTN)key_out ^
                  ((UINT64)key_len << 17);
    for (UINTN i = 0; i < rnd_need; ++i) {
      seed ^= seed << 13;
      seed ^= seed >> 7;
      seed ^= seed << 17;
      seed ^= ((UINT64)(UINTN)&seed >> (i & 7U));
      rnd[i] = (UINT8)(seed & 0xFF);
    }
  }

  const CHAR16 *hex = L"0123456789ABCDEF";
  UINTN pos = 0;
  UINTN nib = 0;
  for (UINTN g = 0; g < groups && pos + 1 < key_len; ++g) {
    if (g > 0 && pos + 1 < key_len) {
      key_out[pos++] = L'-';
    }
    for (UINTN j = 0; j < chars_per_group && pos + 1 < key_len; ++j, ++nib) {
      UINT8 b = rnd[nib / 2];
      UINT8 v = (nib & 1U) ? (b & 0x0F) : ((b >> 4) & 0x0F);
      key_out[pos++] = hex[v];
    }
  }
  key_out[pos] = 0;

  for (UINTN i = 0; i < sizeof(rnd); ++i) {
    rnd[i] = 0;
  }
}

static EFI_STATUS wipe_blocks(EFI_BLOCK_IO_PROTOCOL *bio, UINT64 start_lba,
                              UINT64 sectors) {
  if (!bio || !bio->Media)
    return EFI_INVALID_PARAMETER;
  UINTN bsz = bio->Media->BlockSize;
  if (bsz == 0)
    return EFI_INVALID_PARAMETER;
  if (sectors == 0)
    return EFI_SUCCESS;
  if (start_lba > bio->Media->LastBlock)
    return EFI_INVALID_PARAMETER;
  UINT64 max = (UINT64)bio->Media->LastBlock - start_lba + 1ULL;
  if (sectors > max)
    sectors = max;

  const UINTN chunk_sectors = 128;
  UINTN chunk_bytes = chunk_sectors * bsz;
  VOID *buf = AllocatePool(chunk_bytes);
  if (!buf)
    return EFI_OUT_OF_RESOURCES;
  for (UINTN i = 0; i < chunk_bytes; i++)
    ((UINT8 *)buf)[i] = 0;

  EFI_STATUS stt = EFI_SUCCESS;
  UINT64 done = 0;
  while (done < sectors) {
    UINTN nsec = (UINTN)((sectors - done) > chunk_sectors ? chunk_sectors
                                                          : (sectors - done));
    UINTN nbytes = nsec * bsz;
    stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                            start_lba + done, nbytes, buf);
    if (EFI_ERROR(stt))
      break;
    done += nsec;
  }
  FreePool(buf);
  if (!EFI_ERROR(stt)) {
    uefi_call_wrapper(bio->FlushBlocks, 1, bio);
  }
  return stt;
}

/*
 * Prepare DATA partition for first boot:
 * - clear head/tail regions
 * - clear a middle chunk
 *
 * This prevents stale bytes from previous installations from being interpreted
 * as an existing encrypted volume.
 */
static EFI_STATUS scrub_data_partition_for_first_boot(
    EFI_BLOCK_IO_PROTOCOL *bio, UINT64 data_lba, UINT64 data_sectors) {
  if (!bio || !bio->Media || data_sectors == 0) {
    return EFI_INVALID_PARAMETER;
  }

  const UINT64 probe_block_bytes = 4096ULL;
  UINT64 probe_span_secs =
      (probe_block_bytes + (UINT64)bio->Media->BlockSize - 1ULL) /
      (UINT64)bio->Media->BlockSize;
  const UINT64 edge_span = 4096ULL; /* 2 MiB with 512-byte sectors */
  const UINT64 mid_span =
      (probe_span_secs * 8ULL > 16ULL) ? (probe_span_secs * 8ULL) : 16ULL;
  UINT64 head_secs = (data_sectors < edge_span) ? data_sectors : edge_span;
  EFI_STATUS st = wipe_blocks(bio, data_lba, head_secs);
  if (EFI_ERROR(st)) {
    return st;
  }

  if (data_sectors > head_secs) {
    UINT64 tail_start = data_lba + data_sectors - head_secs;
    st = wipe_blocks(bio, tail_start, head_secs);
    if (EFI_ERROR(st)) {
      return st;
    }
  }

  if (data_sectors > mid_span + 2ULL) {
    UINT64 mid_rel = data_sectors / 2ULL;
    if (mid_rel + mid_span >= data_sectors) {
      mid_rel = data_sectors - mid_span;
    }
    if (probe_span_secs > 1ULL) {
      mid_rel -= (mid_rel % probe_span_secs);
      if (mid_rel + mid_span > data_sectors) {
        mid_rel = (data_sectors > mid_span) ? (data_sectors - mid_span) : 0;
        mid_rel -= (mid_rel % probe_span_secs);
      }
    }
    st = wipe_blocks(bio, data_lba + mid_rel, mid_span);
    if (EFI_ERROR(st)) {
      return st;
    }
  }

  return EFI_SUCCESS;
}

static VOID fill_guid(UINT8 out[16], EFI_SYSTEM_TABLE *st) {
  // Pseudo-random: time + pointer mix. Good enough for VM install IDs.
  for (UINTN i = 0; i < 16; i++)
    out[i] = 0;
  if (!st || !st->RuntimeServices)
    return;
  EFI_TIME t;
  EFI_STATUS stt = uefi_call_wrapper(st->RuntimeServices->GetTime, 2, &t, NULL);
  if (EFI_ERROR(stt))
    return;
  UINT64 seed = ((UINT64)t.Year << 48) ^ ((UINT64)t.Month << 40) ^
                ((UINT64)t.Day << 32) ^ ((UINT64)t.Hour << 24) ^
                ((UINT64)t.Minute << 16) ^ ((UINT64)t.Second << 8) ^
                (UINT64)(UINTN)&t;
  for (UINTN i = 0; i < 16; i++) {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    out[i] = (UINT8)(seed & 0xFF);
  }
  // Ensure it's not all-zero.
  out[0] |= 1;
}

static VOID gpt_set_name(UINT8 name_bytes[72], const CHAR16 *s) {
  for (UINTN i = 0; i < 72; i++)
    name_bytes[i] = 0;
  if (!s)
    return;
  for (UINTN i = 0; i < 36 && s[i]; i++) {
    UINT16 ch = (UINT16)s[i];
    name_bytes[i * 2 + 0] = (UINT8)(ch & 0xFF);
    name_bytes[i * 2 + 1] = (UINT8)((ch >> 8) & 0xFF);
  }
}

static EFI_STATUS write_protective_mbr(EFI_BLOCK_IO_PROTOCOL *bio,
                                       UINT64 total_sectors) {
  if (!bio || !bio->Media)
    return EFI_INVALID_PARAMETER;
  UINT8 mbr[512];
  for (UINTN i = 0; i < sizeof(mbr); i++)
    mbr[i] = 0;

  // One 0xEE partition covering the whole disk (or max 0xFFFFFFFF sectors).
  UINT32 size32 = (total_sectors - 1ULL > 0xFFFFFFFFULL)
                      ? 0xFFFFFFFFU
                      : (UINT32)(total_sectors - 1ULL);
  UINTN off = 446;
  mbr[off + 0] = 0x00; // status
  mbr[off + 4] = 0xEE; // type
  // CHS fields can be 0xFF per spec for GPT protective.
  mbr[off + 1] = 0xFF;
  mbr[off + 2] = 0xFF;
  mbr[off + 3] = 0xFF;
  mbr[off + 5] = 0xFF;
  mbr[off + 6] = 0xFF;
  mbr[off + 7] = 0xFF;
  // LBA start = 1
  mbr[off + 8] = 0x01;
  mbr[off + 9] = 0x00;
  mbr[off + 10] = 0x00;
  mbr[off + 11] = 0x00;
  // size
  mbr[off + 12] = (UINT8)(size32 & 0xFF);
  mbr[off + 13] = (UINT8)((size32 >> 8) & 0xFF);
  mbr[off + 14] = (UINT8)((size32 >> 16) & 0xFF);
  mbr[off + 15] = (UINT8)((size32 >> 24) & 0xFF);
  mbr[510] = 0x55;
  mbr[511] = 0xAA;

  return uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId, 0,
                           512, mbr);
}

static EFI_STATUS gpt_write_layout(EFI_SYSTEM_TABLE *st,
                                   EFI_BLOCK_IO_PROTOCOL *bio, UINT64 esp_mib,
                                   UINT64 boot_mib, UINT64 *out_esp_lba,
                                   UINT64 *out_esp_sectors,
                                   UINT64 *out_boot_lba,
                                   UINT64 *out_boot_sectors,
                                   UINT64 *out_data_lba,
                                   UINT64 *out_data_sectors) {
  if (!st || !st->BootServices || !bio || !bio->Media)
    return EFI_INVALID_PARAMETER;
  if (bio->Media->BlockSize != 512)
    return EFI_UNSUPPORTED;

  UINT64 total_sectors = (UINT64)bio->Media->LastBlock + 1ULL;
  if (total_sectors < 65536)
    return EFI_INVALID_PARAMETER;

  UINT64 last_lba = total_sectors - 1ULL;
  UINT64 backup_entries_lba = last_lba - (UINT64)GPT_ENTRIES_SECTORS;
  UINT64 first_usable_lba = 34ULL;
  UINT64 last_usable_lba = backup_entries_lba - 1ULL;

  UINT64 esp_sectors = (esp_mib * 1024ULL * 1024ULL) / 512ULL;
  UINT64 boot_sectors = (boot_mib * 1024ULL * 1024ULL) / 512ULL;

  UINT64 esp_start = align_up_u64(2048ULL, INSTALL_ALIGN_LBA);
  UINT64 esp_end = esp_start + esp_sectors - 1ULL;
  UINT64 boot_start = align_up_u64(esp_end + 1ULL, INSTALL_ALIGN_LBA);
  UINT64 boot_end = boot_start + boot_sectors - 1ULL;
  UINT64 data_start = align_up_u64(boot_end + 1ULL, INSTALL_ALIGN_LBA);
  UINT64 data_end = last_usable_lba;

  if (esp_start < first_usable_lba)
    esp_start = align_up_u64(first_usable_lba, INSTALL_ALIGN_LBA);
  if (data_start <= boot_end)
    return EFI_INVALID_PARAMETER;
  if (data_end <= data_start)
    return EFI_INVALID_PARAMETER;
  if (boot_end >= last_usable_lba)
    return EFI_INVALID_PARAMETER;

  // Partition entries buffer (primary + backup use the same bytes).
  UINTN entries_bytes = (UINTN)GPT_NUM_ENTRIES * (UINTN)GPT_ENTRY_SIZE;
  UINT8 *entries = AllocatePool(entries_bytes);
  if (!entries)
    return EFI_OUT_OF_RESOURCES;
  for (UINTN i = 0; i < entries_bytes; i++)
    entries[i] = 0;

  gpt_entry_t *e = (gpt_entry_t *)entries;
  UINT8 esp_guid[16] = EFI_PART_TYPE_ESP;
  UINT8 boot_guid[16] = EFI_PART_TYPE_CAPYOS_BOOT;
  UINT8 linux_guid[16] = EFI_PART_TYPE_LINUX_FS;
  UINT8 disk_guid[16];
  fill_guid(disk_guid, st);

  // ESP
  for (UINTN i = 0; i < 16; i++)
    e[0].part_type_guid[i] = esp_guid[i];
  fill_guid(e[0].uniq_guid, st);
  e[0].first_lba = esp_start;
  e[0].last_lba = esp_end;
  e[0].attrs = 0;
  gpt_set_name((UINT8 *)e[0].name, L"ESP");

  // BOOT (CapyOS)
  for (UINTN i = 0; i < 16; i++)
    e[1].part_type_guid[i] = boot_guid[i];
  fill_guid(e[1].uniq_guid, st);
  e[1].first_lba = boot_start;
  e[1].last_lba = boot_end;
  e[1].attrs = 0;
  gpt_set_name((UINT8 *)e[1].name, L"BOOT");

  // DATA
  for (UINTN i = 0; i < 16; i++)
    e[2].part_type_guid[i] = linux_guid[i];
  fill_guid(e[2].uniq_guid, st);
  e[2].first_lba = data_start;
  e[2].last_lba = data_end;
  e[2].attrs = 0;
  gpt_set_name((UINT8 *)e[2].name, L"DATA");

  UINT32 entries_crc = 0;
  EFI_STATUS stt = uefi_call_wrapper(st->BootServices->CalculateCrc32, 3,
                                     entries, entries_bytes, &entries_crc);
  if (EFI_ERROR(stt)) {
    FreePool(entries);
    return stt;
  }

  // Primary header
  UINT8 hdr_sector[512];
  for (UINTN i = 0; i < 512; i++)
    hdr_sector[i] = 0;
  gpt_header_t *hdr = (gpt_header_t *)hdr_sector;
  hdr->signature = GPT_SIG;
  hdr->revision = GPT_REVISION;
  hdr->header_size = GPT_HEADER_SIZE;
  hdr->header_crc32 = 0;
  hdr->reserved = 0;
  hdr->current_lba = 1;
  hdr->backup_lba = last_lba;
  hdr->first_usable_lba = first_usable_lba;
  hdr->last_usable_lba = last_usable_lba;
  for (UINTN i = 0; i < 16; i++)
    hdr->disk_guid[i] = disk_guid[i];
  hdr->part_entry_lba = GPT_ENTRIES_LBA;
  hdr->num_part_entries = GPT_NUM_ENTRIES;
  hdr->part_entry_size = GPT_ENTRY_SIZE;
  hdr->part_entries_crc32 = entries_crc;

  UINT32 hdr_crc = 0;
  stt = uefi_call_wrapper(st->BootServices->CalculateCrc32, 3, hdr_sector,
                          GPT_HEADER_SIZE, &hdr_crc);
  if (EFI_ERROR(stt)) {
    FreePool(entries);
    return stt;
  }
  hdr->header_crc32 = hdr_crc;

  // Backup header
  UINT8 bkp_sector[512];
  for (UINTN i = 0; i < 512; i++)
    bkp_sector[i] = 0;
  gpt_header_t *bkp = (gpt_header_t *)bkp_sector;
  bkp->signature = GPT_SIG;
  bkp->revision = GPT_REVISION;
  bkp->header_size = GPT_HEADER_SIZE;
  bkp->header_crc32 = 0;
  bkp->reserved = 0;
  bkp->current_lba = last_lba;
  bkp->backup_lba = 1;
  bkp->first_usable_lba = first_usable_lba;
  bkp->last_usable_lba = last_usable_lba;
  for (UINTN i = 0; i < 16; i++)
    bkp->disk_guid[i] = disk_guid[i];
  bkp->part_entry_lba = backup_entries_lba;
  bkp->num_part_entries = GPT_NUM_ENTRIES;
  bkp->part_entry_size = GPT_ENTRY_SIZE;
  bkp->part_entries_crc32 = entries_crc;
  UINT32 bkp_crc = 0;
  stt = uefi_call_wrapper(st->BootServices->CalculateCrc32, 3, bkp_sector,
                          GPT_HEADER_SIZE, &bkp_crc);
  if (EFI_ERROR(stt)) {
    FreePool(entries);
    return stt;
  }
  bkp->header_crc32 = bkp_crc;

  // Write MBR + GPT primary + entries + backup entries + backup GPT.
  stt = write_protective_mbr(bio, total_sectors);
  if (EFI_ERROR(stt)) {
    FreePool(entries);
    return stt;
  }
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId, 1, 512,
                          hdr_sector);
  if (EFI_ERROR(stt)) {
    FreePool(entries);
    return stt;
  }
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          GPT_ENTRIES_LBA, entries_bytes, entries);
  if (EFI_ERROR(stt)) {
    FreePool(entries);
    return stt;
  }
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          backup_entries_lba, entries_bytes, entries);
  if (EFI_ERROR(stt)) {
    FreePool(entries);
    return stt;
  }
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          last_lba, 512, bkp_sector);
  FreePool(entries);
  if (EFI_ERROR(stt))
    return stt;

  uefi_call_wrapper(bio->FlushBlocks, 1, bio);

  if (out_esp_lba)
    *out_esp_lba = esp_start;
  if (out_esp_sectors)
    *out_esp_sectors = esp_sectors;
  if (out_boot_lba)
    *out_boot_lba = boot_start;
  if (out_boot_sectors)
    *out_boot_sectors = boot_sectors;
  if (out_data_lba)
    *out_data_lba = data_start;
  if (out_data_sectors)
    *out_data_sectors = (data_end - data_start) + 1ULL;
  return EFI_SUCCESS;
}

