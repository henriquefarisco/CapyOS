#ifndef UEFI_LOADER_INTERNAL_H
#define UEFI_LOADER_INTERNAL_H

#include "boot/boot_config.h"
#include "boot/boot_manifest.h"
#include "boot/boot_slot.h"
#include "boot/boot_slot_store.h"
#include "boot/handoff.h"
#include "core/version.h"
#include "security/sha256.h"
#include "boot/installer_disk_policy.h"
#include "boot/gpt_types.h"
#include "boot/gpt_identity.h"

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

#define DEBUGCON_PORT 0xE9
#define ELF_MAGIC 0x464C457F
#define PT_LOAD 1
#define EM_X86_64 62
#define KERNEL_MAX_PHDRS 32
#define KERNEL_BLOCK_SCRATCH_MAX 4096
#define KERNEL_FIXED_RESERVE_BASE 0x10000000ULL
#define KERNEL_FIXED_RESERVE_BYTES (48ULL * 1024ULL * 1024ULL)

#define GPT_HEADER_LBA 1
#define GPT_SIG 0x5452415020494645ULL
#define EFI_PART_TYPE_ESP CAPYOS_GPT_TYPE_ESP_BYTES
#define EFI_PART_TYPE_CAPYOS_BOOT CAPYOS_GPT_TYPE_BOOT_BYTES
#define EFI_PART_TYPE_LINUX_FS CAPYOS_GPT_TYPE_DATA_BYTES

#define DP_TYPE_MEDIA 0x04
#define DP_SUBTYPE_HARDDRIVE 0x01
#define DP_SUBTYPE_CDROM 0x02
#define DP_TYPE_END 0x7F
#define DP_SUBTYPE_END_ENTIRE 0xFF

#define INSTALL_ALIGN_LBA INSTALLER_DISK_ALIGN_LBA
#define INSTALL_ESP_SIZE_MIB INSTALLER_DISK_ESP_SIZE_MIB
#define INSTALL_BOOT_SIZE_MIB INSTALLER_DISK_BOOT_SIZE_MIB

#define GPT_REVISION 0x00010000U
#define GPT_HEADER_SIZE 92U
#define GPT_NUM_ENTRIES 128U
#define GPT_ENTRY_SIZE 128U
#define GPT_ENTRIES_LBA 2ULL
#define GPT_ENTRIES_SECTORS INSTALLER_DISK_GPT_ENTRIES_SECTORS
#if ((GPT_NUM_ENTRIES * GPT_ENTRY_SIZE) / 512U) != GPT_ENTRIES_SECTORS
#error "installer disk policy GPT entry span mismatch"
#endif

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

typedef enum {
  INSTALLER_LANG_EN = 0,
  INSTALLER_LANG_PT_BR = 1,
  INSTALLER_LANG_ES = 2,
} installer_language_t;

typedef struct {
  EFI_HANDLE handle;
  EFI_BLOCK_IO_PROTOCOL *bio;
  UINT32 media_id;
  UINT64 path_id;
  struct installer_disk_geometry geometry;
  struct installer_disk_layout layout;
} installer_disk_target_t;

typedef struct {
  CHAR8 signature[8];
  UINT8 checksum;
  CHAR8 oemid[6];
  UINT8 revision;
  UINT32 rsdt;
  UINT32 length;
  UINT64 xsdt;
  UINT8 ext_checksum;
  UINT8 reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef EFI_STATUS (*kernel_read_fn_t)(void *ctx, UINT64 offset, VOID *buf,
                                       UINTN len);

struct kernel_buffer_reader {
  const UINT8 *base;
  UINT64 size;
};

struct uefi_boot_store_context {
  EFI_BLOCK_IO_PROTOCOL *bio;
  UINT32 media_id;
  UINT64 boot_lba;
  UINT32 boot_sectors;
};

int uefi_boot_store_read(void *opaque, uint32_t lba,
                         uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE]);
int uefi_boot_store_write(
    void *opaque, uint32_t lba,
    const uint8_t sector[BOOT_SLOT_STORE_SECTOR_SIZE]);
int uefi_boot_store_flush(void *opaque);
int uefi_slot_bytes_equal(const UINT8 *a, const UINT8 *b, UINTN len);

struct kernel_block_reader {
  EFI_BLOCK_IO_PROTOCOL *bio;
  UINT64 base_lba;
  UINT32 block_size;
  UINT64 size;
};

struct kernel_file_reader {
  EFI_FILE_HANDLE file;
  UINT64 size;
};

struct kernel_load_plan {
  EFI_PHYSICAL_ADDRESS link_base;
  EFI_PHYSICAL_ADDRESS link_end;
  UINT64 entry;
  UINTN span;
  UINTN pages;
};

typedef struct {
  EFI_FILE_HANDLE root;
  EFI_FILE_HANDLE file;
} log_file_t;

