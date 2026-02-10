// NoirOS UEFI loader (x86_64): ELF64 loader that reads \\boot\\noiros64.bin
// from the same volume as BOOTX64.EFI, loads PT_LOAD segments at p_paddr and
// jumps to e_entry after ExitBootServices, passing a basic handoff.
#include "boot/boot_manifest.h"
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

#define GPT_HEADER_LBA 1
#define GPT_SIG 0x5452415020494645ULL /* "EFI PART" */
#define EFI_PART_TYPE_ESP                                                      \
  {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,                             \
   0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}
/* Temporário: GUID de partição BOOT NoirOS (ajustar no instalador GPT) */
#define EFI_PART_TYPE_NOIROS_BOOT                                              \
  {0x76, 0x0b, 0x98, 0x04, 0x42, 0x10, 0x4c, 0x9b,                             \
   0x86, 0x1f, 0x11, 0xe0, 0x29, 0xea, 0xc1, 0x01}

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

static BOOLEAN guid_eq(const UINT8 *a, const UINT8 *b) {
  for (UINTN i = 0; i < 16; i++) {
    if (a[i] != b[i])
      return FALSE;
  }
  return TRUE;
}

static EFI_STATUS read_file(EFI_FILE_HANDLE root, CHAR16 *path, VOID **buf,
                            UINTN *size) {
  EFI_STATUS st;
  EFI_FILE_HANDLE fh = NULL;
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

  *size = info->FileSize;
  *buf = AllocatePool(*size);
  if (!*buf) {
    FreePool(info);
    uefi_call_wrapper(fh->Close, 1, fh);
    return EFI_OUT_OF_RESOURCES;
  }

  UINTN req = *size;
  st = uefi_call_wrapper(fh->Read, 3, fh, size, *buf);
  if (EFI_ERROR(st)) {
    Print(L"[UEFI] Read(%s) falhou: %r (req=%lu)\r\n", path, st, req);
  }
  uefi_call_wrapper(fh->Close, 1, fh);
  FreePool(info);
  return st;
}

static EFI_STATUS load_kernel_from_buffer(EFI_SYSTEM_TABLE *st,
                                          VOID *kernel_buf, UINTN kernel_size,
                                          EFI_PHYSICAL_ADDRESS *entry_out) {
  if (kernel_size < sizeof(Elf64_Ehdr)) {
    Print(L"[UEFI] kernel muito pequeno\r\n");
    return EFI_LOAD_ERROR;
  }

  Elf64_Ehdr *eh = (Elf64_Ehdr *)kernel_buf;
  if (*(UINT32 *)eh->e_ident != ELF_MAGIC || eh->e_machine != EM_X86_64) {
    Print(L"[UEFI] kernel ELF64 inválido\r\n");
    return EFI_UNSUPPORTED;
  }

  // Carrega o kernel em um bloco contíguo abaixo de 4GiB e aplica um offset
  // único. O kernel 64-bit é linkado em um endereço base (ex.: 0x0040_0000),
  // mas deve ser escrito de forma relocável (PC-relative/RIP-relative) para
  // suportar esse offset.
  EFI_PHYSICAL_ADDRESS link_base = 0xFFFFFFFFFFFFFFFFULL;
  EFI_PHYSICAL_ADDRESS link_end = 0;

  Elf64_Phdr *ph = (Elf64_Phdr *)((UINT8 *)kernel_buf + eh->e_phoff);
  for (UINT16 i = 0; i < eh->e_phnum; i++, ph++) {
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
      continue;
    EFI_PHYSICAL_ADDRESS seg_start = ph->p_paddr & ~0xFFFULL;
    EFI_PHYSICAL_ADDRESS seg_end =
        (ph->p_paddr + ph->p_memsz + 0xFFFULL) & ~0xFFFULL;
    if (seg_start < link_base)
      link_base = seg_start;
    if (seg_end > link_end)
      link_end = seg_end;
  }
  if (link_base == 0xFFFFFFFFFFFFFFFFULL || link_end <= link_base) {
    Print(L"[UEFI] ELF sem segmentos PT_LOAD válidos\r\n");
    return EFI_LOAD_ERROR;
  }

  UINTN span = (UINTN)(link_end - link_base);
  UINTN pages = (span + 0xFFF) >> 12;

  EFI_PHYSICAL_ADDRESS load_base = 0xFFFFFFFFULL; // abaixo de 4GiB
  EFI_STATUS alloc_st =
      uefi_call_wrapper(st->BootServices->AllocatePages, 4, AllocateMaxAddress,
                        EfiLoaderData, pages, &load_base);
  if (EFI_ERROR(alloc_st)) {
    Print(L"[UEFI] AllocatePages(kernel) falhou: %r\r\n", alloc_st);
    return alloc_st;
  }

  // Zera toda a área (inclui gaps e BSS)
  UINT8 *base = (UINT8 *)(UINTN)load_base;
  for (UINTN b = 0; b < span; b++)
    base[b] = 0;

  // Copia os segmentos para o novo base
  ph = (Elf64_Phdr *)((UINT8 *)kernel_buf + eh->e_phoff);
  for (UINT16 i = 0; i < eh->e_phnum; i++, ph++) {
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
      continue;
    if (ph->p_offset + ph->p_filesz > kernel_size) {
      Print(L"[UEFI] Segmento fora do buffer (seg %d)\r\n", i);
      return EFI_LOAD_ERROR;
    }
    EFI_PHYSICAL_ADDRESS dst_pa = load_base + (ph->p_paddr - link_base);
    UINT8 *dst = (UINT8 *)(UINTN)dst_pa;
    UINT8 *src = (UINT8 *)kernel_buf + ph->p_offset;
    for (UINTN b = 0; b < ph->p_filesz; b++) {
      dst[b] = src[b];
    }
  }

  *entry_out = (EFI_PHYSICAL_ADDRESS)(load_base + (eh->e_entry - link_base));
  return EFI_SUCCESS;
}

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
  UINT8 boot_guid[16] = EFI_PART_TYPE_NOIROS_BOOT;

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
        continue; // só BOOT
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
  // Usar manifest se válido
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
        lba += boot_part_lba; // lba relativo à partição BOOT
      }
      EFI_STATUS rb =
          uefi_call_wrapper(bio->ReadBlocks, 5, bio, bio->Media->MediaId, lba,
                            total_bytes, kernel_buf);
      if (!EFI_ERROR(rb)) {
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
  stt = read_file(root, L"BOOT\\NOIROS64.BIN", &kernel_buf, &kernel_size);
  if (EFI_ERROR(stt) && kernel_buf) {
    FreePool(kernel_buf);
    kernel_buf = NULL;
    kernel_size = 0;
  }
  if (EFI_ERROR(stt))
    stt = read_file(root, L"\\BOOT\\NOIROS64.BIN", &kernel_buf, &kernel_size);
  if (EFI_ERROR(stt) && kernel_buf) {
    FreePool(kernel_buf);
    kernel_buf = NULL;
    kernel_size = 0;
  }
  if (EFI_ERROR(stt))
    stt = read_file(root, L"\\boot\\noiros64.bin", &kernel_buf, &kernel_size);
  if (EFI_ERROR(stt) && kernel_buf) {
    FreePool(kernel_buf);
    kernel_buf = NULL;
    kernel_size = 0;
  }
  if (EFI_ERROR(stt))
    stt = read_file(root, L"boot\\noiros64.bin", &kernel_buf, &kernel_size);
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] Falha ao ler kernel: %r\r\n", stt);
    return stt;
  }
  EFI_STATUS lkst =
      load_kernel_from_buffer(st, kernel_buf, kernel_size, entry_out);
  FreePool(kernel_buf);
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

