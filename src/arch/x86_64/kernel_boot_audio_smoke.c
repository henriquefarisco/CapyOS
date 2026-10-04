/* kernel_boot_audio_smoke.c — Etapa 10 lab-only audio playback smoke.
 *
 * This file is part of the kernel_main translation-unit group (see
 * include/arch/x86_64/kernel_main_internal.h). It holds the verbatim copy of
 * the CAPYOS_AUDIO_PLAYBACK_SMOKE block that previously lived inline in
 * kernel_main.c, extracted on 2026-09-21 to keep the entry point under the
 * source-layout size budget.
 *
 * Behavior-preservation contract:
 *   - The smoke runs after user_init, outside the fragile pre-COM1 /
 *     early-post-EBS window; the call site and its dbgcon marker stay in
 *     kernel_main64().
 *   - This TU carries the SAME `#pragma GCC optimize("O0")` as
 *     kernel_main.c so the extracted code keeps identical codegen
 *     characteristics to the evidence recorded in
 *     docs/operations/etapa-10-audio-development.md.
 *   - The whole body is gated by CAPYOS_AUDIO_PLAYBACK_SMOKE: production
 *     builds compile only the declarations pulled in by the headers below.
 */
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_main_internal.h"

#ifdef CAPYOS_AUDIO_PLAYBACK_SMOKE
#ifdef CAPYOS_AUDIO_OGG_SMOKE
#include "audio_ogg_fixture.h"
#define AUDIO_FIXTURE_PATH "/audio-smoke.ogg"
#define AUDIO_LOADED_MARKER "[smoke] audio-stream-ogg file-loaded\n"
#define AUDIO_EOF_MARKER "[smoke] audio-stream-ogg eof frames=144000\n"
#else
#define AUDIO_FIXTURE_PATH "/audio-smoke.wav"
#define AUDIO_LOADED_MARKER "[smoke] audio-stream-wav file-loaded\n"
#define AUDIO_EOF_MARKER "[smoke] audio-stream-wav eof frames=144000\n"
#endif
#include "arch/x86_64/timebase.h"
#include "audio/audio_output.h"
#include "audio/audio_service.h"
#include "drivers/serial/serial_com1.h"
#include "fs/vfs.h"
#include "kernel/log/klog.h"
#ifdef CAPYOS_MEDIA_PLAYER_SMOKE
#include "gui/desktop_runtime.h"
#endif

static void dbgcon_write(const char *s) {
  if (!s) {
    return;
  }
  while (*s) {
    dbgcon_putc((uint8_t)*s++);
  }
}

static void audio_smoke_log(const char *message) {
  com1_puts(message);
  dbgcon_write(message);
}

static int audio_smoke_fixture(const char *path, const uint8_t *data, size_t size) {
  struct file *fixture;
  size_t written = 0;
  if (vfs_create(path, VFS_MODE_FILE, 0) != VFS_OK) return -1;
  fixture = vfs_open(path, VFS_OPEN_WRITE);
  if (fixture) {
    while (written < size) {
      size_t request = size - written;
      long count;
      if (request > 65536u) request = 65536u;
      count = vfs_write(fixture, data + written, request);
      if (count <= 0 || (size_t)count > request) break;
      written += (size_t)count;
    }
    vfs_close(fixture);
  }
  if (written == size) return 0;
  (void)vfs_unlink(path);
  return -1;
}

