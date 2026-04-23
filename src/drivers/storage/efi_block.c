#include "drivers/storage/efi_block.h"

#include <stdint.h>

#include "memory/kmem.h"

static inline void dbg_putc(char ch) {
  __asm__ volatile("outb %0, %1" : : "a"((uint8_t)ch), "Nd"((uint16_t)0xE9));
}

static void dbg_puts(const char *s) {
  while (s && *s) {
    dbg_putc(*s++);
  }
}

static void dbg_hex32(uint32_t value) {
  static const char hex[] = "0123456789ABCDEF";
  for (int shift = 28; shift >= 0; shift -= 4) {
    dbg_putc(hex[(value >> shift) & 0xFu]);
  }
}

static void *align_ptr_local(void *ptr, uint32_t align) {
  if (!ptr || align <= 1) {
    return ptr;
  }
  uintptr_t p = (uintptr_t)ptr;
  uintptr_t rem = p % (uintptr_t)align;
  if (rem == 0) {
    return ptr;
  }
  p += (uintptr_t)align - rem;
  return (void *)p;
}

static int is_power_of_two_u32(uint32_t v) {
  return (v != 0) && ((v & (v - 1U)) == 0);
}

static int copy_bytes_local(void *dst, const void *src, uint32_t len) {
  if (!dst || !src) {
    return -1;
  }
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (uint32_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
  return 0;
}

static int buffers_equal_local(const void *a, const void *b, uint32_t len) {
  if (!a || !b) {
    return 0;
  }
  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;
  for (uint32_t i = 0; i < len; ++i) {
    if (pa[i] != pb[i]) {
      return 0;
    }
  }
  return 1;
}

static int efi_status_is_error(EFI_STATUS_K st) {
  return (st & EFI_STATUS_ERROR_BIT_K) != 0;
}

static uint32_t efi_status_code(EFI_STATUS_K st) {
  return (uint32_t)(st & 0xFFFFFFFFULL);
}

static int efi_status_should_retry_once(uint32_t code) {
  return (code == 13U /* EFI_MEDIA_CHANGED */ ||
          code == 6U  /* EFI_NOT_READY */ ||
          code == 2U  /* EFI_INVALID_PARAMETER */ ||
          code == 7U  /* EFI_DEVICE_ERROR */);
}

static EFI_STATUS_K efi_read_try(struct efi_block_ctx *bctx, uint32_t media_id,
                                 uint32_t block_no, void *buffer) {
  return bctx->bio->read_blocks(bctx->bio, media_id, (uint64_t)block_no,
                                (uint64_t)bctx->block_size, buffer);
}

static EFI_STATUS_K efi_write_try(struct efi_block_ctx *bctx, uint32_t media_id,
                                  uint32_t block_no, void *buffer) {
  return bctx->bio->write_blocks(bctx->bio, media_id, (uint64_t)block_no,
                                 (uint64_t)bctx->block_size, buffer);
}

static int efi_do_io(struct efi_block_ctx *bctx, uint32_t block_no, void *buffer,
                     int is_write) {
  if (!bctx || !bctx->bio || !buffer) {
    return -1;
  }

  /* Always use a known aligned bounce buffer for firmware I/O stability. */
  void *src_buf = buffer;
  void *io_buf = buffer;
  if (bctx->bounce_aligned) {
    io_buf = bctx->bounce_aligned;
    if (is_write &&
        copy_bytes_local(io_buf, buffer, bctx->block_size) != 0) {
      return -1;
    }
  }

  uint32_t handoff_media_id = bctx->handoff_media_id;
  uint32_t runtime_media_id = bctx->media_id;
  if (bctx->bio->media && bctx->bio->media->media_id != 0U) {
    runtime_media_id = bctx->bio->media->media_id;
  }
  /* Prefer firmware runtime MediaId when handoff provided zero. */
  if (bctx->media_id == 0U && runtime_media_id != 0U) {
    bctx->media_id = runtime_media_id;
  }
  int used_handoff_zero_write = 0;

  EFI_STATUS_K st = is_write ? efi_write_try(bctx, bctx->media_id, block_no, io_buf)
                             : efi_read_try(bctx, bctx->media_id, block_no, io_buf);
  bctx->last_status = st;
  bctx->last_block_no = block_no;
  bctx->last_media_id = bctx->media_id;

  if (efi_status_is_error(st)) {
    uint32_t code = efi_status_code(st);
    if (efi_status_should_retry_once(code)) {
      /* Retry once with the same MediaId first. */
      st = is_write ? efi_write_try(bctx, bctx->media_id, block_no, io_buf)
                    : efi_read_try(bctx, bctx->media_id, block_no, io_buf);
      bctx->last_status = st;
      bctx->last_block_no = block_no;
      bctx->last_media_id = bctx->media_id;

      if (bctx->bio->media && bctx->bio->media->media_id != 0U) {
        runtime_media_id = bctx->bio->media->media_id;
      }

      /* Some firmware returns DEVICE_ERROR on stale MediaId. Try runtime id too. */
      if (efi_status_is_error(st) && runtime_media_id != 0U &&
          runtime_media_id != bctx->media_id) {
        bctx->media_id = runtime_media_id;
        st = is_write ? efi_write_try(bctx, bctx->media_id, block_no, io_buf)
                      : efi_read_try(bctx, bctx->media_id, block_no, io_buf);
        bctx->last_status = st;
        bctx->last_block_no = block_no;
        bctx->last_media_id = bctx->media_id;
      }

      /* Final fallback: handoff MediaId.
       * For write+MediaId=0 fallback, require readback verification later. */
      if (efi_status_is_error(st) && handoff_media_id != bctx->media_id) {
        bctx->media_id = handoff_media_id;
        st = is_write ? efi_write_try(bctx, bctx->media_id, block_no, io_buf)
                      : efi_read_try(bctx, bctx->media_id, block_no, io_buf);
        bctx->last_status = st;
        bctx->last_block_no = block_no;
        bctx->last_media_id = bctx->media_id;
        if (is_write && handoff_media_id == 0U &&
            !efi_status_is_error(st)) {
          used_handoff_zero_write = 1;
        }
      }
    }
  }

  if (!efi_status_is_error(st) && used_handoff_zero_write) {
    EFI_STATUS_K vr = efi_read_try(bctx, bctx->media_id, block_no, io_buf);
    bctx->last_status = vr;
    bctx->last_block_no = block_no;
    bctx->last_media_id = bctx->media_id;
    if (efi_status_is_error(vr) ||
        !buffers_equal_local(io_buf, src_buf, bctx->block_size)) {
      st = (EFI_STATUS_K)(EFI_STATUS_ERROR_BIT_K | 7ULL /* DEVICE_ERROR */);
      bctx->last_status = st;
    }
  }

  if (efi_status_is_error(st)) {
    bctx->last_error_status = st;
    bctx->last_error_block_no = block_no;
    bctx->last_error_media_id = bctx->last_media_id;
    bctx->error_count += 1U;
    dbg_puts("[efi] io fail op=");
    dbg_putc(is_write ? 'w' : 'r');
    dbg_puts(" blk=");
    dbg_hex32(block_no);
    dbg_puts(" media=");
    dbg_hex32(bctx->last_media_id);
    dbg_puts(" code=");
    dbg_hex32((uint32_t)(st & 0xFFFFFFFFULL));
    dbg_putc('\n');
    return -1;
  }

  if (!is_write && io_buf != buffer) {
    if (copy_bytes_local(buffer, io_buf, bctx->block_size) != 0) {
      return -1;
    }
  }
  return 0;
}

static int efi_block_read(void *ctx, uint32_t block_no, void *buffer) {
  struct efi_block_ctx *bctx = (struct efi_block_ctx *)ctx;
  if (!bctx || !bctx->bio || !bctx->bio->read_blocks || !buffer) {
    return -1;
  }
  return efi_do_io(bctx, block_no, buffer, 0);
}

static int efi_block_write(void *ctx, uint32_t block_no, const void *buffer) {
  struct efi_block_ctx *bctx = (struct efi_block_ctx *)ctx;
  uint32_t verify_media_id = 0;
  EFI_STATUS_K verify_status = 0;
  if (!bctx || !bctx->bio || !bctx->bio->write_blocks || !buffer) {
    return -1;
  }
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (efi_do_io(bctx, block_no, (void *)buffer, 1) != 0) {
      return -1;
    }
    if (bctx->bio->flush_blocks) {
      EFI_STATUS_K st = bctx->bio->flush_blocks(bctx->bio);
      if (efi_status_is_error(st)) {
        /* Some firmware paths report flush as unsupported/error even when
         * WriteBlocks succeeded. Keep write success and expose status for debug. */
        bctx->last_status = st;
      }
    }

    verify_media_id = bctx->media_id;
    if (bctx->bio->media && bctx->bio->media->media_id != 0U) {
      verify_media_id = bctx->bio->media->media_id;
    }
    verify_status = efi_read_try(bctx, verify_media_id, block_no,
                                 bctx->bounce_aligned);
    bctx->last_status = verify_status;
    bctx->last_block_no = block_no;
    bctx->last_media_id = verify_media_id;
    if (!efi_status_is_error(verify_status) &&
        buffers_equal_local(bctx->bounce_aligned, buffer, bctx->block_size)) {
      return 0;
    }
  }

  bctx->last_error_status =
      efi_status_is_error(verify_status)
          ? verify_status
          : (EFI_STATUS_K)(EFI_STATUS_ERROR_BIT_K | 7ULL /* DEVICE_ERROR */);
  bctx->last_error_block_no = block_no;
  bctx->last_error_media_id = verify_media_id;
  bctx->error_count += 1U;
  dbg_puts("[efi] verify fail blk=");
  dbg_hex32(block_no);
  dbg_puts(" media=");
  dbg_hex32(verify_media_id);
  dbg_puts(" code=");
  dbg_hex32((uint32_t)(bctx->last_error_status & 0xFFFFFFFFULL));
  dbg_putc('\n');
  return -1;
}

