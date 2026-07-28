#include "internal/storage_runtime_native.h"

#include <stdint.h>

#include "arch/x86_64/storage_boot_provider_policy.h"
#include "drivers/nvme.h"
#include "drivers/storage/ahci.h"
#include "drivers/storage/ata_pio.h"
#include "fs/capyfs.h"
#include "memory/kmem.h"
#include "internal/storage_runtime_gpt.h"

static int native_identity_equal(const struct capyos_gpt_identity *a,
                                 const struct capyos_gpt_identity *b) {
  if (!a || !b || a->disk_sectors != b->disk_sectors ||
      a->esp.lba != b->esp.lba || a->esp.sectors != b->esp.sectors ||
      a->boot.lba != b->boot.lba || a->boot.sectors != b->boot.sectors ||
      a->data.lba != b->data.lba || a->data.sectors != b->data.sectors)
    return 0;
  for (size_t i = 0u; i < 16u; ++i) {
    if (a->disk_guid[i] != b->disk_guid[i] ||
        a->esp.guid[i] != b->esp.guid[i] ||
        a->boot.guid[i] != b->boot.guid[i] ||
        a->data.guid[i] != b->data.guid[i])
      return 0;
  }
  return 1;
}

static void native_io_print(const struct x64_storage_runtime_io *io,
                            const char *message) {
  if (io && io->print && message) {
    io->print(message);
  }
}

static void native_io_print_dec_u32(const struct x64_storage_runtime_io *io,
                                    uint32_t value) {
  if (io && io->print_dec_u32) {
    io->print_dec_u32(value);
  }
}

static void native_io_putc(const struct x64_storage_runtime_io *io, char ch) {
  if (io && io->putc) {
    io->putc(ch);
  }
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
    if (x64_storage_boot_provider_range_u32(
            handoff->data_lba_start_raw, handoff->data_lba_count_raw,
            out_data_start, out_data_count) != 0)
      return -1;
    *out_data_path = "handoff-raw";
    return 1;
  }

  if (handoff->version >= 2 && handoff->data_lba_count != 0) {
    if (x64_storage_boot_provider_range_u32(
            handoff->data_lba_start, handoff->data_lba_count,
            out_data_start, out_data_count) != 0)
      return -1;
    *out_data_path = "handoff-selected";
    return 1;
  }

  return 0;
}

