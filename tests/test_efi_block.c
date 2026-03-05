#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/storage/efi_block.h"
#include "fs/block.h"

struct fake_state {
  EFI_STATUS_K first_status;
  EFI_STATUS_K second_status;
  EFI_STATUS_K third_status;
  EFI_STATUS_K fourth_status;
  int read_calls;
  int reset_calls;
  uint32_t media_id_call1;
  uint32_t media_id_call2;
  uint32_t media_id_call3;
  uint32_t media_id_call4;
  void *buffer_call1;
  void *buffer_call2;
  void *buffer_call3;
  void *buffer_call4;
  int mutate_media_id_on_first;
  uint32_t mutated_media_id;
  int fill_on_success;
};

static struct fake_state g_fake;
static efi_block_io_media_k_t g_media;
static efi_block_io_protocol_k_t g_bio;

static void fake_reset_state(void) {
  memset(&g_fake, 0, sizeof(g_fake));
  memset(&g_media, 0, sizeof(g_media));
  memset(&g_bio, 0, sizeof(g_bio));
}

static EFI_STATUS_K make_error_status(uint32_t code) {
  return EFI_STATUS_ERROR_BIT_K | (EFI_STATUS_K)code;
}

static EFI_STATUS_K __attribute__((ms_abi))
fake_reset(struct efi_block_io_protocol_k *This, uint8_t ExtendedVerification) {
  (void)This;
  (void)ExtendedVerification;
  g_fake.reset_calls++;
  return 0;
}

static EFI_STATUS_K __attribute__((ms_abi))
fake_read(struct efi_block_io_protocol_k *This, uint32_t MediaId, uint64_t Lba,
          uint64_t BufferSize, void *Buffer) {
  (void)Lba;
  g_fake.read_calls++;
  if (g_fake.read_calls == 1) {
    g_fake.media_id_call1 = MediaId;
    g_fake.buffer_call1 = Buffer;
    if (g_fake.mutate_media_id_on_first && This && This->media) {
      This->media->media_id = g_fake.mutated_media_id;
    }
    if (g_fake.fill_on_success && g_fake.first_status == 0) {
      uint8_t *p = (uint8_t *)Buffer;
      for (uint64_t i = 0; i < BufferSize; ++i) {
        p[i] = (uint8_t)((i & 0xFFU) ^ 0x5AU);
      }
    }
    return g_fake.first_status;
  }

  if (g_fake.read_calls == 2) {
    g_fake.media_id_call2 = MediaId;
    g_fake.buffer_call2 = Buffer;
    if (g_fake.fill_on_success && g_fake.second_status == 0) {
      uint8_t *p = (uint8_t *)Buffer;
      for (uint64_t i = 0; i < BufferSize; ++i) {
        p[i] = (uint8_t)((i & 0xFFU) ^ 0x5AU);
      }
    }
    return g_fake.second_status;
  }

  if (g_fake.read_calls == 3) {
    g_fake.media_id_call3 = MediaId;
    g_fake.buffer_call3 = Buffer;
    if (g_fake.fill_on_success && g_fake.third_status == 0) {
      uint8_t *p = (uint8_t *)Buffer;
      for (uint64_t i = 0; i < BufferSize; ++i) {
        p[i] = (uint8_t)((i & 0xFFU) ^ 0x5AU);
      }
    }
    return g_fake.third_status;
  }

  g_fake.media_id_call4 = MediaId;
  g_fake.buffer_call4 = Buffer;
  if (g_fake.fill_on_success && g_fake.fourth_status == 0) {
    uint8_t *p = (uint8_t *)Buffer;
    for (uint64_t i = 0; i < BufferSize; ++i) {
      p[i] = (uint8_t)((i & 0xFFU) ^ 0x5AU);
    }
  }
  return g_fake.fourth_status;
}

static EFI_STATUS_K __attribute__((ms_abi))
fake_write(struct efi_block_io_protocol_k *This, uint32_t MediaId, uint64_t Lba,
           uint64_t BufferSize, void *Buffer) {
  (void)This;
  (void)MediaId;
  (void)Lba;
  (void)BufferSize;
  (void)Buffer;
  return 0;
}

static EFI_STATUS_K __attribute__((ms_abi))
fake_flush(struct efi_block_io_protocol_k *This) {
  (void)This;
  return 0;
}

static void fake_setup_media(uint32_t media_id, uint32_t block_size,
                             uint32_t io_align, uint64_t last_block) {
  g_media.media_id = media_id;
  g_media.media_present = 1;
  g_media.block_size = block_size;
  g_media.io_align = io_align;
  g_media.last_block = last_block;

  g_bio.revision = 2;
  g_bio.media = &g_media;
  g_bio.reset = fake_reset;
  g_bio.read_blocks = fake_read;
  g_bio.write_blocks = fake_write;
  g_bio.flush_blocks = fake_flush;
}

