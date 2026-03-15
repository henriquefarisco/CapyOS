#include "arch/x86_64/storage_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "drivers/storage/ahci.h"
#include "drivers/nvme.h"
#include "fs/capyfs.h"
#include "memory/kmem.h"

static struct efi_block_device g_efi_runtime_disk;
static struct efi_block_device g_efi_runtime_disk_alt;
static enum x64_storage_backend g_storage_backend = X64_STORAGE_BACKEND_NONE;
static enum x64_storage_backend g_storage_native_backend = X64_STORAGE_BACKEND_NONE;
static const char *g_storage_data_path = "none";
static const char *g_storage_native_data_path = "none";
static int g_storage_has_device = 0;
static int g_storage_native_ready = 0;
static struct block_device *g_storage_native_device = NULL;

#define GPT_HEADER_LBA_K 1ULL
#define GPT_SIG_K 0x5452415020494645ULL

static const uint8_t k_gpt_linux_fs_guid[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};

struct gpt_header_k {
  uint64_t signature;
  uint32_t revision;
  uint32_t header_size;
  uint32_t header_crc32;
  uint32_t reserved;
  uint64_t current_lba;
  uint64_t backup_lba;
  uint64_t first_usable_lba;
  uint64_t last_usable_lba;
  uint8_t disk_guid[16];
  uint64_t part_entry_lba;
  uint32_t num_part_entries;
  uint32_t part_entry_size;
  uint32_t part_entries_crc32;
} __attribute__((packed));

struct gpt_entry_k {
  uint8_t part_type_guid[16];
  uint8_t uniq_guid[16];
  uint64_t first_lba;
  uint64_t last_lba;
  uint64_t attrs;
  uint16_t name[36];
} __attribute__((packed));

static void io_print(const struct x64_storage_runtime_io *io,
                     const char *message) {
  if (io && io->print && message) {
    io->print(message);
  }
}

static void io_print_hex64(const struct x64_storage_runtime_io *io,
                           uint64_t value) {
  if (io && io->print_hex64) {
    io->print_hex64(value);
  }
}

static void io_print_dec_u32(const struct x64_storage_runtime_io *io,
                             uint32_t value) {
  if (io && io->print_dec_u32) {
    io->print_dec_u32(value);
  }
}

static void io_putc(const struct x64_storage_runtime_io *io, char ch) {
  if (io && io->putc) {
    io->putc(ch);
  }
}

