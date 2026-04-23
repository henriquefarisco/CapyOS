static int mount_root_capyfs(struct x64_kernel_volume_runtime_state *state,
                             const struct x64_kernel_volume_runtime_io *io,
                             struct block_device *dev, const char *label) {
  int mount_rc = 0;
  int root_rc = -1;

  if (!state || !state->root_sb) {
    return -1;
  }

  mount_rc = mount_capyfs(dev, state->root_sb);
  root_rc = (mount_rc == 0) ? vfs_mount_root(state->root_sb) : -1;
  if (mount_rc != 0 || root_rc != 0) {
    struct capyfs_check_report report;
    int check_ok = (dev && capyfs_check(dev, &report) == 0);
    io_print(io, "[fs] ERRO: falha ao montar CAPYFS em ");
    io_print(io, label ? label : "dispositivo");
    io_print(io, ". mount=");
    io_print_hex(io, (uint64_t)(uint32_t)mount_rc);
    io_print(io, " root=");
    io_print_hex(io, (uint64_t)(uint32_t)root_rc);
    if (dev) {
      io_print(io, " blk=");
      io_print_dec_u32(io, dev->block_size);
      io_print(io, " count=");
      io_print_dec_u32(io, dev->block_count);
    }
    if (check_ok) {
      io_print(io, " check=");
      io_print(io, capyfs_check_result_label(report.result));
      io_print(io, " magic=");
      io_print_hex(io, report.super.magic);
      io_print(io, " ver=");
      io_print_dec_u32(io, report.super.version);
      io_print(io, " data=");
      io_print_dec_u32(io, report.super.data_start);
      io_print(io, " detail=");
      io_print_hex(io, report.detail_primary);
      io_print(io, "/");
      io_print_hex(io, report.detail_secondary);
    }
    io_putc(io, '\n');
    return -1;
  }
  return 0;
}

int x64_kernel_volume_runtime_mount_root_capyfs(
    struct x64_kernel_volume_runtime_state *state,
    const struct x64_kernel_volume_runtime_io *io, struct block_device *dev,
    const char *label) {
  return mount_root_capyfs(state, io, dev, label);
}

