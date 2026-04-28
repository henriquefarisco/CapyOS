#include "internal/uefi_loader_internal.h"

BOOLEAN guid_eq(const UINT8 *a, const UINT8 *b) {
  for (UINTN i = 0; i < 16; i++) {
    if (a[i] != b[i])
      return FALSE;
  }
  return TRUE;
}

EFI_STATUS read_file(EFI_FILE_HANDLE root, CHAR16 *path, VOID **buf,
                            UINTN *size);

struct boot_config_sector g_runtime_boot_cfg;
BOOLEAN g_runtime_boot_cfg_valid = FALSE;
EFI_PHYSICAL_ADDRESS g_kernel_reserved_base = 0;
UINTN g_kernel_reserved_pages = 0;
UINT8 g_kernel_block_scratch[KERNEL_BLOCK_SCRATCH_MAX];

void bootcfg_clear(struct boot_config_sector *cfg) {
  if (!cfg)
    return;
  for (UINTN i = 0; i < sizeof(*cfg); ++i) {
    ((UINT8 *)cfg)[i] = 0;
  }
}

static BOOLEAN ascii_is_alnum_u16(CHAR16 c) {
  if (c >= L'0' && c <= L'9')
    return TRUE;
  if (c >= L'a' && c <= L'z')
    return TRUE;
  if (c >= L'A' && c <= L'Z')
    return TRUE;
  return FALSE;
}

static CHAR16 ascii_upper_u16(CHAR16 c) {
  if (c >= L'a' && c <= L'z')
    return (CHAR16)(c - L'a' + L'A');
  return c;
}

void char16_to_ascii(char *out, UINTN out_len, const CHAR16 *in) {
  if (!out || out_len == 0) {
    return;
  }
  UINTN n = 0;
  if (in) {
    for (; in[n] && n + 1 < out_len; ++n) {
      CHAR16 c = in[n];
      out[n] = (char)((c <= 0x7F) ? c : '?');
    }
  }
  out[n] = 0;
}

int ascii_streq(const char *a, const char *b) {
  if (!a || !b) {
    return 0;
  }
  UINTN i = 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) {
      return 0;
    }
    ++i;
  }
  return a[i] == b[i];
}

int normalize_key_char16(const CHAR16 *in, char *out, UINTN out_len) {
  if (!in || !out || out_len < 2) {
    return -1;
  }
  UINTN n = 0;
  for (UINTN i = 0; in[i]; ++i) {
    CHAR16 c = in[i];
    if (c == L'-' || c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
      continue;
    }
    if (!ascii_is_alnum_u16(c)) {
      return -1;
    }
    if (n + 1 >= out_len) {
      return -1;
    }
    out[n++] = (char)ascii_upper_u16(c);
  }
  if (n < 8) {
    return -1;
  }
  out[n] = 0;
  return 0;
}

EFI_STATUS load_boot_config_from_root(EFI_FILE_HANDLE root) {
  if (!root) {
    return EFI_INVALID_PARAMETER;
  }
  bootcfg_clear(&g_runtime_boot_cfg);
  g_runtime_boot_cfg_valid = FALSE;

  VOID *cfg_buf = NULL;
  UINTN cfg_size = 0;
  EFI_STATUS st = read_file(root, L"BOOT\\CAPYCFG.BIN", &cfg_buf, &cfg_size);
  if (EFI_ERROR(st)) {
    st = read_file(root, L"\\BOOT\\CAPYCFG.BIN", &cfg_buf, &cfg_size);
  }
  if (EFI_ERROR(st)) {
    st = read_file(root, L"\\boot\\capycfg.bin", &cfg_buf, &cfg_size);
  }
  if (EFI_ERROR(st)) {
    st = read_file(root, L"boot\\capycfg.bin", &cfg_buf, &cfg_size);
  }
  if (EFI_ERROR(st)) {
    return st;
  }
  if (!cfg_buf || cfg_size < sizeof(struct boot_config_sector)) {
    if (cfg_buf)
      FreePool(cfg_buf);
    return EFI_LOAD_ERROR;
  }

  struct boot_config_sector *cfg = (struct boot_config_sector *)cfg_buf;
  if (cfg->magic == BOOT_CONFIG_MAGIC) {
    g_runtime_boot_cfg = *cfg;
    if (g_runtime_boot_cfg.version == 0) {
      g_runtime_boot_cfg.version = 1;
    }
    if (g_runtime_boot_cfg.version > BOOT_CONFIG_VERSION) {
      g_runtime_boot_cfg.version = BOOT_CONFIG_VERSION;
    }
    g_runtime_boot_cfg_valid = TRUE;
  }
  FreePool(cfg_buf);
  return g_runtime_boot_cfg_valid ? EFI_SUCCESS : EFI_LOAD_ERROR;
}