static UINT64 align_up_u64(UINT64 v, UINT64 a) {
  if (a == 0)
    return v;
  UINT64 r = v % a;
  if (r == 0)
    return v;
  return v + (a - r);
}

static UINT32 checksum32_words(const UINT8 *data, UINTN len) {
  if (!data || len == 0)
    return 0;
  UINT32 sum = 0;
  UINTN i = 0;
  while (i + 4 <= len) {
    UINT32 v = (UINT32)data[i] | ((UINT32)data[i + 1] << 8) |
               ((UINT32)data[i + 2] << 16) | ((UINT32)data[i + 3] << 24);
    sum += v;
    i += 4;
  }
  while (i < len) {
    sum += data[i];
    i++;
  }
  return sum;
}

static VOID build_manifest(struct boot_manifest *m, UINT32 kernel_lba,
                           UINT32 kernel_sectors, UINT32 cksum32) {
  if (!m)
    return;
  // Mirror boot_manifest_init/add without linking extra objects.
  m->magic = BOOT_MANIFEST_MAGIC;
  m->version = BOOT_MANIFEST_VERSION;
  m->entry_count = 1;
  m->reserved = 0;
  for (UINTN i = 0; i < 4; i++) {
    m->entries[i].type = 0;
    m->entries[i].lba_start = 0;
    m->entries[i].sector_count = 0;
    m->entries[i].checksum32 = 0;
    m->entries[i].reserved = 0;
  }
  m->entries[0].type = BOOT_ENTRY_NORMAL;
  m->entries[0].lba_start = kernel_lba;
  m->entries[0].sector_count = kernel_sectors;
  m->entries[0].checksum32 = cksum32;
  m->entries[0].reserved = 0;
}

static EFI_STATUS open_boot_volume(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
                                   EFI_HANDLE *out_fs_handle,
                                   EFI_FILE_HANDLE *out_root) {
  if (!st || !st->BootServices || !out_fs_handle || !out_root)
    return EFI_INVALID_PARAMETER;
  *out_fs_handle = NULL;
  *out_root = NULL;

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
  if (EFI_ERROR(stt) || sfs == NULL)
    return stt;

  EFI_FILE_HANDLE root = NULL;
  stt = uefi_call_wrapper(sfs->OpenVolume, 2, sfs, &root);
  if (EFI_ERROR(stt) || root == NULL)
    return stt;

  *out_fs_handle = fs_handle;
  *out_root = root;
  return EFI_SUCCESS;
}

static BOOLEAN boot_volume_is_readonly(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
  EFI_LOADED_IMAGE *li = NULL;
  EFI_STATUS stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, image,
                                     &LoadedImageProtocol, (VOID **)&li);
  if (EFI_ERROR(stt) || !li)
    return FALSE;
  EFI_BLOCK_IO_PROTOCOL *bio = NULL;
  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, li->DeviceHandle,
                          &BlockIoProtocol, (VOID **)&bio);
  if (EFI_ERROR(stt) || !bio || !bio->Media)
    return FALSE;
  return bio->Media->ReadOnly ? TRUE : FALSE;
}

static BOOLEAN boot_volume_has_marker(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
  EFI_HANDLE fs_handle = NULL;
  EFI_FILE_HANDLE root = NULL;
  EFI_STATUS stt = open_boot_volume(image, st, &fs_handle, &root);
  if (EFI_ERROR(stt) || !root)
    return FALSE;

  EFI_FILE_HANDLE fh = NULL;
  stt = uefi_call_wrapper(root->Open, 5, root, &fh, L"\\NOIROS.INI",
                          EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(stt)) {
    stt = uefi_call_wrapper(root->Open, 5, root, &fh, L"NOIROS.INI",
                            EFI_FILE_MODE_READ, 0);
  }
  if (!EFI_ERROR(stt) && fh) {
    uefi_call_wrapper(fh->Close, 1, fh);
    uefi_call_wrapper(root->Close, 1, root);
    return TRUE;
  }

  uefi_call_wrapper(root->Close, 1, root);
  return FALSE;
}