static int test_retry_on_device_error_same_media_id(void) {
  fake_reset_state();
  fake_setup_media(11, 512, 1, 8191);
  g_fake.first_status = make_error_status(7);
  g_fake.second_status = 0;
  g_fake.third_status = 0;

  struct efi_block_device dev;
  if (efi_block_device_init(&dev, (uint64_t)(uintptr_t)&g_bio, g_media.media_id,
                            g_media.block_size, g_media.last_block) != 0) {
    printf("[efi_block] init failed (retry same media)\n");
    return 1;
  }
  if (dev.ctx.io_align != 4096U) {
    printf("[efi_block] io_align sanitization failed: %u\n", dev.ctx.io_align);
    return 1;
  }

  uint8_t out[512];
  memset(out, 0, sizeof(out));
  if (block_device_read(&dev.dev, 3, out) != 0) {
    printf("[efi_block] read failed when retry should recover\n");
    return 1;
  }
  if (g_fake.read_calls != 2) {
    printf("[efi_block] expected 2 reads, got %d\n", g_fake.read_calls);
    return 1;
  }
  if (g_fake.reset_calls != 0) {
    printf("[efi_block] unexpected reset call on retry\n");
    return 1;
  }
  if (g_fake.media_id_call1 != 11 || g_fake.media_id_call2 != 11) {
    printf("[efi_block] media id changed unexpectedly (%u -> %u)\n",
           g_fake.media_id_call1, g_fake.media_id_call2);
    return 1;
  }
  return 0;
}

static int test_retry_updates_media_id_after_change(void) {
  fake_reset_state();
  fake_setup_media(5, 512, 1, 8191);
  g_fake.first_status = make_error_status(13);
  g_fake.second_status = make_error_status(13);
  g_fake.third_status = 0;
  g_fake.mutate_media_id_on_first = 1;
  g_fake.mutated_media_id = 9;

  struct efi_block_device dev;
  if (efi_block_device_init(&dev, (uint64_t)(uintptr_t)&g_bio, g_media.media_id,
                            g_media.block_size, g_media.last_block) != 0) {
    printf("[efi_block] init failed (retry media change)\n");
    return 1;
  }

  uint8_t out[512];
  memset(out, 0, sizeof(out));
  if (block_device_read(&dev.dev, 4, out) != 0) {
    printf("[efi_block] read failed after media id change\n");
    return 1;
  }
  if (g_fake.read_calls != 3 || g_fake.reset_calls != 0) {
    printf("[efi_block] retry flow not triggered on media change\n");
    return 1;
  }
  if (g_fake.media_id_call1 != 5 || g_fake.media_id_call2 != 5 ||
      g_fake.media_id_call3 != 9) {
    printf("[efi_block] media ids mismatch after retry (%u -> %u -> %u)\n",
           g_fake.media_id_call1, g_fake.media_id_call2,
           g_fake.media_id_call3);
    return 1;
  }
  if (dev.ctx.media_id != 9 || dev.ctx.last_media_id != 9) {
    printf("[efi_block] context media id was not updated\n");
    return 1;
  }
  return 0;
}

static int test_retry_updates_media_id_after_device_error(void) {
  fake_reset_state();
  fake_setup_media(31, 512, 1, 8191);
  g_fake.first_status = make_error_status(7);
  g_fake.second_status = make_error_status(7);
  g_fake.third_status = 0;
  g_fake.mutate_media_id_on_first = 1;
  g_fake.mutated_media_id = 37;

  struct efi_block_device dev;
  if (efi_block_device_init(&dev, (uint64_t)(uintptr_t)&g_bio, g_media.media_id,
                            g_media.block_size, g_media.last_block) != 0) {
    printf("[efi_block] init failed (retry device error + media change)\n");
    return 1;
  }

  uint8_t out[512];
  memset(out, 0, sizeof(out));
  if (block_device_read(&dev.dev, 4, out) != 0) {
    printf("[efi_block] read failed after device error + media id change\n");
    return 1;
  }
  if (g_fake.read_calls != 3 || g_fake.reset_calls != 0) {
    printf("[efi_block] retry flow not triggered on device error media change\n");
    return 1;
  }
  if (g_fake.media_id_call1 != 31 || g_fake.media_id_call2 != 31 ||
      g_fake.media_id_call3 != 37) {
    printf("[efi_block] media ids mismatch on device error retry (%u -> %u -> %u)\n",
           g_fake.media_id_call1, g_fake.media_id_call2,
           g_fake.media_id_call3);
    return 1;
  }
  if (dev.ctx.media_id != 37 || dev.ctx.last_media_id != 37) {
    printf("[efi_block] context media id was not updated after device error\n");
    return 1;
  }
  return 0;
}

