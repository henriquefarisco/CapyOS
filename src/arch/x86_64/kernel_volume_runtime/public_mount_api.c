#include "internal/kernel_volume_runtime_internal.h"

int x64_kernel_volume_runtime_persist_active_key_hash(
    const struct x64_kernel_volume_runtime_state *state) {
  uint8_t existing[SHA256_DIGEST_SIZE];
  if (!state || !state->shell_persistent_storage ||
      !state->active_volume_key_ready || !state->active_volume_key ||
      !*state->shell_persistent_storage || !*state->active_volume_key_ready) {
    return 0;
  }
  if (x64_kernel_volume_runtime_ensure_dir_recursive("/system") != 0) return -1;
  if (load_volume_key_hash(existing) == 0) {
    secure_memzero(existing, sizeof(existing));
    return 0;
  }
  return save_volume_key_hash(state->active_volume_key);
}

int x64_kernel_volume_runtime_mount_encrypted_data_volume(
    struct x64_kernel_volume_runtime_state *state,
    const struct x64_kernel_volume_runtime_io *io,
    struct block_device *data_dev) {
  char raw_key[128];
  char normalized_key[X64_KERNEL_VOLUME_KEY_MAX];
  char grouped_key[X64_KERNEL_VOLUME_KEY_MAX + 16];
  int data_blank = 0;
  if (!state || !data_dev || !state->active_volume_key ||
      !state->active_volume_key_ready || !state->handoff_volume_key ||
      !state->handoff_volume_key_ready || !state->data_io_probe ||
      state->data_io_probe_size < data_dev->block_size) {
    return -1;
  }
  io_print(io, "[fs] Probing leitura inicial da particao DATA...\n");
  if (block_device_read(data_dev, 0, state->data_io_probe) != 0) {
    io_print(io, "[fs] ERRO: falha de I/O ao ler DATA (backend ");
    io_print(io, x64_storage_runtime_backend_name());
    io_print(io, "/");
    io_print(io, x64_storage_runtime_data_path());
    io_print(io, ").\n");
    if (x64_storage_runtime_uses_firmware()) {
      io_print(io, "[fs] EFI ReadBlocks status=");
      {
        const struct efi_block_device *efi_disk = x64_storage_runtime_active_efi();
        io_print_hex64(io, efi_disk ? efi_disk->ctx.last_status : 0);
        io_print(io, " code=");
        io_print_dec_u32(io,
            efi_disk ? (uint32_t)(efi_disk->ctx.last_status & 0xFFFFFFFFULL) : 0);
        io_print(io, " lba=");
        io_print_dec_u32(io, efi_disk ? efi_disk->ctx.last_block_no : 0);
        io_print(io, " media=");
        io_print_dec_u32(io, efi_disk ? efi_disk->ctx.last_media_id : 0);
      }
      io_putc(io, '\n');
    }
    io_print(io,
        "[fs] A chave pode estar correta; acesso ao disco falhou antes da criptografia.\n");
    return -1;
  }
  io_print(io, "[fs] Probe de leitura DATA concluido.\n");
  (void)x64_kernel_volume_runtime_load_handoff_key(state);
  data_blank = device_is_blank(state, data_dev);
  dbg_puts("[kvr] data blank=");
  dbg_putc(data_blank ? '1' : '0');
  dbg_putc('\n');
  if (data_blank) {
    secure_memzero((uint8_t *)normalized_key, sizeof(normalized_key));
    io_print(io,
        "[fs] Particao DATA vazia detectada. Inicializando volume cifrado.\n");
    if (*state->handoff_volume_key_ready) {
      local_copy(normalized_key, sizeof(normalized_key), state->handoff_volume_key);
      io_print(io, "[fs] Chave de volume provisionada pela instalacao ISO.\n");
    } else {
      io_print(io, "[fs] ERRO: nenhuma chave provisionada encontrada no boot.\n");
      io_print(io, "[fs] Boot normal nao deve gerar/trocar chave de volume.\n");
      io_print(io,
          "[fs] Reinicie pela ISO para provisionar a chave e recriar o volume com seguranca.\n");
      return -1;
    }
    int init_rc = initialize_encrypted_data_volume(state, io, data_dev, normalized_key);
    secure_memzero((uint8_t *)normalized_key, sizeof(normalized_key));
    return init_rc;
  }
  secure_memzero(raw_key, sizeof(raw_key));
  secure_memzero((uint8_t *)normalized_key, sizeof(normalized_key));
  secure_memzero((uint8_t *)grouped_key, sizeof(grouped_key));
  if (*state->handoff_volume_key_ready) {
    dbg_puts("[kvr] try existing volume with handoff key\n");
    struct block_device *crypt_dev = open_crypt_volume_with_password(
        state, data_dev, state->handoff_volume_key);
    /* Install the journal root secret BEFORE mount so the journal hook can
     * derive a per-volume HMAC key on this very first mount. */
    install_journal_root_secret_from_key(state->handoff_volume_key);
    if (crypt_dev && mount_root_capyfs(state, io, crypt_dev, "DATA cifrada") == 0) {
      dbg_puts("[kvr] existing volume mount with handoff key ok\n");
      local_copy(state->active_volume_key, state->active_volume_key_size,
                 state->handoff_volume_key);
      *state->active_volume_key_ready = 1;
      io_print(io, "[fs] Volume cifrado montado automaticamente.\n");
      return 0;
    }
    dbg_puts("[kvr] existing volume mount with handoff key fail\n");
    if (crypt_dev) {
      buffer_cache_invalidate(crypt_dev);
      crypt_free(crypt_dev);
    }
    io_print(io,
        "[fs] Aviso: chave provisionada falhou para desbloquear o volume existente.\n");
    io_print(io,
        "[security] Validacao estrita por hash da chave foi desativada nesta fase.\n");
    io_print(io, "[fs] Seguindo para modo manual de chave (somente desbloqueio).\n");
  }
  io_print(io, "[fs] Volume cifrado detectado. Informe a senha para montar.\n");
  for (int attempt = 0; attempt < 3; ++attempt) {
    io_print(io, "Chave do volume: ");
    size_t len = io_readline(io, raw_key, sizeof(raw_key), 0);
    if (len == 0) {
      io_print(io, "Chave vazia. Tente novamente.\n");
      continue;
    }
    int normalized_ok =
        (normalize_volume_key_input(raw_key, normalized_key,
                                    sizeof(normalized_key)) == 0);
    if (normalized_ok)
      format_volume_key_groups(normalized_key, grouped_key, sizeof(grouped_key));
    const char *candidates[4];
    size_t candidate_count = 0;
    (void)append_key_candidate(candidates, &candidate_count,
                               sizeof(candidates) / sizeof(candidates[0]), raw_key);
    if (normalized_ok) {
      (void)append_key_candidate(candidates, &candidate_count,
                                 sizeof(candidates) / sizeof(candidates[0]),
                                 normalized_key);
      (void)append_key_candidate(candidates, &candidate_count,
                                 sizeof(candidates) / sizeof(candidates[0]),
                                 grouped_key);
    }
    int mounted = 0;
    for (size_t i = 0; i < candidate_count && !mounted; ++i) {
      struct block_device *crypt_dev =
          open_crypt_volume_with_password(state, data_dev, candidates[i]);
      if (!crypt_dev) continue;
      /* Install the journal root secret derived from the candidate key BEFORE
       * mount so the journal hook can run in authenticated mode. If mount
       * fails the secret is replaced on the next iteration. */
      install_journal_root_secret_from_key(candidates[i]);
      if (mount_root_capyfs(state, io, crypt_dev, "DATA cifrada") == 0) {
        mounted = 1;
        break;
      }
      buffer_cache_invalidate(crypt_dev);
      crypt_free(crypt_dev);
    }
    if (mounted) {
      if (normalized_ok) {
        local_copy(state->active_volume_key, state->active_volume_key_size,
                   normalized_key);
      } else {
        local_copy(state->active_volume_key, state->active_volume_key_size, raw_key);
      }
      *state->active_volume_key_ready = 1;
      secure_memzero(raw_key, sizeof(raw_key));
      secure_memzero((uint8_t *)normalized_key, sizeof(normalized_key));
      secure_memzero((uint8_t *)grouped_key, sizeof(grouped_key));
      io_print(io, "[fs] Volume cifrado montado com sucesso.\n");
      return 0;
    }
    secure_memzero(raw_key, sizeof(raw_key));
    secure_memzero((uint8_t *)normalized_key, sizeof(normalized_key));
    secure_memzero((uint8_t *)grouped_key, sizeof(grouped_key));
    io_print(io, "Chave incorreta ou volume invalido. ");
    io_print(io, "Formato aceito: letras/numeros; hifens opcionais.\n");
    if (attempt < 2) {
      io_print(io, "[fs] Tentativas restantes para desbloqueio: ");
      io_print_dec_u32(io, (uint32_t)(2 - attempt));
      io_putc(io, '\n');
    }
  }
  io_print(io, "[fs] Falha ao desbloquear o volume DATA apos 3 tentativas.\n");
  return -1;
}