static int probe_native_storage_backend_from_raw(
    struct x64_storage_native_candidate_state *state,
    const struct boot_handoff *handoff, const struct x64_storage_runtime_io *io,
    void *probe_buf, uint32_t probe_buf_size, struct block_device *raw_dev,
    enum x64_storage_backend backend, const char *label,
    const struct capyos_gpt_identity *expected_identity) {
  uint32_t data_start = 0;
  uint32_t data_count = 0;
  uint32_t handoff_data_start = 0;
  uint32_t handoff_data_count = 0;
  struct capyos_gpt_identity identity;
  int identity_strict = 0;
  struct block_device *slice = NULL;
  const char *native_data_path = "none";
  const char *handoff_data_path = "none";
  int handoff_data_status = 0;
  int has_handoff_data = 0;
  int data_binding_verified = 0;

  if (!state || !probe_buf || !raw_dev || !label) {
    return 0;
  }

  if (raw_dev->block_size == 0 || raw_dev->block_size > probe_buf_size) {
    native_io_print(io, "[storage] ");
    native_io_print(io, label);
    native_io_print(io, " nativo indisponivel: block device invalido.\n");
    return 0;
  }

  native_io_print(io, "[storage] ");
  native_io_print(io, label);
  native_io_print(io, " nativo raw: blk=");
  native_io_print_dec_u32(io, raw_dev->block_size);
  native_io_print(io, " count=");
  native_io_print_dec_u32(io, raw_dev->block_count);
  native_io_putc(io, '\n');

  handoff_data_status = resolve_native_data_from_handoff(
      handoff, &handoff_data_start, &handoff_data_count, &handoff_data_path);
  if (handoff_data_status < 0) {
    native_io_print(io, "[storage] handoff DATA excede ABI de bloco.\n");
    return 0;
  }
  has_handoff_data = handoff_data_status > 0 ? 1 : 0;
  if (x64_storage_runtime_find_data_partition_native(
          raw_dev, probe_buf, probe_buf_size, &data_start, &data_count,
          handoff &&
                  (handoff->version < 9u ||
                   (handoff->disk_identity.flags &
                    BOOT_HANDOFF_DISK_IDENTITY_LEGACY_VALID))
              ? 1
              : 0,
          &identity_strict, &identity, io) == 0) {
    if (!expected_identity || !native_identity_equal(&identity,
                                                      expected_identity))
      return 0;
    if (handoff && handoff->version >= 9u &&
        !x64_storage_boot_provider_identity_matches_handoff(
            handoff, &identity, identity_strict)) {
      native_io_print(io, "[storage] identidade GPT diverge do handoff v9.\n");
      return 0;
    }
    uint32_t selected_start = 0u;
    uint32_t selected_count = 0u;
    if (x64_storage_boot_provider_select_data(
            1, data_start, data_count, has_handoff_data,
            handoff_data_start, handoff_data_count, &selected_start,
            &selected_count, &data_binding_verified) != 0) {
      native_io_print(io, "[storage] ");
      native_io_print(io, label);
      native_io_print(io, " nativo: DATA do GPT diverge do handoff.\n");
      return 0;
    }
    data_start = selected_start;
    data_count = selected_count;
    native_data_path = "gpt-data";
  } else {
    native_io_print(io, "[storage] ");
    native_io_print(io, label);
    native_io_print(io, " nativo: GPT indisponivel; recusando DATA.\n");
    return 0;
  }

  if (data_count == 0) {
    native_io_print(io, "[storage] ");
    native_io_print(io, label);
    native_io_print(io, " nativo indisponivel: GPT/BOOT/DATA nao validos.\n");
    return 0;
  }

  {
    uint64_t effective_data_count = 0;
    if (raw_dev->block_count == 0 ||
        x64_storage_runtime_compute_effective_data_count(
            (uint64_t)data_start, (uint64_t)data_count,
            (uint64_t)raw_dev->block_count - 1ULL,
            &effective_data_count) != 0) {
      native_io_print(io, "[storage] ");
      native_io_print(io, label);
      native_io_print(io, " nativo invalido: faixa DATA fora do bloco bruto.\n");
      native_io_print(io, "[storage] DATA start=");
      native_io_print_dec_u32(io, data_start);
      native_io_print(io, " count=");
      native_io_print_dec_u32(io, data_count);
      native_io_print(io, " raw_count=");
      native_io_print_dec_u32(io, raw_dev->block_count);
      native_io_putc(io, '\n');
      return 0;
    }
    if (effective_data_count != data_count) {
      native_io_print(io, "[storage] ");
      native_io_print(io, label);
      native_io_print(io,
                      " nativo: DATA excede o tamanho do bloco bruto.\n");
      return 0;
    }
  }

  if (data_start >= raw_dev->block_count ||
      data_count > (raw_dev->block_count - data_start)) {
    native_io_print(io, "[storage] ");
    native_io_print(io, label);
    native_io_print(io, " nativo invalido: slice DATA excede o namespace.\n");
    native_io_print(io, "[storage] DATA start=");
    native_io_print_dec_u32(io, data_start);
    native_io_print(io, " count=");
    native_io_print_dec_u32(io, data_count);
    native_io_print(io, " raw_count=");
    native_io_print_dec_u32(io, raw_dev->block_count);
    native_io_putc(io, '\n');
    return 0;
  }

  slice = block_offset_wrap(raw_dev, data_start, data_count);
  if (!slice) {
    native_io_print(io, "[storage] ");
    native_io_print(io, label);
    native_io_print(
        io, " nativo indisponivel: falha ao criar slice DATA (heap ou wrapper).\n");
    native_io_print(io, "[storage] DATA start=");
    native_io_print_dec_u32(io, data_start);
    native_io_print(io, " count=");
    native_io_print_dec_u32(io, data_count);
    native_io_print(io, " raw_count=");
    native_io_print_dec_u32(io, raw_dev->block_count);
    native_io_print(io, " heap=");
    native_io_print_dec_u32(io, (uint32_t)kheap_used());
    native_io_print(io, "/");
    native_io_print_dec_u32(io, (uint32_t)kheap_size());
    native_io_putc(io, '\n');
    return 0;
  }

  state->device = block_chunked_wrap(slice, CAPYFS_BLOCK_SIZE);
  if (!state->device) {
    native_io_print(io, "[storage] ");
    native_io_print(io, label);
    native_io_print(io,
                    " nativo indisponivel: falha ao alinhar DATA para CAPYFS.\n");
    state->device = NULL;
    return 0;
  }
  if (block_device_read(state->device, 0, probe_buf) != 0) {
    native_io_print(io, "[storage] ");
    native_io_print(io, label);
    native_io_print(io,
                    " nativo indisponivel: leitura inicial do DATA falhou.\n");
    state->device = NULL;
    return 0;
  }

  state->backend = backend;
  state->data_path = native_data_path;
  state->raw_device = raw_dev;
  state->data_lba = data_start;
  state->data_sectors = data_count;
  state->identity = identity;
  state->data_binding_verified = data_binding_verified && identity_strict ? 1 : 0;
  state->ready = 1;
  native_io_print(io, "[storage] ");
  native_io_print(io, label);
  native_io_print(io, " nativo detectado: DATA pronta via ");
  native_io_print(io, native_data_path);
  native_io_print(io, ".\n");
  return 1;
}

