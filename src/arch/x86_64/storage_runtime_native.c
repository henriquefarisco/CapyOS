#include "storage_runtime_native.h"

#include <stdint.h>

#include "drivers/nvme.h"
#include "drivers/storage/ahci.h"
#include "fs/capyfs.h"
#include "memory/kmem.h"
#include "storage_runtime_gpt.h"

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
    struct x64_storage_native_candidate_state *state,
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

  has_handoff_data = resolve_native_data_from_handoff(
      handoff, &handoff_data_start, &handoff_data_count, &handoff_data_path);
  if (x64_storage_runtime_find_data_partition_native(
          raw_dev, probe_buf, probe_buf_size, &data_start, &data_count,
          io) == 0) {
    if (has_handoff_data &&
        !native_data_matches_handoff(handoff, data_start, data_count)) {
      native_io_print(io, "[storage] ");
      native_io_print(io, label);
      native_io_print(io, " nativo: DATA do GPT diverge do handoff.\n");
      data_start = 0;
      data_count = 0;
    } else {
      native_data_path = "gpt-data";
      gpt_data_ok = 1;
    }
  } else {
    native_io_print(io, "[storage] ");
    native_io_print(io, label);
    native_io_print(io,
                    " nativo: GPT indisponivel; tentando faixa DATA do handoff.\n");
  }

  if (!gpt_data_ok && has_handoff_data) {
    data_start = handoff_data_start;
    data_count = handoff_data_count;
    native_data_path = handoff_data_path;
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
                      " nativo: ajustando DATA ao tamanho do bloco bruto.\n");
      data_count = (uint32_t)effective_data_count;
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
}

void x64_storage_runtime_native_probe(
    struct x64_storage_native_candidate_state *state,
    const struct boot_handoff *handoff, const struct x64_storage_runtime_io *io,
    void *probe_buf, uint32_t probe_buf_size) {
  struct block_device *raw_nvme = NULL;
  struct block_device *raw_ahci = NULL;

  x64_storage_runtime_native_reset(state);
  if (!state || !probe_buf) {
    return;
  }

  if (nvme_device_count() <= 0 && nvme_init() != 0) {
    native_io_print(
        io,
        "[storage] NVMe nativo indisponivel: inicializacao do controlador falhou.\n");
  } else if (nvme_device_count() > 0) {
    raw_nvme = nvme_get_block_device(0);
    if (probe_native_storage_backend_from_raw(
            state, handoff, io, probe_buf, probe_buf_size, raw_nvme,
            X64_STORAGE_BACKEND_NVME, "NVMe")) {
      return;
    }
  } else {
    native_io_print(
        io, "[storage] NVMe nativo indisponivel: nenhum controlador pronto.\n");
  }

  if (ahci_device_count() <= 0 && ahci_init() != 0) {
    native_io_print(
        io,
        "[storage] AHCI nativo indisponivel: inicializacao do controlador falhou.\n");
    return;
  }
  if (ahci_device_count() <= 0) {
    native_io_print(
        io, "[storage] AHCI nativo indisponivel: nenhum disco SATA pronto.\n");
    return;
  }

  raw_ahci = ahci_get_block_device(0);
  (void)probe_native_storage_backend_from_raw(
      state, handoff, io, probe_buf, probe_buf_size, raw_ahci,
      X64_STORAGE_BACKEND_AHCI, "AHCI");
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