static int initialize_encrypted_data_volume(
    struct x64_kernel_volume_runtime_state *state,
    const struct x64_kernel_volume_runtime_io *io,
    struct block_device *data_dev, const char *normalized_key) {
  struct block_device *crypt_dev = NULL;

  if (!state || !data_dev || !normalized_key || !normalized_key[0] ||
      !state->active_volume_key || !state->active_volume_key_ready) {
    return -1;
  }

  local_copy(state->active_volume_key, state->active_volume_key_size,
             normalized_key);
  *state->active_volume_key_ready = 1;
  dbg_puts("[kvr] init encrypted volume begin\n");

  crypt_dev =
      open_crypt_volume_with_password(state, data_dev, state->active_volume_key);
  if (!crypt_dev) {
    dbg_puts("[kvr] init crypt layer fail\n");
    io_print(io, "[fs] ERRO: falha ao iniciar camada criptografica.\n");
    return -1;
  }

  {
    uint8_t *verify_buf = NULL;
    int roundtrip_ok = 1;
    int write_rc = 0;
    int read_rc = 0;
    uint32_t mismatch_index = 0xFFFFFFFFu;
    uint32_t mismatch_expected = 0;
    uint32_t mismatch_got = 0;
    if (state->data_io_probe && state->data_io_probe_size >= crypt_dev->block_size) {
      for (uint32_t i = 0; i < crypt_dev->block_size; ++i) {
        state->data_io_probe[i] = (uint8_t)((i * 131u + 17u) & 0xFFu);
      }
      verify_buf = (uint8_t *)kalloc(crypt_dev->block_size);
      if (!verify_buf) {
        io_print(io, "[fs] Aviso: sem memoria para autoteste do volume cifrado.\n");
      } else {
        write_rc = block_device_write(crypt_dev, 0, state->data_io_probe);
        if (write_rc != 0) {
          roundtrip_ok = 0;
        } else {
          read_rc = block_device_read(crypt_dev, 0, verify_buf);
          if (read_rc != 0) {
            roundtrip_ok = 0;
          }
        }
        for (uint32_t i = 0; i < crypt_dev->block_size; ++i) {
          if (verify_buf[i] != state->data_io_probe[i]) {
            roundtrip_ok = 0;
            mismatch_index = i;
            mismatch_expected = state->data_io_probe[i];
            mismatch_got = verify_buf[i];
            break;
          }
        }
      }
      if (!roundtrip_ok) {
        dbg_puts("[kvr] crypt roundtrip fail\n");
        io_print(io,
                 "[fs] ERRO: autoteste de roundtrip da camada criptografica falhou.\n");
        io_print(io, "[fs] Roundtrip diag: write_rc=");
        io_print_hex(io, (uint64_t)(uint32_t)write_rc);
        io_print(io, " read_rc=");
        io_print_hex(io, (uint64_t)(uint32_t)read_rc);
        if (mismatch_index != 0xFFFFFFFFu) {
          io_print(io, " mismatch=");
          io_print_dec_u32(io, mismatch_index);
          io_print(io, " expect=");
          io_print_hex(io, mismatch_expected);
          io_print(io, " got=");
          io_print_hex(io, mismatch_got);
        }
        io_putc(io, '\n');
        if (verify_buf) {
          kfree(verify_buf);
        }
        buffer_cache_invalidate(crypt_dev);
        crypt_free(crypt_dev);
        reset_blank_probe_regions(state, data_dev);
        return -1;
      }
      dbg_puts("[kvr] crypt roundtrip ok\n");
      if (verify_buf) {
        kfree(verify_buf);
      }
    }
  }

  int fmt_rc = capyfs_format(crypt_dev, 128, crypt_dev->block_count, NULL);
  if (fmt_rc != 0) {
    dbg_puts("[kvr] capyfs_format fail rc=");
    dbg_hex32((uint32_t)fmt_rc);
    dbg_putc('\n');
    io_print(io, "[fs] ERRO: falha ao formatar volume cifrado. rc=");
    io_print_hex(io, (uint64_t)(uint32_t)fmt_rc);
    uint32_t sync_block = 0;
    if (buffer_cache_last_error_block(&sync_block) == 0) {
      io_print(io, " sync_block=");
      io_print_dec_u32(io, sync_block);
      io_print(io, " sync_code=");
      io_print_dec_u32(io,
                       (uint32_t)(buffer_cache_last_error_code() &
                                  0xFFFFFFFF));
    }
    io_print_active_efi_runtime_trace(io);
    io_putc(io, '\n');
    buffer_cache_invalidate(crypt_dev);
    crypt_free(crypt_dev);
    reset_blank_probe_regions(state, data_dev);
    return -1;
  }
  dbg_puts("[kvr] capyfs_format ok\n");

  if (state->data_io_probe && state->data_io_probe_size >= data_dev->block_size &&
      block_device_read(data_dev, 0, state->data_io_probe) == 0) {
    uint32_t nonzero = 0;
    for (uint32_t i = 0; i < data_dev->block_size; ++i) {
      if (state->data_io_probe[i] != 0) {
        ++nonzero;
      }
    }
    io_print(io, "[fs] Probe RAW apos formatacao (LBA0) bytes!=0: ");
    io_print_dec_u32(io, nonzero);
    io_putc(io, '\n');
  } else {
    io_print(io, "[fs] Aviso: probe RAW apos formatacao falhou.\n");
  }

  if (state->data_io_probe &&
      state->data_io_probe_size >= crypt_dev->block_size &&
      block_device_read(crypt_dev, 0, state->data_io_probe) == 0) {
    dbg_puts("[kvr] crypt blk0=");
    dbg_hex32(((uint32_t)state->data_io_probe[0] << 24) |
              ((uint32_t)state->data_io_probe[1] << 16) |
              ((uint32_t)state->data_io_probe[2] << 8) |
              (uint32_t)state->data_io_probe[3]);
    dbg_puts(" ");
    dbg_hex32(((uint32_t)state->data_io_probe[4] << 24) |
              ((uint32_t)state->data_io_probe[5] << 16) |
              ((uint32_t)state->data_io_probe[6] << 8) |
              (uint32_t)state->data_io_probe[7]);
    dbg_putc('\n');
  } else {
    dbg_puts("[kvr] crypt blk0 read fail\n");
  }

  buffer_cache_invalidate(crypt_dev);
  if (mount_root_capyfs(state, io, crypt_dev, "DATA cifrada") != 0) {
    dbg_puts("[kvr] mount after format fail\n");
    buffer_cache_invalidate(crypt_dev);
    crypt_free(crypt_dev);
    reset_blank_probe_regions(state, data_dev);
    return -1;
  }
  dbg_puts("[kvr] mount after format ok\n");
  io_print(io, "[fs] Volume cifrado inicializado e montado.\n");
  return 0;
}

