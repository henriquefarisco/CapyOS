#include "arch/x86_64/storage_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "fs/capyfs.h"
#include "storage_runtime_gpt.h"
#include "storage_runtime_hyperv.h"
#include "arch/x86_64/storage_runtime_hyperv_plan.h"
#include "storage_runtime_native.h"

static struct efi_block_device g_efi_runtime_disk;
static struct efi_block_device g_efi_runtime_disk_alt;
static enum x64_storage_backend g_storage_backend = X64_STORAGE_BACKEND_NONE;
static const char *g_storage_data_path = "none";
static int g_storage_has_device = 0;
static int g_storage_candidates_initialized = 0;
static struct x64_storage_native_candidate_state g_storage_native;
static struct x64_storage_hyperv_runtime_state g_storage_hyperv_runtime;

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

static void ensure_storage_runtime_candidates(
    const struct boot_handoff *handoff, const struct x64_storage_runtime_io *io,
    void *probe_buf) {
  if (g_storage_candidates_initialized || !handoff || !probe_buf) {
    return;
  }

  x64_storage_hyperv_runtime_reset(&g_storage_hyperv_runtime);
  x64_storage_runtime_native_probe(&g_storage_native, handoff, io, probe_buf,
                                   CAPYFS_BLOCK_SIZE);
  g_storage_candidates_initialized = 1;
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
  ensure_storage_runtime_candidates(handoff, io, probe_buf);

  if (!handoff || !probe_buf) {
    if (g_storage_native.ready) {
      return x64_storage_runtime_native_promote(
          &g_storage_native, &g_storage_backend, &g_storage_data_path,
          &g_storage_has_device, io, "sem handoff disponivel");
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
      g_storage_native.ready) {
    return x64_storage_runtime_native_promote(
        &g_storage_native, &g_storage_backend, &g_storage_data_path,
        &g_storage_has_device, io,
        "backend nativo pronto antes do uso de EFI BlockIO");
  }

  if ((!firmware_block_io_available || !boot_services_active ||
       handoff->version < 2 || handoff->data_lba_count == 0) &&
      g_storage_native.ready) {
    return x64_storage_runtime_native_promote(
        &g_storage_native, &g_storage_backend, &g_storage_data_path,
        &g_storage_has_device, io, "firmware indisponivel ou inativo");
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

  if (x64_storage_runtime_compute_effective_data_count(
          handoff->data_lba_start, handoff->data_lba_count,
          handoff->efi_disk_last_lba, &effective_data_count) != 0) {
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
      if (x64_storage_runtime_compute_effective_data_count(
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
  switch (g_storage_native.backend) {
  case X64_STORAGE_BACKEND_AHCI:
    return "ahci";
  case X64_STORAGE_BACKEND_NVME:
    return "nvme";
  default:
    return "none";
  }
}

const char *x64_storage_runtime_native_data_path(void) {
  return g_storage_native.data_path;
}

int x64_storage_runtime_uses_firmware(void) {
  return g_storage_backend == X64_STORAGE_BACKEND_EFI_BLOCK_IO;
}

int x64_storage_runtime_has_native_candidate(void) {
  return g_storage_native.ready;
}

int x64_storage_runtime_has_device(void) { return g_storage_has_device; }

int x64_storage_runtime_hyperv_present(void) {
  return x64_storage_hyperv_runtime_present(&g_storage_hyperv_runtime);
}

int x64_storage_runtime_hyperv_bus_prepared(void) {
  return x64_storage_hyperv_runtime_bus_prepared(&g_storage_hyperv_runtime);
}

int x64_storage_runtime_hyperv_bus_connected(void) {
  return x64_storage_hyperv_runtime_bus_connected(&g_storage_hyperv_runtime);
}

int x64_storage_runtime_hyperv_offer_cached(void) {
  return x64_storage_hyperv_runtime_offer_cached(&g_storage_hyperv_runtime);
}

const char *x64_storage_runtime_hyperv_phase_name(void) {
  return x64_storage_hyperv_runtime_phase_name(&g_storage_hyperv_runtime);
}

const char *x64_storage_runtime_hyperv_gate_label(int boot_services_active) {
  return x64_storage_hyperv_gate_label(
      x64_storage_hyperv_runtime_gate_state(
          &g_storage_hyperv_runtime, boot_services_active,
          x64_storage_runtime_uses_firmware(),
          g_storage_hyperv_runtime.hybrid_prepare_allowed));
}

const char *x64_storage_runtime_hyperv_next_action_label(
    int boot_services_active) {
  return x64_storage_hyperv_action_label(
      x64_storage_hyperv_runtime_next_action(
          &g_storage_hyperv_runtime, boot_services_active,
          x64_storage_runtime_uses_firmware(),
          g_storage_hyperv_runtime.hybrid_prepare_allowed));
}

const char *x64_storage_runtime_hyperv_block_reason(int boot_services_active) {
  return x64_storage_hyperv_runtime_block_reason(
      &g_storage_hyperv_runtime, boot_services_active,
      x64_storage_runtime_uses_firmware(),
      g_storage_hyperv_runtime.hybrid_prepare_allowed);
}

void x64_storage_runtime_allow_hyperv_hybrid_prepare(int allow) {
  x64_storage_hyperv_runtime_allow_hybrid_prepare(&g_storage_hyperv_runtime,
                                                  allow);
}

int x64_storage_runtime_hyperv_controller_status(
    struct storvsc_controller_status *out) {
  return x64_storage_hyperv_runtime_controller_status(&g_storage_hyperv_runtime,
                                                      out);
}

uint32_t x64_storage_runtime_hyperv_attempt_count(void) {
  return x64_storage_hyperv_runtime_attempt_count(&g_storage_hyperv_runtime);
}

uint32_t x64_storage_runtime_hyperv_change_count(void) {
  return x64_storage_hyperv_runtime_change_count(&g_storage_hyperv_runtime);
}

int32_t x64_storage_runtime_hyperv_last_result(void) {
  return x64_storage_hyperv_runtime_last_result(&g_storage_hyperv_runtime);
}

const char *x64_storage_runtime_hyperv_last_action_label(
    int boot_services_active) {
  (void)boot_services_active;
  return x64_storage_hyperv_action_label(
      x64_storage_hyperv_runtime_last_action(&g_storage_hyperv_runtime));
}

int x64_storage_runtime_try_prepare_hyperv_bus(void (*print)(const char *)) {
  return x64_storage_hyperv_runtime_try_prepare_bus(&g_storage_hyperv_runtime,
                                                    print);
}

int x64_storage_runtime_manual_hyperv_step(int boot_services_active,
                                           void (*print)(const char *)) {
  return x64_storage_hyperv_runtime_manual_step(
      &g_storage_hyperv_runtime, boot_services_active,
      x64_storage_runtime_uses_firmware(), print);
}

int x64_storage_runtime_try_enable_hyperv_native(
    int boot_services_active, int allow_hybrid_prepare,
    void (*print)(const char *)) {
  return x64_storage_hyperv_runtime_try_enable_native(
      &g_storage_hyperv_runtime, boot_services_active,
      x64_storage_runtime_uses_firmware(), allow_hybrid_prepare, print);
}