static inline void dbgcon_putc(UINT8 c) {
  __asm__ __volatile__("outb %0, %1" : : "a"(c), "Nd"((UINT16)DEBUGCON_PORT));
}

/* Post-ExitBootServices tracing: `Print` needs BootServices, so the only way to
 * observe a value in the window between the memory-map handoff and the kernel's
 * first instruction is the debug console port. Emits 16 upper-case nibbles. */
static inline void dbgcon_hex64(UINT64 value) {
  for (int shift = 60; shift >= 0; shift -= 4) {
    UINT8 nibble = (UINT8)((value >> shift) & 0xFu);
    dbgcon_putc((UINT8)(nibble < 10u ? ('0' + nibble) : ('A' + (nibble - 10u))));
  }
}

extern struct boot_config_sector g_runtime_boot_cfg;
extern BOOLEAN g_runtime_boot_cfg_valid;
extern EFI_PHYSICAL_ADDRESS g_kernel_reserved_base;
extern UINTN g_kernel_reserved_pages;
extern UINT8 g_kernel_block_scratch[KERNEL_BLOCK_SCRATCH_MAX];

BOOLEAN guid_eq(const UINT8 *a, const UINT8 *b);
EFI_STATUS read_file(EFI_FILE_HANDLE root, CHAR16 *path, VOID **buf,
                     UINTN *size);
void bootcfg_clear(struct boot_config_sector *cfg);
void char16_to_ascii(char *out, UINTN out_len, const CHAR16 *in);
int ascii_streq(const char *a, const char *b);
int normalize_key_char16(const CHAR16 *in, char *out, UINTN out_len);
EFI_STATUS load_boot_config_from_root(EFI_FILE_HANDLE root);
EFI_STATUS open_file_read(EFI_FILE_HANDLE root, CHAR16 *path,
                          EFI_FILE_HANDLE *out, UINTN *size);
EFI_STATUS uefi_block_io_allocate_aligned(EFI_BLOCK_IO_PROTOCOL *bio,
                                          UINTN bytes,
                                          VOID **out_allocation,
                                          VOID **out_buffer);
EFI_STATUS uefi_block_io_read(EFI_BLOCK_IO_PROTOCOL *bio,
                              UINT32 expected_media_id, UINT64 lba,
                              UINTN bytes, VOID *buffer);
EFI_STATUS uefi_block_io_write(EFI_BLOCK_IO_PROTOCOL *bio,
                               UINT32 expected_media_id, UINT64 lba,
                               UINTN bytes, const VOID *buffer);
EFI_STATUS uefi_block_io_flush(EFI_BLOCK_IO_PROTOCOL *bio,
                               UINT32 expected_media_id);

EFI_STATUS kernel_read_from_blocks(void *ctx, UINT64 offset, VOID *buf,
                                   UINTN len);
EFI_STATUS kernel_read_from_file(void *ctx, UINT64 offset, VOID *buf,
                                 UINTN len);
EFI_STATUS kernel_reserve_fixed_window(EFI_SYSTEM_TABLE *st);
void kernel_release_fixed_window(EFI_SYSTEM_TABLE *st);
EFI_STATUS load_kernel_from_reader(EFI_SYSTEM_TABLE *st,
                                   kernel_read_fn_t reader, void *ctx,
                                   UINTN size, EFI_PHYSICAL_ADDRESS *entry);
EFI_STATUS validate_kernel_buffer(VOID *kernel_buf, UINTN kernel_size);
EFI_STATUS load_kernel_from_buffer(EFI_SYSTEM_TABLE *st, VOID *kernel_buf,
                                   UINTN kernel_size,
                                   EFI_PHYSICAL_ADDRESS *entry);

EFI_STATUS load_kernel_streaming(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
                                 EFI_PHYSICAL_ADDRESS *entry_out);
EFI_STATUS load_kernel_from_ab_store(
    EFI_SYSTEM_TABLE *st, EFI_BLOCK_IO_PROTOCOL *bio,
    UINT32 expected_media_id, const struct boot_disk_identity *identity,
    EFI_PHYSICAL_ADDRESS *entry_out,
    struct boot_slot_attempt_handoff *out_attempt);

const CHAR16 *installer_language_code(installer_language_t language);
const CHAR16 *installer_language_name(installer_language_t language);
UINT64 align_up_u64(UINT64 v, UINT64 a);
UINT32 checksum32_words(const UINT8 *data, UINTN len);
VOID build_manifest(struct boot_manifest *m, UINT32 kernel_lba,
                    UINT32 kernel_sectors, UINT32 cksum32);
EFI_STATUS open_boot_volume(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
                            EFI_HANDLE *out_fs_handle,
                            EFI_FILE_HANDLE *out_root);