EFI_STATUS open_file_read(EFI_FILE_HANDLE root, CHAR16 *path,
                                 EFI_FILE_HANDLE *out_fh, UINTN *out_size) {
  EFI_STATUS st;
  EFI_FILE_HANDLE fh = NULL;
  if (!root || !path || !out_fh || !out_size) {
    return EFI_INVALID_PARAMETER;
  }
  *out_fh = NULL;
  *out_size = 0;
  st = uefi_call_wrapper(root->Open, 5, root, &fh, path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(st)) {
    if (st != EFI_NOT_FOUND) {
      Print(L"[UEFI] Open(%s) falhou: %r\r\n", path, st);
    }
    return st;
  }

  EFI_GUID info_guid = EFI_FILE_INFO_ID;
  // Some firmware returns EFI_INVALID_PARAMETER when Buffer==NULL, so use a
  // growable buffer.
  UINTN info_sz = sizeof(EFI_FILE_INFO) + 256;
  EFI_FILE_INFO *info = AllocatePool(info_sz);
  if (!info) {
    uefi_call_wrapper(fh->Close, 1, fh);
    return EFI_OUT_OF_RESOURCES;
  }
  st = uefi_call_wrapper(fh->GetInfo, 4, fh, &info_guid, &info_sz, info);
  if (st == EFI_BUFFER_TOO_SMALL) {
    FreePool(info);
    info = AllocatePool(info_sz);
    if (!info) {
      uefi_call_wrapper(fh->Close, 1, fh);
      return EFI_OUT_OF_RESOURCES;
    }
    st = uefi_call_wrapper(fh->GetInfo, 4, fh, &info_guid, &info_sz, info);
  }
  if (EFI_ERROR(st)) {
    Print(L"[UEFI] GetInfo(%s) falhou: %r (info_sz=%lu)\r\n", path, st,
          info_sz);
    FreePool(info);
    uefi_call_wrapper(fh->Close, 1, fh);
    return st;
  }

  *out_size = info->FileSize;
  *out_fh = fh;
  FreePool(info);
  return EFI_SUCCESS;
}

EFI_STATUS read_file(EFI_FILE_HANDLE root, CHAR16 *path, VOID **buf,
                            UINTN *size) {
  EFI_STATUS st;
  EFI_FILE_HANDLE fh = NULL;
  UINTN file_size = 0;
  UINTN req = 0;
  if (!buf || !size) {
    return EFI_INVALID_PARAMETER;
  }
  *buf = NULL;
  *size = 0;

  st = open_file_read(root, path, &fh, &file_size);
  if (EFI_ERROR(st)) {
    return st;
  }

  *size = file_size;
  *buf = AllocatePool(file_size);
  if (!*buf) {
    uefi_call_wrapper(fh->Close, 1, fh);
    return EFI_OUT_OF_RESOURCES;
  }

  req = file_size;
  st = uefi_call_wrapper(fh->Read, 3, fh, &req, *buf);
  if (EFI_ERROR(st)) {
    Print(L"[UEFI] Read(%s) falhou: %r (req=%lu)\r\n", path, st, req);
    FreePool(*buf);
    *buf = NULL;
    *size = 0;
    uefi_call_wrapper(fh->Close, 1, fh);
    return st;
  }
  if (req != file_size) {
    Print(L"[UEFI] Read(%s) retornou leitura curta: got=%lu expected=%lu\r\n",
          path, req, file_size);
    FreePool(*buf);
    *buf = NULL;
    *size = 0;
    uefi_call_wrapper(fh->Close, 1, fh);
    return EFI_LOAD_ERROR;
  }
  *size = file_size;
  uefi_call_wrapper(fh->Close, 1, fh);
  return EFI_SUCCESS;
}

