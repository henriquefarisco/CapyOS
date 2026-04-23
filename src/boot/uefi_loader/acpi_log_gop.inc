static UINT8 sum8_bytes(const UINT8 *p, UINTN len) {
  UINT8 s = 0;
  for (UINTN i = 0; i < len; i++)
    s = (UINT8)(s + p[i]);
  return s;
}

static BOOLEAN rsdp_sig_ok(const UINT8 *p) {
  if (!p)
    return FALSE;
  const CHAR8 sig[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};
  for (UINTN i = 0; i < 8; i++) {
    if ((CHAR8)p[i] != sig[i])
      return FALSE;
  }
  return TRUE;
}

static BOOLEAN rsdp_is_valid_ptr(const VOID *ptr) {
  if (!ptr)
    return FALSE;
  const UINT8 *p = (const UINT8 *)ptr;
  if (!rsdp_sig_ok(p))
    return FALSE;
  if (sum8_bytes(p, 20) != 0)
    return FALSE;

  const acpi_rsdp_t *r = (const acpi_rsdp_t *)ptr;
  if (r->revision >= 2) {
    UINT32 len = r->length;
    if (len < 36 || len > 4096) {
      // Alguns firmwares reportam length inesperado; aceite ACPI 1.0 checksum
      // como mÃƒÂ­nimo.
      return TRUE;
    }
    return sum8_bytes(p, len) == 0;
  }
  return TRUE;
}