BOOLEAN boot_volume_is_readonly(EFI_HANDLE image, EFI_SYSTEM_TABLE *st);
BOOLEAN boot_volume_has_marker(EFI_HANDLE image, EFI_SYSTEM_TABLE *st);
BOOLEAN boot_device_is_cdrom(EFI_HANDLE image, EFI_SYSTEM_TABLE *st);
EFI_STATUS choose_target_disk(EFI_SYSTEM_TABLE *st,
                              installer_disk_target_t *out_target);
EFI_STATUS installer_revalidate_target(
    EFI_SYSTEM_TABLE *st, const installer_disk_target_t *target);
EFI_STATUS gpt_find_capyos_data_partition(EFI_BLOCK_IO_PROTOCOL *bio,
                                          UINT64 *out_data_start,
                                          UINT64 *out_data_count,
                                          UINT64 *out_esp_start,
                                          UINT64 *out_esp_count,
                                          struct boot_disk_identity *out_identity);
EFI_STATUS choose_runtime_disk_with_data(EFI_HANDLE image,
                                         UINT32 *out_raw_media_id,
                                         EFI_SYSTEM_TABLE *st,
                                         EFI_BLOCK_IO_PROTOCOL **bio_out,
                                         UINT64 *out_data_start,
                                         UINT64 *out_data_count,
                                         EFI_BLOCK_IO_PROTOCOL **out_raw_bio,
                                         UINT64 *out_raw_data_start,
                                         UINT64 *out_raw_data_count,
                                         struct boot_disk_identity *out_identity);

void disable_uefi_watchdog(EFI_SYSTEM_TABLE *st);
void uefi_installer_serial_init(void);
void uefi_installer_serial_write(const char *text);
void uefi_installer_serial_write_u64(UINT64 value);
int uefi_installer_read_key(EFI_SYSTEM_TABLE *st, EFI_INPUT_KEY *key);
UINTN uefi_readline(EFI_SYSTEM_TABLE *st, CHAR16 *buf, UINTN maxlen,
                    BOOLEAN hidden);
void generate_recovery_key(EFI_SYSTEM_TABLE *st, CHAR16 *key_out,
                           UINTN key_chars);
EFI_STATUS wipe_blocks(EFI_BLOCK_IO_PROTOCOL *bio, UINT32 expected_media_id,
                       UINT64 start_lba, UINT64 count);
EFI_STATUS scrub_data_partition_for_first_boot(EFI_BLOCK_IO_PROTOCOL *bio,
                                               UINT32 expected_media_id,
                                               UINT64 data_start,
                                               UINT64 data_sectors);
EFI_STATUS gpt_write_layout(EFI_SYSTEM_TABLE *st, EFI_BLOCK_IO_PROTOCOL *bio,
                            UINT32 expected_media_id,
                            const struct installer_disk_layout *layout,
                            UINT64 *out_esp_lba,
                            UINT64 *out_esp_sectors,
                            UINT64 *out_boot_lba,
                            UINT64 *out_boot_sectors,
                            UINT64 *out_data_lba,
                            UINT64 *out_data_sectors);

EFI_STATUS fat32_write_volume(EFI_BLOCK_IO_PROTOCOL *bio,
                              UINT32 expected_media_id, UINT64 start_lba,
                              UINT64 total_sectors, const UINT8 *efi_payload,
                              UINTN efi_size, const UINT8 *kernel_payload,
                              UINTN kernel_size, const UINT8 *manifest,
                              UINTN manifest_size, const UINT8 *boot_cfg,
                              UINTN boot_cfg_size);
EFI_STATUS installer_run(EFI_HANDLE image, EFI_SYSTEM_TABLE *st);

UINT8 sum8_bytes(const UINT8 *p, UINTN len);
BOOLEAN rsdp_is_valid_ptr(const VOID *ptr);
EFI_STATUS scan_rsdp(UINT64 *out_rsdp);
EFI_STATUS find_rsdp_memmap(EFI_SYSTEM_TABLE *st, UINT64 *out_rsdp);
EFI_STATUS find_rsdp(EFI_SYSTEM_TABLE *st, UINT64 *out_rsdp);
EFI_STATUS copy_rsdp_low(EFI_SYSTEM_TABLE *st, UINT64 rsdp,
                         UINT64 *out_low_addr);
void log_close(log_file_t *lf);
EFI_STATUS log_open(EFI_HANDLE image, EFI_SYSTEM_TABLE *st, log_file_t *out);
void log_write_ascii(log_file_t *lf, const char *s);
void log_write_u64_hex(log_file_t *lf, UINT64 v);
void log_write_bytes_hex(log_file_t *lf, const UINT8 *p, UINTN len);
EFI_STATUS get_gop(EFI_SYSTEM_TABLE *st,
                   EFI_GRAPHICS_OUTPUT_PROTOCOL **gop_out);

#endif