static EFI_STATUS choose_target_disk(EFI_SYSTEM_TABLE *st,
                                     EFI_BLOCK_IO_PROTOCOL **out_bio) {
  if (!st || !st->BootServices || !out_bio)
    return EFI_INVALID_PARAMETER;
  *out_bio = NULL;

  EFI_HANDLE *handles = NULL;
  UINTN count = 0;
  EFI_STATUS stt =
      uefi_call_wrapper(st->BootServices->LocateHandleBuffer, 5, ByProtocol,
                        &BlockIoProtocol, NULL, &count, &handles);
  if (EFI_ERROR(stt) || !handles || count == 0)
    return EFI_NOT_FOUND;

  EFI_BLOCK_IO_PROTOCOL *best = NULL;
  UINT64 best_blocks = 0;
  for (UINTN i = 0; i < count; i++) {
    EFI_BLOCK_IO_PROTOCOL *bio = NULL;
    stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, handles[i],
                            &BlockIoProtocol, (VOID **)&bio);
    if (EFI_ERROR(stt) || !bio || !bio->Media)
      continue;
    if (bio->Media->LogicalPartition)
      continue;
    if (bio->Media->ReadOnly)
      continue;
    if (bio->Media->RemovableMedia)
      continue;
    if (bio->Media->BlockSize != 512)
      continue; // esperado pelo layout atual
    UINT64 blocks = (UINT64)bio->Media->LastBlock + 1ULL;
    if (blocks > best_blocks) {
      best = bio;
      best_blocks = blocks;
    }
  }
  FreePool(handles);
  if (!best)
    return EFI_NOT_FOUND;
  *out_bio = best;
  return EFI_SUCCESS;
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

/* Generate pseudo-random encryption key (displayed to user for recovery) */
static void generate_recovery_key(EFI_SYSTEM_TABLE *st, CHAR16 *key_out,
                                  UINTN key_len) {
  /* Use UEFI timer and memory addresses as entropy source */
  UINT64 tsc = 0;
  __asm__ volatile("rdtsc" : "=A"(tsc));
  UINT64 entropy = tsc ^ (UINT64)(UINTN)st ^ (UINT64)(UINTN)key_out;

  const CHAR16 *hex = L"0123456789ABCDEF";
  UINTN pos = 0;
  for (UINTN i = 0; i < 4 && pos + 5 < key_len; i++) {
    if (i > 0 && pos + 1 < key_len) {
      key_out[pos++] = L'-';
    }
    for (UINTN j = 0; j < 4 && pos + 1 < key_len; j++) {
      key_out[pos++] = hex[entropy & 0xF];
      entropy = (entropy >> 4) ^ (entropy * 0x5DEECE66DLL);
    }
  }
  key_out[pos] = 0;
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
                                   UINT64 *out_boot_sectors) {
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
  UINT8 boot_guid[16] = EFI_PART_TYPE_NOIROS_BOOT;
  UINT8 linux_guid[16] = {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
                          0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};
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

  // BOOT (NoirOS)
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
  return EFI_SUCCESS;
}

static BOOLEAN fat32_alloc_contig(UINT32 *fat, UINT32 fat_len,
                                  UINT32 bytes_per_cluster, UINT32 *next_free,
                                  UINTN bytes_needed, UINT32 *out_start,
                                  UINT32 *out_clusters) {
  if (!fat || !next_free || !out_start || !out_clusters)
    return FALSE;
  if (fat_len < 4 || bytes_per_cluster == 0)
    return FALSE;

  UINTN need = (bytes_needed + bytes_per_cluster - 1U) / bytes_per_cluster;
  if (need == 0)
    need = 1;

  UINT64 start64 = (UINT64)(*next_free);
  if (start64 < 2ULL)
    start64 = 2ULL;
  if (start64 + need > (UINT64)fat_len)
    return FALSE;

  UINT32 start = (UINT32)start64;
  for (UINTN i = 0; i < need; i++) {
    UINT32 c = start + (UINT32)i;
    fat[c] = (i + 1 < need) ? (c + 1U) : 0x0FFFFFFFU;
  }
  *next_free = start + (UINT32)need;
  *out_start = start;
  *out_clusters = (UINT32)need;
  return TRUE;
}

static VOID fat32_dirent83(UINT8 *ent, const char *name8, const char *ext3,
                           UINT8 attr, UINT32 cluster, UINT32 size) {
  if (!ent || !name8 || !ext3)
    return;
  for (UINTN i = 0; i < 32; i++)
    ent[i] = 0;
  for (UINTN i = 0; i < 8; i++)
    ent[i] = (UINT8)name8[i];
  for (UINTN i = 0; i < 3; i++)
    ent[8 + i] = (UINT8)ext3[i];
  ent[11] = attr;

  // Cluster high/low words (little-endian)
  ent[20] = (UINT8)((cluster >> 16) & 0xFF);
  ent[21] = (UINT8)((cluster >> 24) & 0xFF);
  ent[26] = (UINT8)(cluster & 0xFF);
  ent[27] = (UINT8)((cluster >> 8) & 0xFF);

  ent[28] = (UINT8)(size & 0xFF);
  ent[29] = (UINT8)((size >> 8) & 0xFF);
  ent[30] = (UINT8)((size >> 16) & 0xFF);
  ent[31] = (UINT8)((size >> 24) & 0xFF);
}