void x64_storage_runtime_native_reset(
    struct x64_storage_native_candidate_state *state) {
  if (!state) {
    return;
  }
  state->backend = X64_STORAGE_BACKEND_NONE;
  state->data_path = "none";
  state->ready = 0;
  state->device = NULL;
  state->raw_device = NULL;
  state->data_lba = 0u;
  state->data_sectors = 0u;
  state->identity = (struct capyos_gpt_identity){0};
  state->data_binding_verified = 0;
}

static int native_raw_matches_handoff(
    struct block_device *raw, const struct boot_handoff *handoff,
    const struct x64_storage_runtime_io *io, void *probe_buf,
    uint32_t probe_buf_size, struct capyos_gpt_identity *out_identity) {
  struct capyos_gpt_identity identity;
  uint32_t data_start = 0u;
  uint32_t data_count = 0u;
  int identity_strict = 0;
  int allow_legacy = handoff &&
                             (handoff->version < 9u ||
                              (handoff->disk_identity.flags &
                               BOOT_HANDOFF_DISK_IDENTITY_LEGACY_VALID))
                         ? 1
                         : 0;
  if (out_identity)
    *out_identity = (struct capyos_gpt_identity){0};
  if (!raw || !handoff || !out_identity)
    return 0;
  if (x64_storage_runtime_find_data_partition_native(
          raw, probe_buf, probe_buf_size, &data_start, &data_count,
          allow_legacy, &identity_strict, &identity, io) == 0) {
    int matches = 0;
    if (handoff->version >= 9u) {
      matches = x64_storage_boot_provider_identity_matches_handoff(
          handoff, &identity, identity_strict);
    } else if (handoff->version >= 4u &&
               handoff->data_lba_count_raw != 0u &&
               handoff->efi_disk_last_lba_raw != UINT64_MAX) {
      matches = handoff->data_lba_start_raw == data_start &&
                handoff->data_lba_count_raw == data_count &&
                (uint64_t)identity.disk_sectors ==
                    handoff->efi_disk_last_lba_raw + 1u;
    } else {
      matches = handoff->data_lba_start == data_start &&
                handoff->data_lba_count == data_count;
    }
    if (matches)
      *out_identity = identity;
    return matches;
  }
  return 0;
}

