#include "arch/x86_64/storage_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "fs/capyfs.h"
#include "internal/storage_runtime_gpt.h"
#include "internal/storage_runtime_hyperv.h"
#include "arch/x86_64/storage_runtime_hyperv_plan.h"
#include "internal/storage_runtime_native.h"
#include "kernel/log/klog.h"

static struct efi_block_device g_efi_runtime_disk;
static struct efi_block_device g_efi_runtime_disk_alt;
static enum x64_storage_backend g_storage_backend = X64_STORAGE_BACKEND_NONE;
static const char *g_storage_data_path = "none";
static int g_storage_has_device = 0;
static int g_storage_candidates_initialized = 0;
static struct x64_storage_native_candidate_state g_storage_native;
static struct x64_storage_hyperv_runtime_state g_storage_hyperv_runtime;
extern int boot_slot_block_provider_init_from_block_device(
    struct boot_slot_block_provider *provider, struct block_device *raw,
    const struct boot_slot_disk_binding *binding,
    uint64_t *out_registration_epoch);

static struct boot_slot_block_provider g_runtime_boot_provider;
static struct boot_slot_store g_runtime_boot_store;
static uint64_t g_runtime_boot_registration_epoch;
static uint64_t g_runtime_boot_lease_epoch;

static struct x64_storage_boot_provider_status g_boot_provider_status = {
    .provider_ready = 0u,
    .reason = X64_STORAGE_BOOT_PROVIDER_NO_PERSISTENT_MOUNT,
};

static void storage_zero_bytes(void *ptr, size_t len) {
  uint8_t *bytes = ptr;
  if (!bytes)
    return;
  for (size_t i = 0u; i < len; ++i)
    bytes[i] = 0u;
}

/* Slice 3E.4.C (2026-05-25) — local `dbg_putc`/`dbg_puts`/`dbg_hex32`
 * helpers removed. The storage-runtime decision trace now routes
 * through `klog(KLOG_INFO, ...)` / `klog_hex(KLOG_INFO, ...)` so
 * the trace lands in the kernel klog ring (persisted by the kernel
 * logger service) instead of the QEMU-only port 0xE9 debug
 * console. Diagnostic strings keep the `[srt]` prefix used by the
 * older lines so external grep targets stay stable. */

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
  if (!handoff || !probe_buf) {
    return;
  }

  if (!g_storage_candidates_initialized) {
    x64_storage_hyperv_runtime_reset(&g_storage_hyperv_runtime);
    g_storage_candidates_initialized = 1;
  }

  if (!g_storage_native.ready) {
    /* Probe native storage even when EFI Block I/O is still available.
     * Installed-disk boots can hand off a whole-disk firmware handle while the
     * real DATA slice must be rediscovered from GPT; skipping native probing
     * here makes persistence depend on that ambiguous firmware path. */
    x64_storage_runtime_native_probe(&g_storage_native, handoff, io, probe_buf,
                                     CAPYFS_BLOCK_SIZE);
  }
}

static struct block_device *try_native_fallback(
    const struct boot_handoff *handoff, const struct x64_storage_runtime_io *io,
    void *probe_buf, const char *reason) {
  if (g_storage_native.ready) {
    return x64_storage_runtime_native_promote(
        &g_storage_native, &g_storage_backend, &g_storage_data_path,
        &g_storage_has_device, io, reason);
  }

  if (!handoff || !probe_buf) {
    return NULL;
  }

  klog(KLOG_INFO, "[srt] native: probing on demand");
  x64_storage_runtime_native_probe(&g_storage_native, handoff, io, probe_buf,
                                   CAPYFS_BLOCK_SIZE);

  if (!g_storage_native.ready) {
    return NULL;
  }

  return x64_storage_runtime_native_promote(
      &g_storage_native, &g_storage_backend, &g_storage_data_path,
      &g_storage_has_device, io, reason);
}