static EFI_STATUS fat32_write_cluster(EFI_BLOCK_IO_PROTOCOL *bio,
                                      UINT64 data_start_lba, UINT8 spc,
                                      UINT32 bytes_per_cluster, UINT8 *scratch,
                                      UINT32 cluster, const UINT8 *data,
                                      UINTN data_len) {
  if (!bio || !bio->Media || !scratch)
    return EFI_INVALID_PARAMETER;
  if (bio->Media->BlockSize != 512)
    return EFI_UNSUPPORTED;
  if (cluster < 2)
    return EFI_INVALID_PARAMETER;
  if (bytes_per_cluster == 0)
    return EFI_INVALID_PARAMETER;

  for (UINTN i = 0; i < bytes_per_cluster; i++)
    scratch[i] = 0;
  if (data && data_len) {
    UINTN n = (data_len > bytes_per_cluster) ? bytes_per_cluster : data_len;
    for (UINTN i = 0; i < n; i++)
      scratch[i] = data[i];
  }

  UINT64 lba = data_start_lba + (UINT64)(cluster - 2U) * (UINT64)spc;
  return uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId, lba,
                           bytes_per_cluster, scratch);
}

static EFI_STATUS fat32_write_chain_contig(EFI_BLOCK_IO_PROTOCOL *bio,
                                           UINT64 data_start_lba, UINT8 spc,
                                           UINT32 bytes_per_cluster,
                                           UINT8 *scratch, UINT32 start_cluster,
                                           UINT32 clusters, const UINT8 *data,
                                           UINTN data_len) {
  if (!bio || !scratch)
    return EFI_INVALID_PARAMETER;
  for (UINT32 i = 0; i < clusters; i++) {
    UINTN off = (UINTN)i * (UINTN)bytes_per_cluster;
    const UINT8 *ptr = NULL;
    UINTN len = 0;
    if (off < data_len) {
      ptr = data + off;
      len = data_len - off;
      if (len > bytes_per_cluster)
        len = bytes_per_cluster;
    }
    EFI_STATUS stt =
        fat32_write_cluster(bio, data_start_lba, spc, bytes_per_cluster,
                            scratch, start_cluster + i, ptr, len);
    if (EFI_ERROR(stt))
      return stt;
  }
  return EFI_SUCCESS;
}