void x64_storage_runtime_native_probe(
    struct x64_storage_native_candidate_state *state,
    const struct boot_handoff *handoff, const struct x64_storage_runtime_io *io,
    void *probe_buf, uint32_t probe_buf_size) {
  struct block_device *selected_raw = NULL;
  struct capyos_gpt_identity selected_identity;
  enum x64_storage_backend selected_backend = X64_STORAGE_BACKEND_NONE;
  const char *selected_label = NULL;
  int matches = 0;
  x64_storage_runtime_native_reset(state);
  if (!state || !probe_buf || !handoff)
    return;

  if (nvme_device_count() <= 0 && nvme_init() != 0) {
    native_io_print(
        io,
        "[storage] NVMe nativo indisponivel: inicializacao do controlador falhou.\n");
  }
  for (int i = 0; i < nvme_device_count(); ++i) {
    struct block_device *raw = nvme_get_block_device(i);
    struct capyos_gpt_identity identity;
    if (native_raw_matches_handoff(raw, handoff, io, probe_buf, probe_buf_size,
                                   &identity)) {
      selected_raw = raw;
      selected_identity = identity;
      selected_backend = X64_STORAGE_BACKEND_NVME;
      selected_label = "NVMe";
      matches++;
    }
  }

  if (ahci_device_count() <= 0 && ahci_init() != 0) {
    native_io_print(
        io,
        "[storage] AHCI nativo indisponivel: inicializacao do controlador falhou.\n");
  }
  for (int i = 0; i < ahci_device_count(); ++i) {
    struct block_device *raw = ahci_get_block_device(i);
    struct capyos_gpt_identity identity;
    if (native_raw_matches_handoff(raw, handoff, io, probe_buf, probe_buf_size,
                                   &identity)) {
      selected_raw = raw;
      selected_identity = identity;
      selected_backend = X64_STORAGE_BACKEND_AHCI;
      selected_label = "AHCI";
      matches++;
    }
  }

  /* ATA-PIO is the broad-hardware-compatibility fallback for hypervisor
   * environments that expose legacy IDE/ATA emulation instead of (or in
   * addition to) NVMe/AHCI. Concrete targets:
   *
   *   - Hyper-V Generation 1 with IDE-attached VHD;
   *   - older QEMU/Bochs/VirtualBox legacy IDE machines;
   *   - bare-metal hosts with firmware that exposes ATA legacy fallback.
   *
   * On the VMware + UEFI + E1000 official validation track this branch is
   * unreachable because AHCI/NVMe always probe first. */
  if (ata_devices_count() <= 0)
    ata_init();
  for (int i = 0; i < ata_devices_count(); ++i) {
    struct block_device *raw = ata_device_by_index(i);
    struct capyos_gpt_identity identity;
    if (native_raw_matches_handoff(raw, handoff, io, probe_buf, probe_buf_size,
                                   &identity)) {
      selected_raw = raw;
      selected_identity = identity;
      selected_backend = X64_STORAGE_BACKEND_ATA_PIO;
      selected_label = "ATA-PIO";
      matches++;
    }
  }

  if (matches != 1 || !selected_raw) {
    if (matches > 1)
      native_io_print(io, "[storage] identidade nativa ambigua; recusando DATA.\n");
    return;
  }
  (void)probe_native_storage_backend_from_raw(
      state, handoff, io, probe_buf, probe_buf_size, selected_raw,
      selected_backend, selected_label, &selected_identity);
}

struct block_device *x64_storage_runtime_native_promote(
    struct x64_storage_native_candidate_state *state,
    enum x64_storage_backend *active_backend, const char **active_data_path,
    int *has_device, const struct x64_storage_runtime_io *io,
    const char *reason) {
  if (!state || !state->ready || !state->device) {
    return NULL;
  }

  native_io_print(io, "[storage] Promovendo backend nativo para volume DATA (");
  native_io_print(io, reason ? reason : "motivo nao especificado");
  native_io_print(io, ").\n");
  if (active_backend) {
    *active_backend = state->backend;
  }
  if (active_data_path) {
    *active_data_path = state->data_path;
  }
  if (has_device) {
    *has_device = 1;
  }
  return state->device;
}