static int guid_eq_local(const uint8_t *a, const uint8_t *b) {
  if (!a || !b) {
    return 0;
  }
  for (uint32_t i = 0; i < 16u; ++i) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

static int gpt_name_equals_ascii(const uint16_t *name, const char *ascii) {
  if (!name || !ascii) {
    return 0;
  }
  for (uint32_t i = 0; i < 36u; ++i) {
    char ch = ascii[i];
    if (ch == '\0') {
      return name[i] == 0;
    }
    if ((uint16_t)(uint8_t)ch != name[i]) {
      return 0;
    }
  }
  return 0;
}

static int compute_effective_data_count(uint64_t data_start, uint64_t data_count,
                                        uint64_t disk_last_lba,
                                        uint64_t *out_effective_count) {
  if (!out_effective_count) {
    return -1;
  }
  *out_effective_count = 0;
  if (data_start > 0xFFFFFFFFULL || data_count > 0xFFFFFFFFULL) {
    return -1;
  }
  if (data_start + data_count < data_start) {
    return -1;
  }
  if (data_start > disk_last_lba) {
    return -1;
  }

  {
    uint64_t max_data_count = (disk_last_lba - data_start) + 1ULL;
    uint64_t effective_data_count = data_count;
    if (effective_data_count > max_data_count) {
      effective_data_count = max_data_count;
    }
    if (effective_data_count == 0 || disk_last_lba > 0xFFFFFFFFULL) {
      return -1;
    }
    *out_effective_count = effective_data_count;
  }
  return 0;
}

static int probe_blockio_lba0(struct efi_block_device *dev,
                              uint32_t handoff_media_id, const char *tag,
                              const struct x64_storage_runtime_io *io,
                              void *probe_buf) {
  uint32_t runtime_media_id = handoff_media_id;
  EFI_STATUS_K st_probe = 0;
  int ok = 0;

  if (!dev || !dev->ctx.bio || !dev->ctx.bio->read_blocks ||
      dev->dev.block_size == 0 || !probe_buf) {
    return 0;
  }

  if (dev->ctx.bio->media && dev->ctx.bio->media->media_id != 0U) {
    runtime_media_id = dev->ctx.bio->media->media_id;
  }

  st_probe = dev->ctx.bio->read_blocks(dev->ctx.bio, handoff_media_id, 0ULL,
                                       (uint64_t)dev->dev.block_size, probe_buf);
  io_print(io, "[fs] Probe ");
  io_print(io, tag);
  io_print(io, " ReadBlocks(handoff_media) status=");
  io_print_hex64(io, st_probe);
  io_print(io, " code=");
  io_print_dec_u32(io, (uint32_t)(st_probe & 0xFFFFFFFFULL));
  io_print(io, " media=");
  io_print_dec_u32(io, handoff_media_id);
  io_putc(io, '\n');
  ok = ((st_probe & EFI_STATUS_ERROR_BIT_K) == 0);

  if (runtime_media_id != handoff_media_id) {
    st_probe = dev->ctx.bio->read_blocks(dev->ctx.bio, runtime_media_id, 0ULL,
                                         (uint64_t)dev->dev.block_size,
                                         probe_buf);
    io_print(io, "[fs] Probe ");
    io_print(io, tag);
    io_print(io, " ReadBlocks(runtime_media) status=");
    io_print_hex64(io, st_probe);
    io_print(io, " code=");
    io_print_dec_u32(io, (uint32_t)(st_probe & 0xFFFFFFFFULL));
    io_print(io, " media=");
    io_print_dec_u32(io, runtime_media_id);
    io_putc(io, '\n');
    if ((st_probe & EFI_STATUS_ERROR_BIT_K) == 0) {
      ok = 1;
    }
  }

  return ok;
}

static void print_efi_blockio_status(const struct efi_block_device *dev,
                                     uint32_t handoff_media_id,
                                     const struct x64_storage_runtime_io *io) {
  if (!dev) {
    return;
  }

  io_print(io, "[fs] EFI BlockIO selecionado ativo: blk=");
  io_print_dec_u32(io, dev->dev.block_size);
  io_print(io, " align=");
  io_print_dec_u32(io, dev->ctx.io_align);
  io_print(io, " media(handoff)=");
  io_print_dec_u32(io, handoff_media_id);
  io_print(io, " media(runtime)=");
  if (dev->ctx.bio && dev->ctx.bio->media) {
    io_print_dec_u32(io, dev->ctx.bio->media->media_id);
  } else {
    io_print(io, "0");
  }
  io_print(io, " read=");
  io_print_hex64(io, (uint64_t)(uintptr_t)dev->ctx.bio->read_blocks);
  io_print(io, " write=");
  io_print_hex64(io, (uint64_t)(uintptr_t)dev->ctx.bio->write_blocks);
  io_putc(io, '\n');
}

static int gpt_find_capyos_data_partition_native(
    struct block_device *raw, void *probe_buf, uint32_t probe_buf_size,
    uint32_t *out_data_start, uint32_t *out_data_count,
    const struct x64_storage_runtime_io *io) {
  struct gpt_header_k *hdr = (struct gpt_header_k *)probe_buf;
  uint32_t part_entry_size = 0;
  uint32_t num_part_entries = 0;
  uint64_t part_entry_lba = 0;
  uint32_t ents_per_block = 0;
  uint32_t read_entries = 0;
  uint64_t cur_lba = 0;
  uint32_t data_start = 0;
  uint32_t data_count = 0;
  uint32_t named_data_start = 0;
  uint32_t named_data_count = 0;

  if (!raw || !probe_buf || !out_data_start || !out_data_count ||
      raw->block_size == 0 || raw->block_size > probe_buf_size) {
    io_print(io, "[storage] GPT nativo invalido: parametros de probe.\n");
    return -1;
  }
  *out_data_start = 0;
  *out_data_count = 0;

  if (block_device_read(raw, (uint32_t)GPT_HEADER_LBA_K, probe_buf) != 0) {
    io_print(io, "[storage] GPT nativo invalido: leitura do header falhou.\n");
    return -1;
  }
  if (hdr->signature != GPT_SIG_K || hdr->part_entry_size == 0 ||
      hdr->num_part_entries == 0 || hdr->part_entry_size > raw->block_size) {
    io_print(io, "[storage] GPT nativo invalido: header ou entries invalidos.\n");
    return -1;
  }

  part_entry_size = hdr->part_entry_size;
  num_part_entries = hdr->num_part_entries;
  part_entry_lba = hdr->part_entry_lba;

  ents_per_block = raw->block_size / part_entry_size;
  if (ents_per_block == 0) {
    return -1;
  }

  cur_lba = part_entry_lba;
  while (read_entries < num_part_entries) {
    uint32_t max_in_block = num_part_entries - read_entries;
    if (max_in_block > ents_per_block) {
      max_in_block = ents_per_block;
    }
    if (cur_lba > 0xFFFFFFFFULL ||
        block_device_read(raw, (uint32_t)cur_lba, probe_buf) != 0) {
      io_print(io, "[storage] GPT nativo invalido: leitura das entries falhou.\n");
      return -1;
    }

    for (uint32_t i = 0; i < max_in_block; ++i) {
      uint8_t *ptr = (uint8_t *)probe_buf + (i * part_entry_size);
      struct gpt_entry_k *entry = (struct gpt_entry_k *)ptr;

      if (entry->first_lba == 0 || entry->last_lba < entry->first_lba ||
          entry->first_lba > 0xFFFFFFFFULL ||
          (entry->last_lba - entry->first_lba) > 0xFFFFFFFFULL) {
        continue;
      }

      if (guid_eq_local(entry->part_type_guid, k_gpt_linux_fs_guid)) {
        uint32_t part_start = (uint32_t)entry->first_lba;
        uint32_t part_count = (uint32_t)((entry->last_lba - entry->first_lba) +
                                         1ULL);
        uint16_t part_name[36];
        for (uint32_t name_i = 0; name_i < 36u; ++name_i) {
          part_name[name_i] = entry->name[name_i];
        }
        if (gpt_name_equals_ascii(part_name, "DATA")) {
          named_data_start = part_start;
          named_data_count = part_count;
        } else if (data_start == 0) {
          data_start = part_start;
          data_count = part_count;
        }
      }
    }

    read_entries += max_in_block;
    ++cur_lba;
  }

  if (named_data_start != 0) {
    *out_data_start = named_data_start;
    *out_data_count = named_data_count;
    return 0;
  }
  if (data_start != 0 && data_count != 0) {
    *out_data_start = data_start;
    *out_data_count = data_count;
    return 0;
  }

  io_print(io, "[storage] GPT nativo invalido: particao DATA nao encontrada.\n");
  return -1;
}

static int native_data_matches_handoff(const struct boot_handoff *handoff,
                                       uint32_t data_start,
                                       uint32_t data_count) {
  if (!handoff || data_count == 0) {
    return 0;
  }

  if (handoff->version >= 4 && handoff->data_lba_count_raw != 0 &&
      handoff->data_lba_start_raw == data_start &&
      handoff->data_lba_count_raw == data_count) {
    return 1;
  }

  if (handoff->version >= 2 && handoff->data_lba_count != 0 &&
      handoff->data_lba_start == data_start &&
      handoff->data_lba_count == data_count) {
    return 1;
  }

  return 0;
}

static int resolve_native_data_from_handoff(const struct boot_handoff *handoff,
                                            uint32_t *out_data_start,
                                            uint32_t *out_data_count,
                                            const char **out_data_path) {
  if (out_data_start) {
    *out_data_start = 0;
  }
  if (out_data_count) {
    *out_data_count = 0;
  }
  if (out_data_path) {
    *out_data_path = "none";
  }
  if (!handoff || !out_data_start || !out_data_count || !out_data_path) {
    return 0;
  }

  if (handoff->version >= 4 && handoff->data_lba_count_raw != 0) {
    *out_data_start = (uint32_t)handoff->data_lba_start_raw;
    *out_data_count = (uint32_t)handoff->data_lba_count_raw;
    *out_data_path = "handoff-raw";
    return 1;
  }

  if (handoff->version >= 2 && handoff->data_lba_count != 0) {
    *out_data_start = (uint32_t)handoff->data_lba_start;
    *out_data_count = (uint32_t)handoff->data_lba_count;
    *out_data_path = "handoff-selected";
    return 1;
  }

  return 0;
}

static int probe_native_storage_backend_from_raw(
    const struct boot_handoff *handoff, const struct x64_storage_runtime_io *io,
    void *probe_buf, uint32_t probe_buf_size, struct block_device *raw_dev,
    enum x64_storage_backend backend, const char *label) {
  uint32_t data_start = 0;
  uint32_t data_count = 0;
  uint32_t handoff_data_start = 0;
  uint32_t handoff_data_count = 0;
  struct block_device *slice = NULL;
  const char *native_data_path = "none";
  const char *handoff_data_path = "none";
  int has_handoff_data = 0;
  int gpt_data_ok = 0;

  if (!probe_buf || !raw_dev || !label) {
    return 0;
  }

  if (raw_dev->block_size == 0 || raw_dev->block_size > probe_buf_size) {
    io_print(io, "[storage] ");
    io_print(io, label);
    io_print(io, " nativo indisponivel: block device invalido.\n");
    return 0;
  }

  io_print(io, "[storage] ");
  io_print(io, label);
  io_print(io, " nativo raw: blk=");
  io_print_dec_u32(io, raw_dev->block_size);
  io_print(io, " count=");
  io_print_dec_u32(io, raw_dev->block_count);
  io_putc(io, '\n');

  has_handoff_data = resolve_native_data_from_handoff(
      handoff, &handoff_data_start, &handoff_data_count, &handoff_data_path);
  if (gpt_find_capyos_data_partition_native(raw_dev, probe_buf,
                                            probe_buf_size, &data_start,
                                            &data_count, io) == 0) {
    if (has_handoff_data &&
        !native_data_matches_handoff(handoff, data_start, data_count)) {
      io_print(io, "[storage] ");
      io_print(io, label);
      io_print(io, " nativo: DATA do GPT diverge do handoff.\n");
      data_start = 0;
      data_count = 0;
    } else {
      native_data_path = "gpt-data";
      gpt_data_ok = 1;
    }
  } else {
    io_print(io, "[storage] ");
    io_print(io, label);
    io_print(io,
             " nativo: GPT indisponivel; tentando faixa DATA do handoff.\n");
  }

  if (!gpt_data_ok && has_handoff_data) {
    data_start = handoff_data_start;
    data_count = handoff_data_count;
    native_data_path = handoff_data_path;
  }

  if (data_count == 0) {
    io_print(io, "[storage] ");
    io_print(io, label);
    io_print(io, " nativo indisponivel: GPT/BOOT/DATA nao validos.\n");
    return 0;
  }

  {
    uint64_t effective_data_count = 0;
    if (raw_dev->block_count == 0 ||
        compute_effective_data_count((uint64_t)data_start, (uint64_t)data_count,
                                     (uint64_t)raw_dev->block_count - 1ULL,
                                     &effective_data_count) != 0) {
      io_print(io, "[storage] ");
      io_print(io, label);
      io_print(io, " nativo invalido: faixa DATA fora do bloco bruto.\n");
      io_print(io, "[storage] DATA start=");
      io_print_dec_u32(io, data_start);
      io_print(io, " count=");
      io_print_dec_u32(io, data_count);
      io_print(io, " raw_count=");
      io_print_dec_u32(io, raw_dev->block_count);
      io_putc(io, '\n');
      return 0;
    }
    if (effective_data_count != data_count) {
      io_print(io, "[storage] ");
      io_print(io, label);
      io_print(io, " nativo: ajustando DATA ao tamanho do bloco bruto.\n");
      data_count = (uint32_t)effective_data_count;
    }
  }

  if (data_start >= raw_dev->block_count ||
      data_count > (raw_dev->block_count - data_start)) {
    io_print(io, "[storage] ");
    io_print(io, label);
    io_print(io, " nativo invalido: slice DATA excede o namespace.\n");
    io_print(io, "[storage] DATA start=");
    io_print_dec_u32(io, data_start);
    io_print(io, " count=");
    io_print_dec_u32(io, data_count);
    io_print(io, " raw_count=");
    io_print_dec_u32(io, raw_dev->block_count);
    io_putc(io, '\n');
    return 0;
  }

  slice = block_offset_wrap(raw_dev, data_start, data_count);
  if (!slice) {
    io_print(io, "[storage] ");
    io_print(io, label);
    io_print(io,
             " nativo indisponivel: falha ao criar slice DATA (heap ou wrapper).\n");
    io_print(io, "[storage] DATA start=");
    io_print_dec_u32(io, data_start);
    io_print(io, " count=");
    io_print_dec_u32(io, data_count);
    io_print(io, " raw_count=");
    io_print_dec_u32(io, raw_dev->block_count);
    io_print(io, " heap=");
    io_print_dec_u32(io, (uint32_t)kheap_used());
    io_print(io, "/");
    io_print_dec_u32(io, (uint32_t)kheap_size());
    io_putc(io, '\n');
    return 0;
  }
  g_storage_native_device = block_chunked_wrap(slice, CAPYFS_BLOCK_SIZE);
  if (!g_storage_native_device) {
    io_print(io, "[storage] ");
    io_print(io, label);
    io_print(io,
             " nativo indisponivel: falha ao alinhar DATA para CAPYFS.\n");
    g_storage_native_device = NULL;
    return 0;
  }
  if (block_device_read(g_storage_native_device, 0, probe_buf) != 0) {
    io_print(io, "[storage] ");
    io_print(io, label);
    io_print(io, " nativo indisponivel: leitura inicial do DATA falhou.\n");
    g_storage_native_device = NULL;
    return 0;
  }

  g_storage_native_backend = backend;
  g_storage_native_data_path = native_data_path;
  g_storage_native_ready = 1;
  io_print(io, "[storage] ");
  io_print(io, label);
  io_print(io, " nativo detectado: DATA pronta via ");
  io_print(io, native_data_path);
  io_print(io, ".\n");
  return 1;
}

static void probe_native_storage_candidate(
    const struct boot_handoff *handoff, const struct x64_storage_runtime_io *io,
    void *probe_buf, uint32_t probe_buf_size) {
  struct block_device *raw_nvme = NULL;
  struct block_device *raw_ahci = NULL;

  g_storage_native_backend = X64_STORAGE_BACKEND_NONE;
  g_storage_native_data_path = "none";
  g_storage_native_ready = 0;
  g_storage_native_device = NULL;

  if (!probe_buf) {
    return;
  }

  if (nvme_device_count() <= 0 && nvme_init() != 0) {
    io_print(io,
             "[storage] NVMe nativo indisponivel: inicializacao do controlador falhou.\n");
  } else if (nvme_device_count() > 0) {
    raw_nvme = nvme_get_block_device(0);
    if (probe_native_storage_backend_from_raw(
            handoff, io, probe_buf, probe_buf_size, raw_nvme,
            X64_STORAGE_BACKEND_NVME, "NVMe")) {
      return;
    }
  } else {
    io_print(io,
             "[storage] NVMe nativo indisponivel: nenhum controlador pronto.\n");
  }

  if (ahci_device_count() <= 0 && ahci_init() != 0) {
    io_print(io,
             "[storage] AHCI nativo indisponivel: inicializacao do controlador falhou.\n");
    return;
  }
  if (ahci_device_count() <= 0) {
    io_print(io,
             "[storage] AHCI nativo indisponivel: nenhum disco SATA pronto.\n");
    return;
  }

  raw_ahci = ahci_get_block_device(0);
  (void)probe_native_storage_backend_from_raw(
      handoff, io, probe_buf, probe_buf_size, raw_ahci,
      X64_STORAGE_BACKEND_AHCI, "AHCI");
}

static struct block_device *promote_native_storage_backend(
    const struct x64_storage_runtime_io *io, const char *reason) {
  if (!g_storage_native_ready || !g_storage_native_device) {
    return NULL;
  }

  io_print(io, "[storage] Promovendo backend nativo para volume DATA (");
  io_print(io, reason ? reason : "motivo nao especificado");
  io_print(io, ").\n");
  g_storage_backend = g_storage_native_backend;
  g_storage_data_path = g_storage_native_data_path;
  g_storage_has_device = 1;
  return g_storage_native_device;
}

struct block_device *x64_storage_runtime_open_handoff_data_device(
    const struct boot_handoff *handoff, const struct x64_storage_runtime_io *io,
    void *probe_buf) {
  uint32_t block_size = 0;
  uint64_t effective_data_count = 0;
  uint64_t active_data_start = 0;
  uint64_t active_data_count = 0;
  int selected_probe_ok = 0;
  int has_raw_fallback = 0;
  int firmware_block_io_available = 0;
  int boot_services_active = 0;

  g_storage_backend = X64_STORAGE_BACKEND_NONE;
  g_storage_data_path = "none";
  g_storage_has_device = 0;
  probe_native_storage_candidate(handoff, io, probe_buf, CAPYFS_BLOCK_SIZE);

  if (!handoff || !probe_buf) {
    if (g_storage_native_ready) {
      g_storage_backend = g_storage_native_backend;
      g_storage_data_path = g_storage_native_data_path;
      g_storage_has_device = 1;
      return g_storage_native_device;
    }
    return NULL;
  }

  firmware_block_io_available =
      (handoff->runtime_flags & BOOT_HANDOFF_RUNTIME_FIRMWARE_BLOCK_IO) != 0;
  boot_services_active =
      (handoff->runtime_flags & BOOT_HANDOFF_RUNTIME_BOOT_SERVICES_ACTIVE) != 0;
  has_raw_fallback =
      (handoff->version >= 4 && handoff->efi_block_io_raw != 0 &&
       handoff->data_lba_count_raw != 0 &&
       (handoff->efi_block_io_raw != handoff->efi_block_io ||
        handoff->data_lba_start_raw != handoff->data_lba_start ||
        handoff->data_lba_count_raw != handoff->data_lba_count ||
        handoff->efi_media_id_raw != handoff->efi_media_id));

  if (firmware_block_io_available && boot_services_active &&
      g_storage_native_ready) {
    return promote_native_storage_backend(
        io, "backend nativo pronto antes do uso de EFI BlockIO");
  }

  if ((!firmware_block_io_available || !boot_services_active ||
       handoff->version < 2 || handoff->data_lba_count == 0) &&
      g_storage_native_ready) {
    return promote_native_storage_backend(io,
                                          "firmware indisponivel ou inativo");
  }

  if (handoff->version < 2 || !firmware_block_io_available ||
      handoff->data_lba_count == 0) {
    return NULL;
  }

  block_size = handoff->efi_block_size ? handoff->efi_block_size : 512;
  if (block_size == 0) {
    return NULL;
  }

  if (handoff->data_lba_start > handoff->efi_disk_last_lba) {
    io_print(io,
             "[fs] ERRO: DATA start LBA fora do disco reportado pelo handoff.\n");
    return NULL;
  }

  {
    uint64_t max_data_count =
        (handoff->efi_disk_last_lba - handoff->data_lba_start) + 1ULL;
    if (handoff->data_lba_count > max_data_count) {
      io_print(io,
               "[fs] aviso: DATA count do handoff excede o disco; ajustando.\n");
    }
  }

  if (compute_effective_data_count(handoff->data_lba_start,
                                   handoff->data_lba_count,
                                   handoff->efi_disk_last_lba,
                                   &effective_data_count) != 0) {
    io_print(io, "[fs] ERRO: parametros DATA invalidos no handoff.\n");
    return NULL;
  }

  if (efi_block_device_init(&g_efi_runtime_disk, handoff->efi_block_io,
                            handoff->efi_media_id, block_size,
                            handoff->efi_disk_last_lba) != 0) {
    io_print(io,
             "[fs] ERRO: falha ao inicializar adaptador EFI BlockIO do handoff.\n");
    return NULL;
  }
  print_efi_blockio_status(&g_efi_runtime_disk, handoff->efi_media_id, io);

  selected_probe_ok = probe_blockio_lba0(&g_efi_runtime_disk, handoff->efi_media_id,
                                         "selected", io, probe_buf);
  active_data_start = handoff->data_lba_start;
  active_data_count = effective_data_count;

  if (!selected_probe_ok && has_raw_fallback) {
    uint64_t raw_effective_data_count = 0;

    io_print(io,
             "[fs] Probe do BlockIO selecionado falhou; tentando fallback RAW.\n");
    if (handoff->data_lba_start_raw > handoff->efi_disk_last_lba_raw) {
      io_print(io, "[fs] aviso: fallback RAW com start LBA fora do disco.\n");
    } else {
      uint64_t raw_max_data_count =
          (handoff->efi_disk_last_lba_raw - handoff->data_lba_start_raw) + 1ULL;
      if (handoff->data_lba_count_raw > raw_max_data_count) {
        io_print(io,
                 "[fs] aviso: DATA count do fallback RAW excede o disco; ajustando.\n");
      }
      if (compute_effective_data_count(
              handoff->data_lba_start_raw, handoff->data_lba_count_raw,
              handoff->efi_disk_last_lba_raw, &raw_effective_data_count) == 0 &&
          efi_block_device_init(
              &g_efi_runtime_disk_alt, handoff->efi_block_io_raw,
              handoff->efi_media_id_raw ? handoff->efi_media_id_raw
                                        : handoff->efi_media_id,
              block_size, handoff->efi_disk_last_lba_raw) == 0) {
        int raw_probe_ok = 0;
        io_print(io, "[fs] Tentando fallback RAW do handoff para volume DATA.\n");
        raw_probe_ok = probe_blockio_lba0(
            &g_efi_runtime_disk_alt,
            handoff->efi_media_id_raw ? handoff->efi_media_id_raw
                                      : handoff->efi_media_id,
            "raw-fallback", io, probe_buf);
        if (raw_probe_ok) {
          g_efi_runtime_disk = g_efi_runtime_disk_alt;
          g_efi_runtime_disk.dev.ctx = &g_efi_runtime_disk.ctx;
          active_data_start = handoff->data_lba_start_raw;
          active_data_count = raw_effective_data_count;
          g_storage_data_path = "raw-fallback";
          io_print(io, "[fs] Fallback RAW ativo para DATA.\n");
        } else {
          io_print(io, "[fs] Fallback RAW falhou no probe inicial.\n");
        }
      } else {
        io_print(io, "[fs] aviso: fallback RAW invalido no handoff.\n");
      }
    }
  } else if (selected_probe_ok && has_raw_fallback &&
             handoff->data_lba_start == 0 && handoff->data_lba_start_raw != 0) {
    io_print(io,
             "[fs] Handle logico DATA validado; fallback RAW mantido apenas para "
             "erro de probe.\n");
  }

  {
    struct block_device *slice = NULL;
    slice = block_offset_wrap(&g_efi_runtime_disk.dev, (uint32_t)active_data_start,
                              (uint32_t)active_data_count);
    if (!slice) {
      return NULL;
    }
    g_storage_backend = X64_STORAGE_BACKEND_EFI_BLOCK_IO;
    if (!g_storage_data_path || g_storage_data_path[0] == '\0' ||
        g_storage_data_path[0] == 'n') {
      g_storage_data_path = "selected";
    }
    g_storage_has_device = 1;
    return block_chunked_wrap(slice, CAPYFS_BLOCK_SIZE);
  }
}

const struct efi_block_device *x64_storage_runtime_active_efi(void) {
  return g_storage_backend == X64_STORAGE_BACKEND_EFI_BLOCK_IO
             ? &g_efi_runtime_disk
             : NULL;
}

const char *x64_storage_runtime_backend_name(void) {
  switch (g_storage_backend) {
  case X64_STORAGE_BACKEND_EFI_BLOCK_IO:
    return "efi-blockio";
  case X64_STORAGE_BACKEND_AHCI:
    return "ahci";
  case X64_STORAGE_BACKEND_NVME:
    return "nvme";
  default:
    return "none";
  }
}

const char *x64_storage_runtime_data_path(void) { return g_storage_data_path; }

const char *x64_storage_runtime_native_candidate_name(void) {
  switch (g_storage_native_backend) {
  case X64_STORAGE_BACKEND_AHCI:
    return "ahci";
  case X64_STORAGE_BACKEND_NVME:
    return "nvme";
  default:
    return "none";
  }
}

const char *x64_storage_runtime_native_data_path(void) {
  return g_storage_native_data_path;
}

int x64_storage_runtime_uses_firmware(void) {
  return g_storage_backend == X64_STORAGE_BACKEND_EFI_BLOCK_IO;
}

int x64_storage_runtime_has_native_candidate(void) {
  return g_storage_native_ready;
}

int x64_storage_runtime_has_device(void) { return g_storage_has_device; }