static EFI_STATUS fat32_write_volume(EFI_BLOCK_IO_PROTOCOL *bio,
                                     UINT64 part_lba, UINT64 total_sectors,
                                     const UINT8 *bootx64, UINTN bootx64_sz,
                                     const UINT8 *kernel, UINTN kernel_sz,
                                     const UINT8 *manifest, UINTN manifest_sz) {
  if (!bio || !bio->Media)
    return EFI_INVALID_PARAMETER;
  if (bio->Media->BlockSize != 512)
    return EFI_UNSUPPORTED;
  if (total_sectors < 65536)
    return EFI_INVALID_PARAMETER;
  if (total_sectors > 0xFFFFFFFFULL)
    return EFI_UNSUPPORTED;

  const UINT8 spc = 8; // 8 sectors/cluster
  const UINT16 reserved = 32;
  const UINT8 num_fats = 2;
  const UINT32 root_cluster = 2;
  const UINT32 bytes_per_cluster = (UINT32)spc * 512U;

  // Compute FAT size iteratively.
  UINT32 fat_size = 1;
  UINT32 cluster_count = 0;
  for (;;) {
    UINT64 data_sectors =
        total_sectors - reserved - (UINT64)num_fats * fat_size;
    cluster_count = (UINT32)(data_sectors / spc);
    UINT64 need_bytes = ((UINT64)cluster_count + 2ULL) * 4ULL;
    UINT32 new_fat = (UINT32)((need_bytes + 511ULL) / 512ULL);
    if (new_fat == fat_size)
      break;
    fat_size = new_fat;
  }
  if (cluster_count < 65525) {
    Print(L"[UEFI] ESP muito pequena para FAT32 (clusters=%u)\r\n",
          cluster_count);
    return EFI_INVALID_PARAMETER;
  }

  UINT64 fat_start_lba = part_lba + reserved;
  UINT64 data_start_lba = fat_start_lba + (UINT64)num_fats * fat_size;

  UINT32 fat_len = cluster_count + 2U;
  UINT32 *fat = AllocatePool((UINTN)fat_len * sizeof(UINT32));
  if (!fat)
    return EFI_OUT_OF_RESOURCES;
  for (UINT32 i = 0; i < fat_len; i++)
    fat[i] = 0;
  fat[0] = 0x0FFFFFF8U;
  fat[1] = 0x0FFFFFFFU;
  fat[root_cluster] = 0x0FFFFFFFU;
  UINT32 next_free = 3;

  UINT32 efi_cl = 0, efi_boot_cl = 0, bootdir_cl = 0;
  UINT32 efi_n = 0, efi_boot_n = 0, bootdir_n = 0;
  if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                          bytes_per_cluster, &efi_cl, &efi_n)) {
    FreePool(fat);
    return EFI_OUT_OF_RESOURCES;
  }
  if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                          bytes_per_cluster, &efi_boot_cl, &efi_boot_n)) {
    FreePool(fat);
    return EFI_OUT_OF_RESOURCES;
  }
  if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                          bytes_per_cluster, &bootdir_cl, &bootdir_n)) {
    FreePool(fat);
    return EFI_OUT_OF_RESOURCES;
  }

  UINT32 bootx64_cl = 0, kernel_cl = 0, manifest_cl = 0;
  UINT32 bootx64_need = 0, kernel_need = 0, manifest_need = 0;
  if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                          bootx64_sz, &bootx64_cl, &bootx64_need)) {
    FreePool(fat);
    return EFI_OUT_OF_RESOURCES;
  }
  if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                          kernel_sz, &kernel_cl, &kernel_need)) {
    FreePool(fat);
    return EFI_OUT_OF_RESOURCES;
  }
  if (manifest && manifest_sz) {
    if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                            manifest_sz, &manifest_cl, &manifest_need)) {
      FreePool(fat);
      return EFI_OUT_OF_RESOURCES;
    }
  }

  EFI_STATUS stt;

  // Boot sector
  UINT8 bs[512];
  for (UINTN i = 0; i < 512; i++)
    bs[i] = 0;
  bs[0] = 0xEB;
  bs[1] = 0x58;
  bs[2] = 0x90;
  // "MSWIN4.1"
  const char oem[8] = {'M', 'S', 'W', 'I', 'N', '4', '.', '1'};
  for (UINTN i = 0; i < 8; i++)
    bs[3 + i] = (UINT8)oem[i];
  bs[11] = 0x00;
  bs[12] = 0x02; // 512
  bs[13] = spc;
  bs[14] = (UINT8)(reserved & 0xFF);
  bs[15] = (UINT8)((reserved >> 8) & 0xFF);
  bs[16] = num_fats;
  bs[17] = 0;
  bs[18] = 0; // root entries
  bs[19] = 0;
  bs[20] = 0; // totsec16
  bs[21] = 0xF8;
  bs[22] = 0;
  bs[23] = 0; // fatsz16
  bs[24] = 63;
  bs[25] = 0;
  bs[26] = 255;
  bs[27] = 0;
  // hidden=0
  // totsec32
  UINT32 tot32 = (UINT32)total_sectors;
  bs[32] = (UINT8)(tot32 & 0xFF);
  bs[33] = (UINT8)((tot32 >> 8) & 0xFF);
  bs[34] = (UINT8)((tot32 >> 16) & 0xFF);
  bs[35] = (UINT8)((tot32 >> 24) & 0xFF);
  // fatsz32
  bs[36] = (UINT8)(fat_size & 0xFF);
  bs[37] = (UINT8)((fat_size >> 8) & 0xFF);
  bs[38] = (UINT8)((fat_size >> 16) & 0xFF);
  bs[39] = (UINT8)((fat_size >> 24) & 0xFF);
  // root cluster
  bs[44] = (UINT8)(root_cluster & 0xFF);
  bs[45] = (UINT8)((root_cluster >> 8) & 0xFF);
  bs[46] = (UINT8)((root_cluster >> 16) & 0xFF);
  bs[47] = (UINT8)((root_cluster >> 24) & 0xFF);
  // fsinfo=1, backup=6
  bs[48] = 1;
  bs[49] = 0;
  bs[50] = 6;
  bs[51] = 0;
  bs[64] = 0x80;
  bs[66] = 0x29;
  bs[67] = 0x78;
  bs[68] = 0x56;
  bs[69] = 0x34;
  bs[70] = 0x12;
  // label
  const char lab[11] = {'E', 'F', 'I', 'S', 'Y', 'S', 'P', 'A', 'R', 'T', ' '};
  for (UINTN i = 0; i < 11; i++)
    bs[71 + i] = (UINT8)lab[i];
  const char fst[8] = {'F', 'A', 'T', '3', '2', ' ', ' ', ' '};
  for (UINTN i = 0; i < 8; i++)
    bs[82 + i] = (UINT8)fst[i];
  bs[510] = 0x55;
  bs[511] = 0xAA;

  // FSInfo
  UINT8 fsinfo[512];
  for (UINTN i = 0; i < 512; i++)
    fsinfo[i] = 0;
  fsinfo[0] = 0x52;
  fsinfo[1] = 0x52;
  fsinfo[2] = 0x61;
  fsinfo[3] = 0x41;
  fsinfo[484] = 0x72;
  fsinfo[485] = 0x72;
  fsinfo[486] = 0x41;
  fsinfo[487] = 0x61;
  fsinfo[488] = 0xFF;
  fsinfo[489] = 0xFF;
  fsinfo[490] = 0xFF;
  fsinfo[491] = 0xFF;
  fsinfo[492] = 0xFF;
  fsinfo[493] = 0xFF;
  fsinfo[494] = 0xFF;
  fsinfo[495] = 0xFF;
  fsinfo[508] = 0x00;
  fsinfo[509] = 0x00;
  fsinfo[510] = 0x55;
  fsinfo[511] = 0xAA;

  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          part_lba + 0, 512, bs);
  if (EFI_ERROR(stt)) {
    FreePool(fat);
    return stt;
  }
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          part_lba + 1, 512, fsinfo);
  if (EFI_ERROR(stt)) {
    FreePool(fat);
    return stt;
  }
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          part_lba + 6, 512, bs);
  if (EFI_ERROR(stt)) {
    FreePool(fat);
    return stt;
  }
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          part_lba + 7, 512, fsinfo);
  if (EFI_ERROR(stt)) {
    FreePool(fat);
    return stt;
  }

  // Write FAT1/FAT2
  UINTN fat_bytes = (UINTN)fat_size * 512U;
  UINT8 *fatbuf = AllocatePool(fat_bytes);
  if (!fatbuf) {
    FreePool(fat);
    return EFI_OUT_OF_RESOURCES;
  }
  for (UINTN i = 0; i < fat_bytes; i++)
    fatbuf[i] = 0;
  for (UINT32 i = 0; i < cluster_count + 2U; i++) {
    UINT32 v = fat[i] & 0x0FFFFFFF;
    UINTN off = (UINTN)i * 4U;
    fatbuf[off + 0] = (UINT8)(v & 0xFF);
    fatbuf[off + 1] = (UINT8)((v >> 8) & 0xFF);
    fatbuf[off + 2] = (UINT8)((v >> 16) & 0xFF);
    fatbuf[off + 3] = (UINT8)((v >> 24) & 0xFF);
  }
  FreePool(fat);
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          fat_start_lba, fat_bytes, fatbuf);
  if (EFI_ERROR(stt)) {
    FreePool(fatbuf);
    return stt;
  }
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          fat_start_lba + fat_size, fat_bytes, fatbuf);
  FreePool(fatbuf);
  if (EFI_ERROR(stt))
    return stt;

  UINT8 *scratch = AllocatePool(bytes_per_cluster);
  if (!scratch)
    return EFI_OUT_OF_RESOURCES;

  UINT8 *root = AllocatePool(bytes_per_cluster);
  UINT8 *efi_dir = AllocatePool(bytes_per_cluster);
  UINT8 *efi_boot_dir = AllocatePool(bytes_per_cluster);
  UINT8 *boot_dir = AllocatePool(bytes_per_cluster);
  if (!root || !efi_dir || !efi_boot_dir || !boot_dir) {
    if (root)
      FreePool(root);
    if (efi_dir)
      FreePool(efi_dir);
    if (efi_boot_dir)
      FreePool(efi_boot_dir);
    if (boot_dir)
      FreePool(boot_dir);
    FreePool(scratch);
    return EFI_OUT_OF_RESOURCES;
  }
  for (UINTN i = 0; i < bytes_per_cluster; i++) {
    root[i] = 0;
    efi_dir[i] = 0;
    efi_boot_dir[i] = 0;
    boot_dir[i] = 0;
  }

  fat32_dirent83(&root[0], "EFI     ", "   ", 0x10, efi_cl, 0);
  fat32_dirent83(&root[32], "BOOT    ", "   ", 0x10, bootdir_cl, 0);

  fat32_dirent83(&efi_dir[0], ".       ", "   ", 0x10, efi_cl, 0);
  fat32_dirent83(&efi_dir[32], "..      ", "   ", 0x10, root_cluster, 0);
  fat32_dirent83(&efi_dir[64], "BOOT    ", "   ", 0x10, efi_boot_cl, 0);

  fat32_dirent83(&efi_boot_dir[0], ".       ", "   ", 0x10, efi_boot_cl, 0);
  fat32_dirent83(&efi_boot_dir[32], "..      ", "   ", 0x10, efi_cl, 0);
  fat32_dirent83(&efi_boot_dir[64], "BOOTX64 ", "EFI", 0x20, bootx64_cl,
                 (UINT32)bootx64_sz);

  fat32_dirent83(&boot_dir[0], ".       ", "   ", 0x10, bootdir_cl, 0);
  fat32_dirent83(&boot_dir[32], "..      ", "   ", 0x10, root_cluster, 0);
  fat32_dirent83(&boot_dir[64], "NOIROS64", "BIN", 0x20, kernel_cl,
                 (UINT32)kernel_sz);
  if (manifest && manifest_sz) {
    fat32_dirent83(&boot_dir[96], "MANIFEST", "BIN", 0x20, manifest_cl,
                   (UINT32)manifest_sz);
  }

  stt = fat32_write_cluster(bio, data_start_lba, spc, bytes_per_cluster,
                            scratch, root_cluster, root, bytes_per_cluster);
  if (!EFI_ERROR(stt))
    stt = fat32_write_cluster(bio, data_start_lba, spc, bytes_per_cluster,
                              scratch, efi_cl, efi_dir, bytes_per_cluster);
  if (!EFI_ERROR(stt))
    stt = fat32_write_cluster(bio, data_start_lba, spc, bytes_per_cluster,
                              scratch, efi_boot_cl, efi_boot_dir,
                              bytes_per_cluster);
  if (!EFI_ERROR(stt))
    stt = fat32_write_cluster(bio, data_start_lba, spc, bytes_per_cluster,
                              scratch, bootdir_cl, boot_dir, bytes_per_cluster);
  FreePool(root);
  FreePool(efi_dir);
  FreePool(efi_boot_dir);
  FreePool(boot_dir);
  if (EFI_ERROR(stt)) {
    FreePool(scratch);
    return stt;
  }

  stt = fat32_write_chain_contig(bio, data_start_lba, spc, bytes_per_cluster,
                                 scratch, bootx64_cl, bootx64_need, bootx64,
                                 bootx64_sz);
  if (!EFI_ERROR(stt))
    stt = fat32_write_chain_contig(bio, data_start_lba, spc, bytes_per_cluster,
                                   scratch, kernel_cl, kernel_need, kernel,
                                   kernel_sz);
  if (!EFI_ERROR(stt) && manifest && manifest_sz)
    stt = fat32_write_chain_contig(bio, data_start_lba, spc, bytes_per_cluster,
                                   scratch, manifest_cl, manifest_need,
                                   manifest, manifest_sz);
  FreePool(scratch);
  if (EFI_ERROR(stt))
    return stt;

  uefi_call_wrapper(bio->FlushBlocks, 1, bio);
  return EFI_SUCCESS;
}

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