static int probe_blockio_lba0(struct efi_block_device *dev,
                              uint32_t handoff_media_id, const char *tag,
                              const struct x64_storage_runtime_io *io,
                              void *probe_buf) {
  EFI_STATUS_K st_probe = 0;
  int rc = 0;

  if (!dev || !dev->ctx.bio || !dev->ctx.bio->read_blocks ||
      dev->dev.block_size == 0 || !probe_buf) {
    return 0;
  }

  (void)handoff_media_id;
  rc = block_device_read(&dev->dev, 0, probe_buf);
  st_probe = dev->ctx.last_status;
  io_print(io, "[fs] Probe ");
  io_print(io, tag);
  io_print(io, " ReadBlocks(aligned) status=");
  io_print_hex64(io, st_probe);
  io_print(io, " code=");
  io_print_dec_u32(io, (uint32_t)(st_probe & 0xFFFFFFFFULL));
  io_print(io, " media=");
  io_print_dec_u32(io, dev->ctx.last_media_id);
  io_putc(io, '\n');

  return rc == 0;
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
  int active_probe_ok = 0;
  int has_raw_fallback = 0;
  int firmware_block_io_available = 0;
  int boot_services_active = 0;

  g_storage_backend = X64_STORAGE_BACKEND_NONE;
  g_storage_data_path = "none";
  g_storage_has_device = 0;
  ensure_storage_runtime_candidates(handoff, io, probe_buf);

  if (!handoff || !probe_buf) {
    return try_native_fallback(handoff, io, probe_buf,
                               "sem handoff disponivel");
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
    klog(KLOG_INFO, "[srt] native: ready before EFI");
    return x64_storage_runtime_native_promote(
        &g_storage_native, &g_storage_backend, &g_storage_data_path,
        &g_storage_has_device, io,
        "backend nativo pronto antes do uso de EFI BlockIO");
  }

  if ((!firmware_block_io_available || !boot_services_active ||
       handoff->version < 2 || handoff->data_lba_count == 0) &&
      g_storage_native.ready) {
    klog(KLOG_INFO, "[srt] native: firmware unavailable/inactive");
    return x64_storage_runtime_native_promote(
        &g_storage_native, &g_storage_backend, &g_storage_data_path,
        &g_storage_has_device, io, "firmware indisponivel ou inativo");
  }

  if (handoff->version < 2 || !firmware_block_io_available ||
      !boot_services_active || handoff->data_lba_count == 0) {
    return try_native_fallback(handoff, io, probe_buf,
                               "firmware indisponivel ou inativo");
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
    klog(KLOG_WARN, "[srt] native: invalid EFI handoff");
    return try_native_fallback(handoff, io, probe_buf,
                               "handoff EFI invalido");
  }

  if (efi_block_device_init(&g_efi_runtime_disk, handoff->efi_block_io,
                            handoff->efi_media_id, block_size,
                            handoff->efi_disk_last_lba) != 0) {
    io_print(io,
             "[fs] ERRO: falha ao inicializar adaptador EFI BlockIO do handoff.\n");
    klog(KLOG_WARN, "[srt] native: EFI init failed");
    return try_native_fallback(handoff, io, probe_buf,
                               "falha ao inicializar EFI BlockIO");
  }
  print_efi_blockio_status(&g_efi_runtime_disk, handoff->efi_media_id, io);

  selected_probe_ok = probe_blockio_lba0(&g_efi_runtime_disk, handoff->efi_media_id,
                                         "selected", io, probe_buf);
  active_probe_ok = selected_probe_ok;
  active_data_start = handoff->data_lba_start;
  active_data_count = effective_data_count;

  if ((!selected_probe_ok ||
       (has_raw_fallback && selected_probe_ok &&
        handoff->data_lba_start == 0 && handoff->data_lba_start_raw != 0 &&
        handoff->data_lba_count_raw != 0 &&
        handoff->data_lba_count_raw <= handoff->data_lba_count)) &&
      has_raw_fallback) {
    uint64_t raw_effective_data_count = 0;

    if (!selected_probe_ok) {
      io_print(io,
               "[fs] Probe do BlockIO selecionado falhou; tentando fallback RAW.\n");
    } else {
      io_print(
          io,
          "[fs] Handle particionado em LBA 0 detectado; promovendo fallback RAW "
          "para evitar ambiguidade do volume DATA.\n");
    }
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
          active_probe_ok = 1;
          g_storage_data_path = "raw-fallback";
          if (!selected_probe_ok) {
            io_print(io, "[fs] Fallback RAW ativo para DATA.\n");
          } else {
            io_print(io,
                     "[fs] Fallback RAW promovido como caminho principal de DATA.\n");
          }
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

  if (!active_probe_ok) {
    io_print(io,
             "[fs] EFI BlockIO nao passou no probe inicial da particao DATA.\n");
    klog(KLOG_WARN, "[srt] native: EFI probe failed");
    return try_native_fallback(handoff, io, probe_buf,
                               "probe inicial do EFI BlockIO falhou");
  }

  {
    struct block_device *slice = NULL;
    struct block_device *chunked = NULL;
    slice = block_offset_wrap(&g_efi_runtime_disk.dev, (uint32_t)active_data_start,
                              (uint32_t)active_data_count);
    if (!slice) {
      klog(KLOG_WARN, "[srt] native: EFI slice failed");
      return try_native_fallback(handoff, io, probe_buf,
                                 "falha ao criar slice EFI da DATA");
    }
    chunked = block_chunked_wrap(slice, CAPYFS_BLOCK_SIZE);
    if (!chunked) {
      klog(KLOG_WARN, "[srt] native: EFI chunk failed");
      return try_native_fallback(handoff, io, probe_buf,
                                 "falha ao alinhar slice EFI para CAPYFS");
    }
    if (block_device_read(chunked, 0, probe_buf) != 0) {
      io_print(io,
               "[fs] EFI BlockIO abriu DATA, mas falhou na leitura validada do bloco 0.\n");
      klog(KLOG_WARN, "[srt] native: EFI validated read failed");
      return try_native_fallback(
          handoff, io, probe_buf,
          "leitura validada do bloco 0 em EFI BlockIO falhou");
    }
    g_storage_backend = X64_STORAGE_BACKEND_EFI_BLOCK_IO;
    if (!g_storage_data_path || g_storage_data_path[0] == '\0' ||
        g_storage_data_path[0] == 'n') {
      g_storage_data_path = "selected";
    }
    g_storage_has_device = 1;
    /* Slice 3E.4.C audit: EFI BlockIO promotion success. Each
     * dimension (start/count) becomes its own klog_hex entry so
     * downstream parsers can grep individually. */
    klog_hex(KLOG_INFO, "[srt] efi ready start=",
             (uint64_t)(uint32_t)active_data_start);
    klog_hex(KLOG_INFO, "[srt] efi ready count=",
             (uint64_t)(uint32_t)active_data_count);
    return chunked;
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
  case X64_STORAGE_BACKEND_ATA_PIO:
    return "ata-pio";
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
  case X64_STORAGE_BACKEND_ATA_PIO:
    return "ata-pio";
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

struct block_device *x64_storage_runtime_raw_device(void) {
  return g_storage_backend != X64_STORAGE_BACKEND_EFI_BLOCK_IO &&
                 g_storage_native.ready
             ? g_storage_native.raw_device
             : NULL;
}

int x64_storage_runtime_data_binding(uint32_t *out_lba,
                                     uint32_t *out_sectors) {
  if (out_lba)
    *out_lba = 0u;
  if (out_sectors)
    *out_sectors = 0u;
  if (!out_lba || !out_sectors ||
      g_storage_backend == X64_STORAGE_BACKEND_EFI_BLOCK_IO ||
      !g_storage_native.ready || !g_storage_native.raw_device ||
      !g_storage_native.data_binding_verified ||
      g_storage_native.data_sectors == 0u)
    return -1;
  *out_lba = g_storage_native.data_lba;
  *out_sectors = g_storage_native.data_sectors;
  return 0;
}

int x64_storage_runtime_boot_provider_status(
    struct x64_storage_boot_provider_status *out) {
  if (!out)
    return -1;
  *out = g_boot_provider_status;
  return 0;
}

/* Map a store result onto the runtime capability. An indeterminate commit
 * invalidates the capability for the rest of the boot: the durable metadata is
 * no longer known, so no further staging or arming may be authorized. */
static int boot_store_result(int rc) {
  if (rc == 0)
    return 0;
  if (rc == BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN) {
    g_boot_provider_status.provider_ready = 0u;
    g_boot_provider_status.reason = X64_STORAGE_BOOT_PROVIDER_CONTROL_UNKNOWN;
  } else {
    g_boot_provider_status.reason = X64_STORAGE_BOOT_PROVIDER_TOKEN_MISMATCH;
  }
  return -1;
}

int x64_storage_runtime_stage_boot_payload_sha256(
    const char *version, const uint8_t *payload, size_t payload_len,
    const uint8_t expected_sha256[BOOT_SLOT_SHA256_SIZE], uint32_t *out_slot,
    uint64_t *out_generation) {
  struct boot_slot_snapshot snapshot;
  struct x64_storage_boot_stage_plan plan;
  if (out_slot)
    *out_slot = BOOT_SLOT_NONE;
  if (out_generation)
    *out_generation = 0u;
  if (!g_boot_provider_status.provider_ready || !out_slot || !out_generation ||
      !payload || payload_len == 0u || payload_len > (size_t)UINT32_MAX)
    return -1;
  if (boot_slot_snapshot_get(&snapshot) != 0) {
    g_boot_provider_status.reason = X64_STORAGE_BOOT_PROVIDER_CONTROL_UNKNOWN;
    return -1;
  }
  if (x64_storage_boot_provider_plan_stage(&snapshot, version,
                                          (uint32_t)payload_len,
                                          expected_sha256, &plan) != 0)
    return -1;
  if (boot_store_result(boot_slot_store_stage_inactive_authorized(
          &g_runtime_boot_store, g_runtime_boot_lease_epoch, &snapshot,
          plan.slot, &plan.image, payload, payload_len, out_generation)) != 0)
    return -1;
  *out_slot = plan.slot;
  return 0;
}

int x64_storage_runtime_arm_boot_slot(uint32_t slot,
                                     uint64_t expected_generation,
                                     uint64_t *out_generation) {
  if (out_generation)
    *out_generation = 0u;
  if (!g_boot_provider_status.provider_ready || !out_generation ||
      slot >= BOOT_SLOT_COUNT || expected_generation == 0u)
    return -1;
  return boot_store_result(
      boot_slot_store_arm(&g_runtime_boot_store, g_runtime_boot_lease_epoch,
                          slot, expected_generation, out_generation));
}

int x64_storage_runtime_boot_rollback_check(
    const struct boot_slot_attempt_handoff *attempt) {
  int pending;
  if (!attempt || !g_boot_provider_status.provider_ready ||
      (attempt->flags & BOOT_HANDOFF_SLOT_ATTEMPT_VALID) == 0u ||
      attempt->slot >= BOOT_SLOT_COUNT || attempt->generation == 0u)
    return -1;
  if ((attempt->flags & BOOT_HANDOFF_SLOT_ATTEMPT_ROLLBACK) != 0u)
    return 2;
  pending = boot_slot_needs_rollback();
  if (pending < 0) {
    g_boot_provider_status.reason = X64_STORAGE_BOOT_PROVIDER_CONTROL_UNKNOWN;
    return -1;
  }
  return pending ? 1 : 0;
}

int x64_storage_runtime_confirm_boot_health(
    const struct boot_slot_attempt_handoff *attempt) {
  if (!attempt || !g_boot_provider_status.provider_ready ||
      attempt->flags != (BOOT_HANDOFF_SLOT_ATTEMPT_VALID |
                         BOOT_HANDOFF_SLOT_ATTEMPT_PENDING) ||
      attempt->slot >= BOOT_SLOT_COUNT || attempt->generation == 0u)
    return -1;
  {
    int rc = boot_slot_store_confirm_health(
        &g_runtime_boot_store, g_runtime_boot_lease_epoch, attempt->slot,
        attempt->generation);
    if (rc == BOOT_SLOT_ERR_COMMIT_UNKNOWN) {
      g_boot_provider_status.provider_ready = 0u;
      g_boot_provider_status.reason = X64_STORAGE_BOOT_PROVIDER_CONTROL_UNKNOWN;
    } else if (rc != 0) {
      g_boot_provider_status.reason = X64_STORAGE_BOOT_PROVIDER_TOKEN_MISMATCH;
    }
    return rc;
  }
}

int x64_storage_runtime_register_boot_provider(
    int persistent_mount_ready, const struct boot_slot_disk_binding *binding,
    struct boot_slot_block_provider *provider) {
  struct x64_storage_boot_provider_runtime_snapshot snapshot = {0};
  struct block_device *raw = x64_storage_runtime_raw_device();
  uint32_t data_lba = 0u;
  uint32_t data_sectors = 0u;
  storage_zero_bytes(provider, provider ? sizeof(*provider) : 0u);
  if (g_boot_provider_status.provider_ready)
    return 0;
  snapshot.persistent_mount_ready = persistent_mount_ready ? 1u : 0u;
  snapshot.active_native_backend =
      g_storage_backend != X64_STORAGE_BACKEND_EFI_BLOCK_IO &&
              g_storage_backend != X64_STORAGE_BACKEND_NONE
          ? 1u
          : 0u;
  snapshot.storage_device_ready = g_storage_has_device ? 1u : 0u;
  snapshot.raw_block_size = raw ? raw->block_size : 0u;
  snapshot.raw_read_ready =
      raw && raw->ops && (raw->ops->read_block || raw->ops->read_block_ex)
          ? 1u
          : 0u;
  snapshot.raw_write_ready =
      raw && raw->ops && (raw->ops->write_block || raw->ops->write_block_ex)
          ? 1u
          : 0u;
  snapshot.data_binding_verified =
      x64_storage_runtime_data_binding(&data_lba, &data_sectors) == 0 && binding &&
              binding->data_lba == data_lba &&
              binding->data_sectors == data_sectors
          ? 1u
          : 0u;
  snapshot.esp_binding_verified =
      binding && g_storage_native.data_binding_verified &&
              binding->esp_lba == g_storage_native.identity.esp.lba &&
              binding->esp_sectors == g_storage_native.identity.esp.sectors &&
              binding->boot_lba == g_storage_native.identity.boot.lba &&
              binding->boot_sectors == g_storage_native.identity.boot.sectors
          ? 1u
          : 0u;
  snapshot.flush_ready = block_device_supports_flush(raw) ? 1u : 0u;
  x64_storage_boot_provider_evaluate_runtime(&snapshot,
                                             &g_boot_provider_status);
  if (!g_boot_provider_status.provider_ready)
    return -1;
  g_boot_provider_status.provider_ready = 0u;
  if (boot_slot_block_provider_init_from_block_device(
          &g_runtime_boot_provider, raw, binding,
          &g_runtime_boot_registration_epoch) != 0) {
    g_boot_provider_status.reason = X64_STORAGE_BOOT_PROVIDER_NO_FLUSH;
    return -1;
  }
  if (boot_slot_block_provider_open_store(
          &g_runtime_boot_provider, g_runtime_boot_registration_epoch,
          &g_runtime_boot_store, &g_runtime_boot_lease_epoch) != 0) {
    (void)boot_slot_block_provider_unregister(
        &g_runtime_boot_provider, g_runtime_boot_registration_epoch);
    g_runtime_boot_registration_epoch = 0u;
    g_boot_provider_status.reason = X64_STORAGE_BOOT_PROVIDER_NO_FLUSH;
    return -1;
  }
  if (boot_slot_store_bind_control(&g_runtime_boot_store,
                                   g_runtime_boot_lease_epoch) != 0) {
    (void)boot_slot_block_provider_close_store(
        &g_runtime_boot_provider, g_runtime_boot_registration_epoch,
        &g_runtime_boot_store, g_runtime_boot_lease_epoch);
    (void)boot_slot_block_provider_unregister(
        &g_runtime_boot_provider, g_runtime_boot_registration_epoch);
    g_runtime_boot_registration_epoch = 0u;
    g_runtime_boot_lease_epoch = 0u;
    g_boot_provider_status.reason = X64_STORAGE_BOOT_PROVIDER_NO_CONTROL;
    return -1;
  }
  g_boot_provider_status.provider_ready = 1u;
  g_boot_provider_status.reason = X64_STORAGE_BOOT_PROVIDER_READY;
  if (provider)
    *provider = g_runtime_boot_provider;
  return 0;
}

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
