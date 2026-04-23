// CapyOS UEFI loader (x86_64): ELF64 loader that reads \\boot\\capyos64.bin
// from the same volume as BOOTX64.EFI, loads PT_LOAD segments at p_paddr and
// jumps to e_entry after ExitBootServices, passing a basic handoff.
#include "boot/boot_manifest.h"
#include "boot/boot_config.h"
#include "boot/handoff.h"
#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

#define DEBUGCON_PORT 0xE9
static inline void dbgcon_putc(UINT8 c) {
  __asm__ __volatile__("outb %0, %1" : : "a"(c), "Nd"((UINT16)DEBUGCON_PORT));
}

#define ELF_MAGIC 0x464C457F
#define PT_LOAD 1
#define EM_X86_64 62
#define KERNEL_MAX_PHDRS 32
#define KERNEL_BLOCK_SCRATCH_MAX 4096
/* VMware UEFI lies about EfiConventionalMemory below ~16M and also reports
   16M as free even when its own structures sit there. 0x400000 (4 MB) was
   verified working in a prior session — far enough above firmware (~1MB
   compat hole) and well below the 256MB+ region VMware reserves internally. */
#define KERNEL_FIXED_RESERVE_BASE 0x10000000ULL /* 256 MiB */
#define KERNEL_FIXED_RESERVE_BYTES (48ULL * 1024ULL * 1024ULL)

#define GPT_HEADER_LBA 1
#define GPT_SIG 0x5452415020494645ULL /* "EFI PART" */
#define EFI_PART_TYPE_ESP                                                      \
  {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,                             \
   0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}
/* TemporÃƒÂ¡rio: GUID de partiÃƒÂ§ÃƒÂ£o BOOT CAPYOS (ajustar no instalador GPT) */
#define EFI_PART_TYPE_CAPYOS_BOOT                                              \
  {0x76, 0x0b, 0x98, 0x04, 0x42, 0x10, 0x4c, 0x9b,                             \
   0x86, 0x1f, 0x11, 0xe0, 0x29, 0xea, 0xc1, 0x01}
#define EFI_PART_TYPE_LINUX_FS                                                 \
  {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,                             \
   0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4}

typedef struct {
  UINT8 e_ident[16];
  UINT16 e_type;
  UINT16 e_machine;
  UINT32 e_version;
  UINT64 e_entry;
  UINT64 e_phoff;
  UINT64 e_shoff;
  UINT32 e_flags;
  UINT16 e_ehsize;
  UINT16 e_phentsize;
  UINT16 e_phnum;
  UINT16 e_shentsize;
  UINT16 e_shnum;
  UINT16 e_shstrndx;
} Elf64_Ehdr;

typedef struct {
  UINT32 p_type;
  UINT32 p_flags;
  UINT64 p_offset;
  UINT64 p_vaddr;
  UINT64 p_paddr;
  UINT64 p_filesz;
  UINT64 p_memsz;
  UINT64 p_align;
} Elf64_Phdr;

typedef struct {
  UINT64 signature;
  UINT32 revision;
  UINT32 header_size;
  UINT32 header_crc32;
  UINT32 reserved;
  UINT64 current_lba;
  UINT64 backup_lba;
  UINT64 first_usable_lba;
  UINT64 last_usable_lba;
  UINT8 disk_guid[16];
  UINT64 part_entry_lba;
  UINT32 num_part_entries;
  UINT32 part_entry_size;
  UINT32 part_entries_crc32;
} __attribute__((packed)) gpt_header_t;

typedef struct {
  UINT8 part_type_guid[16];
  UINT8 uniq_guid[16];
  UINT64 first_lba;
  UINT64 last_lba;
  UINT64 attrs;
  UINT16 name[36];
} __attribute__((packed)) gpt_entry_t;

typedef struct {
  UINT8 Type;
  UINT8 SubType;
  UINT8 Length[2];
} __attribute__((packed)) dp_node_hdr_t;

typedef struct {
  dp_node_hdr_t Header;
  UINT32 PartitionNumber;
  UINT64 PartitionStart;
  UINT64 PartitionSize;
  UINT8 Signature[16];
  UINT8 MBRType;
  UINT8 SignatureType;
} __attribute__((packed)) dp_hd_node_t;

#define DP_TYPE_MEDIA 0x04
#define DP_SUBTYPE_HARDDRIVE 0x01
#define DP_TYPE_END 0x7F
#define DP_SUBTYPE_END_ENTIRE 0xFF

static BOOLEAN guid_eq(const UINT8 *a, const UINT8 *b) {
  for (UINTN i = 0; i < 16; i++) {
    if (a[i] != b[i])
      return FALSE;
  }
  return TRUE;
}

static EFI_STATUS read_file(EFI_FILE_HANDLE root, CHAR16 *path, VOID **buf,
                            UINTN *size);

static struct boot_config_sector g_runtime_boot_cfg;
static BOOLEAN g_runtime_boot_cfg_valid = FALSE;
static EFI_PHYSICAL_ADDRESS g_kernel_reserved_base = 0;
static UINTN g_kernel_reserved_pages = 0;
static UINT8 g_kernel_block_scratch[KERNEL_BLOCK_SCRATCH_MAX];

static void bootcfg_clear(struct boot_config_sector *cfg) {
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

static void char16_to_ascii(char *out, UINTN out_len, const CHAR16 *in) {
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

static int ascii_streq(const char *a, const char *b) {
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

static int normalize_key_char16(const CHAR16 *in, char *out, UINTN out_len) {
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

static EFI_STATUS load_boot_config_from_root(EFI_FILE_HANDLE root) {
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

static EFI_STATUS open_file_read(EFI_FILE_HANDLE root, CHAR16 *path,
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

static EFI_STATUS read_file(EFI_FILE_HANDLE root, CHAR16 *path, VOID **buf,
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