static EFI_STATUS installer_run(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
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

  stt = read_file(root, L"BOOT\\NOIROS64.BIN", &kernel_buf, &kernel_sz);
  if (EFI_ERROR(stt) && kernel_buf) {
    FreePool(kernel_buf);
    kernel_buf = NULL;
    kernel_sz = 0;
  }
  if (EFI_ERROR(stt))
    stt = read_file(root, L"\\BOOT\\NOIROS64.BIN", &kernel_buf, &kernel_sz);
  if (EFI_ERROR(stt) && kernel_buf) {
    FreePool(kernel_buf);
    kernel_buf = NULL;
    kernel_sz = 0;
  }
  if (EFI_ERROR(stt))
    stt = read_file(root, L"\\boot\\noiros64.bin", &kernel_buf, &kernel_sz);
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
  Print(L"   NoirOS 64-bit - Assistente de Instalacao\r\n");
  Print(L"========================================\r\n");
  Print(L"\r\n");
  Print(L"Disco destino: %lu MiB\r\n", (disk_bytes / (1024ULL * 1024ULL)));
  Print(L"\r\n");
  Print(L"[AVISO] TODOS OS DADOS SERAO APAGADOS!\r\n");
  Print(L"\r\n");
  Print(L"Pressione 'I' para iniciar ou outra tecla para cancelar: ");

  EFI_INPUT_KEY key;
  UINTN idx = 0;
  uefi_call_wrapper(st->BootServices->WaitForEvent, 3, 1,
                    &st->ConIn->WaitForKey, &idx);
  stt = uefi_call_wrapper(st->ConIn->ReadKeyStroke, 2, st->ConIn, &key);
  if (EFI_ERROR(stt) || (key.UnicodeChar != L'I' && key.UnicodeChar != L'i')) {
    Print(L"\r\n[UEFI] Instalacao cancelada.\r\n");
    return EFI_ABORTED;
  }
  Print(L"\r\n\r\n");

  /* --- Step 1: Admin Username --- */
  Print(L"=== Configuracao do Administrador ===\r\n\r\n");

  CHAR16 admin_user[64];
  Print(L"Nome de usuario admin: ");
  UINTN user_len = uefi_readline(st, admin_user, 64, FALSE);
  if (user_len == 0) {
    /* Default to 'admin' */
    admin_user[0] = L'a';
    admin_user[1] = L'd';
    admin_user[2] = L'm';
    admin_user[3] = L'i';
    admin_user[4] = L'n';
    admin_user[5] = 0;
    Print(L"(usando padrao: admin)\r\n");
  }

  /* --- Step 2: Admin Password --- */
  CHAR16 admin_pass[128], admin_pass2[128];
  for (;;) {
    Print(L"Senha do administrador: ");
    UINTN pass_len = uefi_readline(st, admin_pass, 128, TRUE);
    if (pass_len < 4) {
      Print(L"Senha muito curta (minimo 4 caracteres).\r\n");
      continue;
    }

    Print(L"Confirmar senha: ");
    UINTN pass2_len = uefi_readline(st, admin_pass2, 128, TRUE);

    if (pass_len != pass2_len) {
      Print(L"Senhas nao conferem. Tente novamente.\r\n");
      continue;
    }

    BOOLEAN match = TRUE;
    for (UINTN i = 0; i < pass_len; i++) {
      if (admin_pass[i] != admin_pass2[i]) {
        match = FALSE;
        break;
      }
    }
    if (!match) {
      Print(L"Senhas nao conferem. Tente novamente.\r\n");
      continue;
    }
    break;
  }

  /* --- Step 3: Generate and display recovery key --- */
  Print(L"\r\n=== Chave de Recuperacao ===\r\n\r\n");
  CHAR16 recovery_key[32];
  generate_recovery_key(st, recovery_key, 32);
  Print(L"ANOTE ESTA CHAVE EM LOCAL SEGURO:\r\n\r\n");
  Print(L"    %s\r\n\r\n", recovery_key);
  Print(L"Esta chave permite recuperar dados em caso de\r\n");
  Print(L"esquecimento da senha. Guarde-a com cuidado!\r\n\r\n");
  Print(L"Pressione ENTER para continuar...");
  uefi_readline(st, admin_pass2, 2, FALSE); /* Reuse buffer for confirmation */
  Print(L"\r\n");

  /* --- Step 4: Confirm installation --- */
  Print(L"=== Confirmacao Final ===\r\n\r\n");
  Print(L"Usuario: %s\r\n", admin_user);
  Print(L"Disco: %lu MiB (sera APAGADO)\r\n",
        (disk_bytes / (1024ULL * 1024ULL)));
  Print(L"\r\nConfirmar instalacao? [S/n]: ");
  CHAR16 confirm[8];
  uefi_readline(st, confirm, 8, FALSE);
  if (confirm[0] == L'n' || confirm[0] == L'N') {
    Print(L"[UEFI] Instalacao cancelada pelo usuario.\r\n");
    return EFI_ABORTED;
  }
  Print(L"\r\n");

  // Wipe head/tail (2 MiB) to clear stale boot records + GPT backup.
  const UINT64 wipe_span = 4096ULL;
  Print(L"[UEFI] Limpando cabecalho/rodape...\r\n");
  wipe_blocks(disk, 0, wipe_span);
  if (disk->Media->LastBlock + 1ULL > wipe_span) {
    UINT64 tail_start = (UINT64)disk->Media->LastBlock + 1ULL - wipe_span;
    wipe_blocks(disk, tail_start, wipe_span);
  }

  UINT64 esp_lba = 0, esp_secs = 0, boot_lba = 0, boot_secs = 0;
  Print(L"[UEFI] Gravando GPT...\r\n");
  stt = gpt_write_layout(st, disk, INSTALL_ESP_SIZE_MIB, INSTALL_BOOT_SIZE_MIB,
                         &esp_lba, &esp_secs, &boot_lba, &boot_secs);
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] GPT falhou: %r\r\n", stt);
    return stt;
  }

  // Build manifest for BOOT partition: manifest@0, kernel@+1
  struct boot_manifest mf;
  UINT32 ksec = (UINT32)((kernel_sz + 511U) / 512U);
  UINT32 cksum = checksum32_words((const UINT8 *)kernel_buf, kernel_sz);
  build_manifest(&mf, 1, ksec, cksum);

  Print(L"[UEFI] Criando ESP (FAT32) e copiando arquivos...\r\n");
  stt = fat32_write_volume(disk, esp_lba, esp_secs, (const UINT8 *)bootx64_buf,
                           bootx64_sz, (const UINT8 *)kernel_buf, kernel_sz,
                           (const UINT8 *)&mf, sizeof(mf));
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] FAT32/ESP falhou: %r\r\n", stt);
    return stt;
  }

  Print(L"[UEFI] Gravando BOOT (manifest+kernel)...\r\n");
  stt = write_boot_partition_raw(disk, boot_lba, boot_secs, &mf,
                                 (const UINT8 *)kernel_buf, kernel_sz);
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] BOOT raw falhou: %r\r\n", stt);
    return stt;
  }

  uefi_call_wrapper(disk->FlushBlocks, 1, disk);

  Print(L"[UEFI] Instalacao concluida. Reiniciando...\r\n");
  uefi_call_wrapper(st->RuntimeServices->ResetSystem, 4, EfiResetCold,
                    EFI_SUCCESS, 0, NULL);
  return EFI_SUCCESS;
}