static int test_retry_falls_back_to_handoff_media_id(void) {
  fake_reset_state();
  fake_setup_media(99, 512, 1, 8191); /* runtime media id */
  g_bio.reset = NULL;                  /* force fallback path without reset */
  g_fake.first_status = make_error_status(7);
  g_fake.second_status = make_error_status(7);
  g_fake.third_status = make_error_status(7);
  g_fake.fourth_status = 0;

  struct efi_block_device dev;
  if (efi_block_device_init(&dev, (uint64_t)(uintptr_t)&g_bio, 1 /* handoff */,
                            g_media.block_size, g_media.last_block) != 0) {
    printf("[efi_block] init failed (handoff fallback)\n");
    return 1;
  }

  uint8_t out[512];
  memset(out, 0, sizeof(out));
  if (block_device_read(&dev.dev, 6, out) != 0) {
    printf("[efi_block] read failed on handoff fallback flow\n");
    return 1;
  }
  if (g_fake.read_calls != 4 || g_fake.reset_calls != 0) {
    printf("[efi_block] expected 4 reads on handoff fallback, got %d\n",
           g_fake.read_calls);
    return 1;
  }
  if (g_fake.media_id_call1 != 1 || g_fake.media_id_call2 != 1 ||
      g_fake.media_id_call3 != 99 || g_fake.media_id_call4 != 1) {
    printf("[efi_block] media ids mismatch on handoff fallback (%u -> %u -> %u -> %u)\n",
           g_fake.media_id_call1, g_fake.media_id_call2,
           g_fake.media_id_call3, g_fake.media_id_call4);
    return 1;
  }
  if (dev.ctx.media_id != 1 || dev.ctx.last_media_id != 1 ||
      dev.ctx.handoff_media_id != 1) {
    printf("[efi_block] handoff media id was not preserved\n");
    return 1;
  }
  return 0;
}

static int test_retry_falls_back_to_handoff_media_id_zero(void) {
  fake_reset_state();
  fake_setup_media(99, 512, 1, 8191); /* runtime media id */
  g_bio.reset = NULL;                  /* force fallback path without reset */
  g_fake.first_status = make_error_status(7);
  g_fake.second_status = make_error_status(7);
  g_fake.third_status = 0;
  g_fake.fourth_status = 0;

  struct efi_block_device dev;
  if (efi_block_device_init(&dev, (uint64_t)(uintptr_t)&g_bio, 0 /* handoff */,
                            g_media.block_size, g_media.last_block) != 0) {
    printf("[efi_block] init failed (handoff fallback zero)\n");
    return 1;
  }

  uint8_t out[512];
  memset(out, 0, sizeof(out));
  if (block_device_read(&dev.dev, 7, out) != 0) {
    printf("[efi_block] read failed on handoff-zero fallback flow\n");
    return 1;
  }
  if (g_fake.read_calls != 3 || g_fake.reset_calls != 0) {
    printf("[efi_block] expected 3 reads on handoff-zero fallback, got %d\n",
           g_fake.read_calls);
    return 1;
  }
  if (g_fake.media_id_call1 != 99 || g_fake.media_id_call2 != 99 ||
      g_fake.media_id_call3 != 0) {
    printf(
        "[efi_block] media ids mismatch on handoff-zero fallback (%u -> %u -> "
        "%u)\n",
        g_fake.media_id_call1, g_fake.media_id_call2, g_fake.media_id_call3);
    return 1;
  }
  if (dev.ctx.media_id != 0 || dev.ctx.last_media_id != 0 ||
      dev.ctx.handoff_media_id != 0) {
    printf("[efi_block] handoff media id zero was not preserved\n");
    return 1;
  }
  return 0;
}

static int test_bounce_alignment_and_copyback(void) {
  fake_reset_state();
  fake_setup_media(21, 512, 4096, 8191);
  g_fake.first_status = 0;
  g_fake.second_status = 0;
  g_fake.third_status = 0;
  g_fake.fill_on_success = 1;

  struct efi_block_device dev;
  if (efi_block_device_init(&dev, (uint64_t)(uintptr_t)&g_bio, g_media.media_id,
                            g_media.block_size, g_media.last_block) != 0) {
    printf("[efi_block] init failed (bounce)\n");
    return 1;
  }

  uint8_t raw[1024];
  memset(raw, 0, sizeof(raw));
  uint8_t *user_buf = &raw[1];
  if (block_device_read(&dev.dev, 1, user_buf) != 0) {
    printf("[efi_block] read failed (bounce)\n");
    return 1;
  }
  if (g_fake.read_calls != 1) {
    printf("[efi_block] unexpected retry count in bounce test\n");
    return 1;
  }
  if (g_fake.buffer_call1 == user_buf) {
    printf("[efi_block] firmware read used caller buffer instead of bounce\n");
    return 1;
  }
  if (((uintptr_t)g_fake.buffer_call1 % 4096U) != 0U) {
    printf("[efi_block] bounce buffer not aligned: %p\n", g_fake.buffer_call1);
    return 1;
  }
  if (user_buf[0] != 0x5A || user_buf[1] != 0x5B || user_buf[2] != 0x58) {
    printf("[efi_block] bounce copy-back pattern mismatch\n");
    return 1;
  }
  return 0;
}

int run_efi_block_tests(void) {
  int fails = 0;
  fails += test_retry_on_device_error_same_media_id();
  fails += test_retry_updates_media_id_after_change();
  fails += test_retry_updates_media_id_after_device_error();
  fails += test_retry_falls_back_to_handoff_media_id();
  fails += test_retry_falls_back_to_handoff_media_id_zero();
  fails += test_bounce_alignment_and_copyback();
  if (fails == 0) {
    printf("[tests] efi_block OK\n");
  }
  return fails;
}