static struct block_device_ops g_efi_block_ops;
static int g_efi_block_ops_initialized = 0;

static void efi_block_init_ops(void) {
  if (g_efi_block_ops_initialized) {
    return;
  }
  g_efi_block_ops.read_block = efi_block_read;
  g_efi_block_ops.write_block = efi_block_write;
  g_efi_block_ops_initialized = 1;
}

int efi_block_device_init(struct efi_block_device *out, uint64_t bio_ptr,
                          uint32_t media_id, uint32_t block_size,
                          uint64_t last_lba) {
  if (!out || bio_ptr == 0 || block_size == 0 || last_lba == 0 ||
      last_lba > 0xFFFFFFFFULL) {
    return -1;
  }
  efi_block_init_ops();

  out->ctx.bio = (efi_block_io_protocol_k_t *)(uintptr_t)bio_ptr;
  out->ctx.media_id = media_id;
  out->ctx.handoff_media_id = media_id;
  out->ctx.block_size = block_size;
  out->ctx.io_align = 1;
  out->ctx.bounce_raw = out->ctx.bounce_inline;
  out->ctx.bounce_aligned = out->ctx.bounce_inline;
  out->ctx.last_status = 0;
  out->ctx.last_block_no = 0;
  out->ctx.last_media_id = out->ctx.media_id;
  out->ctx.last_error_status = 0;
  out->ctx.last_error_block_no = 0;
  out->ctx.last_error_media_id = 0;
  out->ctx.error_count = 0;

  if (out->ctx.bio->media) {
    uint32_t media_block_size = out->ctx.bio->media->block_size;
    if (media_block_size != 0 && media_block_size <= 4096 &&
        is_power_of_two_u32(media_block_size)) {
      out->ctx.block_size = media_block_size;
    }
    uint32_t media_io_align = out->ctx.bio->media->io_align;
    if (media_io_align > 1 && media_io_align <= 4096 &&
        is_power_of_two_u32(media_io_align)) {
      out->ctx.io_align = media_io_align;
    }
  }

  if (out->ctx.io_align < 64 || out->ctx.io_align > 4096 ||
      !is_power_of_two_u32(out->ctx.io_align)) {
    out->ctx.io_align = 4096;
  }
  out->ctx.bounce_aligned =
      align_ptr_local(out->ctx.bounce_raw, out->ctx.io_align);
  if (!out->ctx.bounce_aligned) {
    /* Last-resort deterministic alignment. */
    out->ctx.io_align = 64;
    out->ctx.bounce_aligned = align_ptr_local(out->ctx.bounce_raw, 64);
    if (!out->ctx.bounce_aligned) {
      return -1;
    }
  }
  out->ctx.last_media_id = out->ctx.media_id;

  out->dev.name = "efi-block";
  out->dev.block_size = out->ctx.block_size;
  out->dev.block_count = (uint32_t)(last_lba + 1ULL);
  out->dev.ctx = &out->ctx;
  out->dev.ops = &g_efi_block_ops;
  return 0;
}

int efi_block_device_flush(struct efi_block_device *dev) {
  if (!dev || !dev->ctx.bio || !dev->ctx.bio->flush_blocks) {
    return -1;
  }
  EFI_STATUS_K st = dev->ctx.bio->flush_blocks(dev->ctx.bio);
  if (st & EFI_STATUS_ERROR_BIT_K) {
    return -1;
  }
  return 0;
}