typedef struct {
  CHAR8 signature[8]; /* "RSD PTR " */
  UINT8 checksum;
  CHAR8 oemid[6];
  UINT8 revision;
  UINT32 rsdt;
  UINT32 length;
  UINT64 xsdt;
  UINT8 ext_checksum;
  UINT8 reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

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
      // como mínimo.
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

  // Área BIOS "high" tradicional: 0xE0000..0xFFFFF
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
      out->root->Open, 5, out->root, &fh, L"\\EFI\\NOIROS.LOG",
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

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab) {
  InitializeLib(image, systab);
  Print(L"NoirOS UEFI loader: iniciando\r\n");

  // Modo instalador: ISO de instalacao contem um marcador (NOIROS.INI) e/ou o
  // volume e read-only.
  BOOLEAN install_marker = boot_volume_has_marker(image, systab);
  BOOLEAN install_ro = boot_volume_is_readonly(image, systab);
  if (install_marker || install_ro) {
    Print(L"[UEFI] Modo instalador detectado (marker=%d readonly=%d)\r\n",
          install_marker ? 1 : 0, install_ro ? 1 : 0);
    EFI_STATUS ist = installer_run(image, systab);
    if (!EFI_ERROR(ist)) {
      // Normalmente não retorna (ResetSystem). Se retornar, apenas aguarde.
      uefi_call_wrapper(systab->BootServices->Stall, 1, 5 * 1000 * 1000);
    }
    // Se a instalação foi cancelada ou falhou, continue para o boot normal
    // (kernel do ISO).
  }

  EFI_PHYSICAL_ADDRESS entry = 0;
  EFI_STATUS st = load_kernel(image, systab, &entry);
  if (EFI_ERROR(st)) {
    Print(L"[UEFI] Falha ao carregar kernel: %r\r\n", st);
    // Não retorne ao firmware em caso de erro: isso vira "boot loader failed"
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

  // Alocar handoff e memory map abaixo de 1GiB (compatível com identity map do
  // kernel atual)
  log_file_t logf = {0};
  EFI_STATUS logst = log_open(image, systab, &logf);
  if (EFI_ERROR(logst)) {
    Print(L"[UEFI] log_open falhou: %r\r\n", logst);
  } else {
    log_write_ascii(&logf, "\r\n[noiros] boot start\r\n");
    log_write_ascii(&logf, "[noiros] kernel entry=");
    log_write_u64_hex(&logf, (UINT64)entry);
    log_write_ascii(&logf, "\r\n[noiros] rsdp=");
    log_write_u64_hex(&logf, rsdp);
    log_write_ascii(&logf, "\r\n[noiros] rsdp.src=");
    log_write_ascii(&logf, rsdp_src8);
    log_write_ascii(&logf, "\r\n[noiros] rsdp.copied=");
    log_write_u64_hex(&logf, rsdp_copied ? 1 : 0);
    log_write_ascii(&logf, "\r\n[noiros] fb.base=");
    log_write_u64_hex(&logf, fb.base);
    log_write_ascii(&logf, "\r\n");

    log_write_ascii(&logf, "[noiros] rsdp.valid=");
    log_write_u64_hex(&logf,
                      rsdp_is_valid_ptr((const VOID *)(UINTN)rsdp) ? 1 : 0);
    log_write_ascii(&logf, "\r\n");
    if (rsdp && rsdp_is_valid_ptr((const VOID *)(UINTN)rsdp)) {
      const UINT8 *p = (const UINT8 *)(UINTN)rsdp;
      const acpi_rsdp_t *r = (const acpi_rsdp_t *)(UINTN)rsdp;
      log_write_ascii(&logf, "[noiros] rsdp.rev=");
      log_write_u64_hex(&logf, (UINT64)r->revision);
      log_write_ascii(&logf, " chk=");
      log_write_u64_hex(&logf, (UINT64)r->checksum);
      log_write_ascii(&logf, " rsdt=");
      log_write_u64_hex(&logf, (UINT64)r->rsdt);
      log_write_ascii(&logf, "\r\n");
      log_write_ascii(&logf, "[noiros] rsdp.sum20=");
      log_write_u64_hex(&logf, (UINT64)sum8_bytes(p, 20));
      log_write_ascii(&logf, " bytes20=");
      log_write_bytes_hex(&logf, p, 20);

      if (r->revision >= 2) {
        UINT32 len = r->length;
        if (len < 36 || len > 4096)
          len = 36;
        log_write_ascii(&logf, "[noiros] rsdp.len=");
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
    // Qualquer I/O/alloc após GetMemoryMap pode alterar o map_key e causar
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

    // Importante: não chame mais nada que possa alocar/IO entre GetMemoryMap e
    // ExitBootServices.

    /* HYBRID BOOT: Skip ExitBootServices to keep UEFI ConIn available for
     * keyboard input */
    /* st = uefi_call_wrapper(systab->BootServices->ExitBootServices, 2, image,
     * map_key); */
    st = EFI_SUCCESS; /* Pretend success, don't exit boot services */
    if (!EFI_ERROR(st)) {
      break;
    }

    // map_key ficou inválido (firmware mudou o memory map). Libera o buffer e
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
    // Não retorne ao firmware (isso vira PXE/boot fail confuso no Hyper-V).
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
  handoff->efi_system_table = (UINT64)(UINTN)systab;

#if 0
    /* HYBRID BOOT: Keep Boot Services active for keyboard input support! */
    /* NoirOS kernel will call UEFI services for input until native driver is ready. */
    st = uefi_call_wrapper(systab->BootServices->ExitBootServices, 2, image, map_key);
    /* ... error handling code ... */
#endif
  Print(L"[UEFI] Hybrid Boot: Boot Services kept active. Jumping to "
        L"kernel...\r\n");

  void (*kentry)(struct boot_handoff *) =
      (void (*)(struct boot_handoff *))(UINTN)entry;
  dbgcon_putc('J');
  kentry(handoff);

  for (;;) {
    __asm__ __volatile__("hlt");
  }
}