int kernel_boot_run_audio_playback_smoke(void) {
#ifdef CAPYOS_AUDIO_MULTI_SMOKE
  return kernel_boot_run_audio_multi_smoke();
#endif
  /* Three distinct one-second sections prove source progression across many
   * DMA ring wraps, instead of replaying one short preloaded tone forever. */
  static uint8_t wav[44u + 48000u * 3u * 4u] = {
    'R','I','F','F',0,0,0,0,'W','A','V','E',
    'f','m','t',' ',16,0,0,0,1,0,2,0,0x80,0xbb,0,0,
    0,0xee,2,0,4,0,16,0,'d','a','t','a',0,0,0,0
  };
  struct audio_output_status status;
  struct audio_service_status audio;
  uint32_t spin;
  uint32_t previous_position;
  uint32_t started;
  uint32_t wraps = 0;
  uint64_t host_started;

  for (uint32_t i = 0; i < 4u; ++i) {
    wav[4u+i] = (uint8_t)((sizeof(wav) - 8u) >> (i * 8u));
    wav[40u+i] = (uint8_t)((sizeof(wav) - 44u) >> (i * 8u));
  }
  for (uint32_t i = 0; i < 144000u; ++i) {
    uint32_t period = i < 48000u ? 200u : (i < 96000u ? 160u : 120u);
    uint16_t sample = (uint16_t)((i % period) < period / 2u ? 6000 : -6000);
    wav[44u+i*4u] = wav[46u+i*4u] = (uint8_t)sample;
    wav[45u+i*4u] = wav[47u+i*4u] = (uint8_t)(sample >> 8);
  }

  audio_smoke_log("[smoke] audio-playback-roundtrip starting\n");
  /* Lab-only disposable-disk fixture. Never overwrite a pre-existing path. */
#ifdef CAPYOS_AUDIO_OGG_SMOKE
  const uint8_t *payload = audio_ogg_fixture;
  size_t payload_size = sizeof(audio_ogg_fixture);
#else
  const uint8_t *payload = wav;
  size_t payload_size = sizeof(wav);
#endif
  if (audio_smoke_fixture(AUDIO_FIXTURE_PATH, payload, payload_size) != 0) {
    audio_smoke_log("[smoke] audio-playback-roundtrip FAIL fixture-create\n");
    return -1;
  }
#ifdef CAPYOS_MEDIA_PLAYER_SMOKE
  {
    int rc;
    if (audio_smoke_fixture("/audio-smoke-next.wav", wav, sizeof(wav)) != 0) {
      (void)vfs_unlink(AUDIO_FIXTURE_PATH);
      audio_smoke_log("[smoke] media-player-playlist FAIL fixture\n");
      return -1;
    }
    rc = desktop_media_player_smoke_run();
    audio_service_stop();
    if (vfs_unlink(AUDIO_FIXTURE_PATH) != VFS_OK) rc = -1;
    if (vfs_unlink("/audio-smoke-next.wav") != VFS_OK) rc = -1;
    audio_smoke_log(rc == 0 ? "[smoke] media-player-playlist ready\n" :
                              "[smoke] media-player-playlist FAIL cleanup-or-desktop\n");
    return rc;
  }
#endif
  /* Backend-agnostic: the service picked HDA or AC'97; the same ring/position
   * contract (audio/audio_output.h) is checked either way. */
  if (audio_service_play_wav_file(0x534d4b45u, AUDIO_FIXTURE_PATH) != 0 ||
      audio_service_get_output_status(&status) != 0 ||
      status.state != AUDIO_OUTPUT_PLAYING) {
    if (audio_service_get_status(&audio) == 0)
      klog_hex(KLOG_WARN, "[audio-smoke] service-error=", (uint32_t)audio.last_error);
    audio_service_stop();
    (void)vfs_unlink(AUDIO_FIXTURE_PATH);
    klog_dump(audio_smoke_log);
    audio_smoke_log("[smoke] audio-playback-roundtrip FAIL start\n");
    return -1;
  }
  audio_smoke_log(AUDIO_LOADED_MARKER);
  previous_position = status.position_bytes;
  started = status.wallclock_ticks;
  host_started = x64_timebase_ticks_100hz();
  /* Use elapsed time for the timeout: a fixed spin count can finish before
   * two seconds on an accelerated VMware CPU but last much longer in TCG. */
  for (spin = 0; x64_timebase_ticks_100hz() - host_started < 500u; ++spin) {
    if ((spin & 0x3ffu) == 0u) {
      audio_service_poll();
      /* The snapshot goes through the engine gate; a busy gate is a skipped
       * sample, not a failure. */
      if (audio_service_get_output_status(&status) != 0) continue;
      if (audio_service_get_status(&audio) != 0 || audio.last_error) break;
      uint32_t elapsed = status.wallclock_ticks - started;
      if (audio.completed && !audio.playing && audio.played_frames == 144000u &&
          elapsed >= 48000000u && wraps >= 2u && status.state == AUDIO_OUTPUT_READY) {
        if (vfs_unlink(AUDIO_FIXTURE_PATH) != VFS_OK) break;
        audio_smoke_log(AUDIO_EOF_MARKER);
        audio_smoke_log("[smoke] audio-playback-roundtrip ready\n");
        return 0;
      }
      if (status.state != AUDIO_OUTPUT_PLAYING || status.stream_error ||
          !status.buffer_bytes || status.position_bytes > status.buffer_bytes)
        break;
      /* Positions may expose the ring size itself before wrapping to zero
       * (HDA SDLPIB/CBL contract, mirrored by the AC'97 backend). Count only
       * a later observed decrease. */
      if (status.position_bytes < previous_position) ++wraps;
      previous_position = status.position_bytes;
      if (elapsed >= 120000000u) break;
    }
    __asm__ volatile("pause");
  }
  klog_dec(KLOG_WARN, "[audio-smoke] position=", status.position_bytes);
  klog_dec(KLOG_WARN, "[audio-smoke] buffer=", status.buffer_bytes);
  klog_dec(KLOG_WARN, "[audio-smoke] wallclock-elapsed=", status.wallclock_ticks - started);
  klog_dec(KLOG_WARN, "[audio-smoke] wraps=", wraps);
  klog_dec(KLOG_WARN, "[audio-smoke] spins=", spin);
  klog_dec(KLOG_WARN, "[audio-smoke] stream-error=", (uint32_t)status.stream_error);
  audio_service_stop();
  (void)vfs_unlink(AUDIO_FIXTURE_PATH);
  klog_dump(audio_smoke_log);
  audio_smoke_log("[smoke] audio-playback-roundtrip FAIL dma-stalled-or-error\n");
  return -1;
}
#endif /* CAPYOS_AUDIO_PLAYBACK_SMOKE */
__asm__(".section .note.GNU-stack,\"\",@progbits");