static EFI_STATUS scan_rsdp_range(UINT64 start, UINT64 end, UINT64 *out_rsdp) {
  if (!out_rsdp)
    return EFI_INVALID_PARAMETER;
  *out_rsdp = 0;
  if (end <= start)
    return EFI_INVALID_PARAMETER;

  // RSDP is 16-byte aligned in memory.
  UINT64 a = (start + 15ULL) & ~15ULL;
  for (; a + 36ULL <= end; a += 16ULL) {
    const VOID *p = (const VOID *)(UINTN)a;
    if (rsdp_is_valid_ptr(p)) {
      *out_rsdp = a;
      return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}

static EFI_STATUS scan_rsdp(UINT64 *out_rsdp) {
  if (!out_rsdp)
    return EFI_INVALID_PARAMETER;
  *out_rsdp = 0;

  // EBDA: segmento em 0x40E (BDA). Nem sempre presente em UEFI, mas barato
  // tentar.
  UINT16 ebda_seg = *(volatile UINT16 *)(UINTN)0x40E;
  UINTN ebda = ((UINTN)ebda_seg) << 4;
  if (ebda >= 0x80000 && ebda < 0xA0000) {
    UINT64 found = 0;
    if (!EFI_ERROR(
            scan_rsdp_range((UINT64)ebda, (UINT64)ebda + 1024ULL, &found)) &&
        found) {
      *out_rsdp = found;
      return EFI_SUCCESS;
    }
  }

  // ÃƒÂrea BIOS "high" tradicional: 0xE0000..0xFFFFF
  UINT64 found = 0;
  if (!EFI_ERROR(scan_rsdp_range(0xE0000ULL, 0x100000ULL, &found)) && found) {
    *out_rsdp = found;
    return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}

static EFI_STATUS find_rsdp_memmap(EFI_SYSTEM_TABLE *st, UINT64 *out_rsdp) {
  if (!st || !st->BootServices || !out_rsdp)
    return EFI_INVALID_PARAMETER;
  *out_rsdp = 0;

  EFI_STATUS stt;
  UINTN map_sz = 0, map_key = 0, desc_sz = 0;
  UINT32 desc_ver = 0;
  stt = uefi_call_wrapper(st->BootServices->GetMemoryMap, 5, &map_sz, NULL,
                          &map_key, &desc_sz, &desc_ver);
  if (stt != EFI_BUFFER_TOO_SMALL)
    return stt;

  // Try a few times in case the map changes while we allocate the buffer.
  for (UINTN attempt = 0; attempt < 4; attempt++) {
    UINTN req = map_sz + desc_sz * 16;
    VOID *map = AllocatePool(req);
    if (!map)
      return EFI_OUT_OF_RESOURCES;

    UINTN got = req;
    stt = uefi_call_wrapper(st->BootServices->GetMemoryMap, 5, &got, map,
                            &map_key, &desc_sz, &desc_ver);
    if (stt == EFI_BUFFER_TOO_SMALL) {
      FreePool(map);
      map_sz = got;
      continue;
    }
    if (EFI_ERROR(stt)) {
      FreePool(map);
      return stt;
    }

    // Scan only ACPI memory ranges. Cap each range to avoid pathological scans.
    UINT8 *p = (UINT8 *)map;
    for (UINTN off = 0; off + desc_sz <= got; off += desc_sz) {
      EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)(p + off);
      if (d->Type != EfiACPIReclaimMemory && d->Type != EfiACPIMemoryNVS)
        continue;

      UINT64 start = d->PhysicalStart;
      UINT64 end = start + ((UINT64)d->NumberOfPages << 12);
      if (end <= start)
        continue;

      // RSDP is usually near the beginning of these regions on modern firmware.
      UINT64 cap = start + (2ULL << 20); // 2 MiB
      if (cap < end)
        end = cap;

      UINT64 found = 0;
      if (!EFI_ERROR(scan_rsdp_range(start, end, &found)) && found) {
        *out_rsdp = found;
        FreePool(map);
        return EFI_SUCCESS;
      }
    }

    FreePool(map);
    return EFI_NOT_FOUND;
  }

  return EFI_NOT_FOUND;
}

static EFI_STATUS find_rsdp(EFI_SYSTEM_TABLE *st, UINT64 *out_rsdp) {
  if (!out_rsdp)
    return EFI_INVALID_PARAMETER;
  *out_rsdp = 0;
  static EFI_GUID acpi20 = ACPI_20_TABLE_GUID;
  static EFI_GUID acpi10 = ACPI_TABLE_GUID;
  for (UINTN i = 0; i < st->NumberOfTableEntries; i++) {
    EFI_CONFIGURATION_TABLE *ct = &st->ConfigurationTable[i];
    if (CompareGuid(&ct->VendorGuid, &acpi20) ||
        CompareGuid(&ct->VendorGuid, &acpi10)) {
      *out_rsdp = (UINT64)(UINTN)ct->VendorTable;
      return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}

static EFI_STATUS copy_rsdp_low(EFI_SYSTEM_TABLE *st, UINT64 rsdp,
                                UINT64 *out_copy) {
  if (!st || !st->BootServices || !out_copy)
    return EFI_INVALID_PARAMETER;
  *out_copy = 0;
  if (!rsdp)
    return EFI_NOT_FOUND;

  EFI_PHYSICAL_ADDRESS dst_pa = 0x3FFFFFFF;
  EFI_STATUS stt =
      uefi_call_wrapper(st->BootServices->AllocatePages, 4, AllocateMaxAddress,
                        EfiLoaderData, 1, &dst_pa);
  if (EFI_ERROR(stt))
    return stt;

  UINT8 *dst = (UINT8 *)(UINTN)dst_pa;
  UINT8 *src = (UINT8 *)(UINTN)rsdp;
  for (UINTN i = 0; i < 64; i++)
    dst[i] = src[i];
  *out_copy = (UINT64)dst_pa;
  return EFI_SUCCESS;
}

typedef struct {
  EFI_FILE_HANDLE root;
  EFI_FILE_HANDLE file;
} log_file_t;

static void log_close(log_file_t *lf) {
  if (!lf)
    return;
  if (lf->file) {
    uefi_call_wrapper(lf->file->Close, 1, lf->file);
    lf->file = NULL;
  }
  if (lf->root) {
    uefi_call_wrapper(lf->root->Close, 1, lf->root);
    lf->root = NULL;
  }
}

static EFI_STATUS log_open(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
                           log_file_t *out) {
  if (!out)
    return EFI_INVALID_PARAMETER;
  out->root = NULL;
  out->file = NULL;

  EFI_LOADED_IMAGE *li = NULL;
  EFI_STATUS stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, image,
                                     &LoadedImageProtocol, (VOID **)&li);
  if (EFI_ERROR(stt) || !li)
    return stt;

  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, li->DeviceHandle,
                          &FileSystemProtocol, (VOID **)&sfs);
  if (EFI_ERROR(stt) || !sfs)
    return stt;

  stt = uefi_call_wrapper(sfs->OpenVolume, 2, sfs, &out->root);
  if (EFI_ERROR(stt) || !out->root)
    return stt;

  EFI_FILE_HANDLE fh = NULL;
  stt = uefi_call_wrapper(
      out->root->Open, 5, out->root, &fh, L"\\EFI\\CAPYOS.LOG",
      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
      EFI_FILE_ARCHIVE);
  if (EFI_ERROR(stt) || !fh) {
    log_close(out);
    return stt;
  }
  // Seek to end for append.
  uefi_call_wrapper(fh->SetPosition, 2, fh, 0xFFFFFFFFFFFFFFFFULL);
  out->file = fh;
  return EFI_SUCCESS;
}

static void log_write_ascii(log_file_t *lf, const char *s) {
  if (!lf || !lf->file || !s)
    return;
  UINTN len = 0;
  while (s[len])
    len++;
  if (!len)
    return;
  uefi_call_wrapper(lf->file->Write, 3, lf->file, &len, (VOID *)s);
}

static void log_write_u64_hex(log_file_t *lf, UINT64 v) {
  char buf[19];
  buf[0] = '0';
  buf[1] = 'x';
  for (int i = 0; i < 16; i++) {
    UINT8 nib = (UINT8)((v >> (60 - i * 4)) & 0xF);
    buf[2 + i] = (char)(nib < 10 ? ('0' + nib) : ('A' + (nib - 10)));
  }
  buf[18] = 0;
  log_write_ascii(lf, buf);
}

static void log_write_bytes_hex(log_file_t *lf, const UINT8 *p, UINTN len) {
  if (!lf || !lf->file || !p || len == 0)
    return;
  if (len > 64)
    len = 64;
  char buf[3 * 64 + 3];
  UINTN out = 0;
  for (UINTN i = 0; i < len; i++) {
    UINT8 b = p[i];
    UINT8 hi = (UINT8)((b >> 4) & 0xF);
    UINT8 lo = (UINT8)(b & 0xF);
    buf[out++] = (char)(hi < 10 ? ('0' + hi) : ('A' + (hi - 10)));
    buf[out++] = (char)(lo < 10 ? ('0' + lo) : ('A' + (lo - 10)));
    buf[out++] = (i + 1 == len) ? '\n' : ' ';
  }
  buf[out] = 0;
  log_write_ascii(lf, buf);
}

static EFI_STATUS get_gop(EFI_SYSTEM_TABLE *st,
                          EFI_GRAPHICS_OUTPUT_PROTOCOL **out_gop) {
  EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
  return uefi_call_wrapper(st->BootServices->LocateProtocol, 3, &gop_guid, NULL,
                           (VOID **)out_gop);
}

