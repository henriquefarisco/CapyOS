#include "audio/audio_engine.h"
#include "arch/x86_64/platform_timer.h"
#include "kernel/scheduler.h"
#include "kernel/task.h"

/* The current scheduler dispatches tasks on the BSP. The worker never yields
 * or permits preemption while holding this gate, so foreground stop cannot
 * encounter a suspended worker owning the engine. Decode/VFS I/O owns only a
 * request-local prepared source and runs WITHOUT this gate, so timer/worker
 * service remains available while another application is being decoded.
 * Release/acquire also makes rejection safe for a future concurrent caller;
 * simultaneous control callers are not supported: several applications may
 * have sources mixed at once, but commands come from one foreground owner.
 * The timer may interrupt either context but skips an owned gate. Only the
 * bounded poll_irq phase runs there; cleanup never runs in an IRQ. */
static unsigned char engine_busy;
static unsigned char preparation_busy;
static struct task *audio_worker;
static int runtime_unavailable;
#ifdef CAPYOS_MEDIA_PLAYER_SMOKE
#include "arch/x86_64/timebase.h"
#ifdef CAPYOS_AUDIO_OGG_SMOKE
#include "audio_ogg_fixture.h"
#endif
static uint32_t worker_dispatches;
#endif

static int enter(void) {
    return !__atomic_test_and_set(&engine_busy, __ATOMIC_ACQUIRE);
}
static void leave(void) { __atomic_clear(&engine_busy, __ATOMIC_RELEASE); }

/* The timer services only the bounded non-blocking engine phase. It never
 * frees PCM or touches desktop state. EOF/error cleanup stays on the worker. */
static void audio_timer_service(void) {
    if (enter()) {
        audio_engine_poll_irq();
        leave();
    }
}

void audio_service_poll(void) {
    scheduler_preempt_disable();
    if (enter()) {
        /* The legacy heap/VFS is not reentrant. IRQ-phase progress remains
         * available, but defer worker frees until preparation has returned. */
        if (__atomic_load_n(&preparation_busy, __ATOMIC_ACQUIRE))
            audio_engine_poll_irq();
        else
            audio_engine_poll();
        leave();
    }
    scheduler_preempt_enable();
}

static void audio_worker_entry(void *arg) {
    (void)arg;
    for (;;) {
        audio_service_poll();
#ifdef CAPYOS_MEDIA_PLAYER_SMOKE
        __atomic_add_fetch(&worker_dispatches, 1u, __ATOMIC_RELAXED);
        /* Deliberately clobber a caller-saved SIMD register. The interrupted
         * foreground must recover its value from its own IRQ stack image. */
        __asm__ volatile("pxor %%xmm15, %%xmm15" : : : "xmm15");
#endif
        task_sleep(1u);
    }
}

#ifdef CAPYOS_MEDIA_PLAYER_SMOKE
int audio_service_smoke_preemption(void) {
    uint32_t lo, hi;
    uint32_t before = __atomic_load_n(&worker_dispatches, __ATOMIC_RELAXED);
    uint64_t observed;
    uint64_t pattern = UINT64_C(0x123456789abcdef0);
    uint64_t hz = x64_timebase_hz();
    if (!hz) return -1;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t deadline = (((uint64_t)hi << 32) | lo) + hz / 2u;
    /* No calls/yields: only timer-driven preemption can run the worker. */
    __asm__ volatile(
        "movq %1, %%xmm15\n"
        "1: rdtsc\n"
        "shlq $32, %%rdx\n"
        "orq %%rdx, %%rax\n"
        "cmpq %2, %%rax\n"
        "jb 1b\n"
        "movq %%xmm15, %0\n"
        : "=r"(observed) : "r"(pattern), "r"(deadline)
        : "rax", "rdx", "xmm15", "cc", "memory");
    return observed == pattern &&
        __atomic_load_n(&worker_dispatches, __ATOMIC_RELAXED) - before >= 2u ? 0 : -1;
}

int audio_service_smoke_guarded_playback(void) {
    struct audio_service_status before, after;
    uint32_t lo, hi;
    uint64_t hz = x64_timebase_hz();
    if (!hz || audio_service_get_status(&before) != 0 || !before.playing)
        return -1;
    scheduler_preempt_disable();
    uint32_t dispatches = __atomic_load_n(&worker_dispatches, __ATOMIC_RELAXED);
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t deadline = (((uint64_t)hi << 32) | lo) + hz / 2u;
    /* Simulate a long protected paint callback: no task dispatch, polling or
     * yielding for 500 ms (longer than the DMA ring). IRQs remain enabled. */
#ifdef CAPYOS_AUDIO_OGG_SMOKE
    /* Join a real second Vorbis source through the public runtime, then stop
     * only that source. Muting the diagnostic app preserves the independent
     * PCM oracle for the original track; its playback ID/progress must survive. */
    const uint32_t diagnostic_app = 0x50525032u;
    struct audio_service_status second = {0};
    int decode_ok = audio_service_set_app_volume(diagnostic_app, 0) == 0 &&
        audio_service_play_wav_memory(diagnostic_app, audio_ogg_fixture,
            sizeof(audio_ogg_fixture)) == 0 &&
        audio_service_get_app_status(diagnostic_app, &second) == 0 &&
        second.playing && second.source_frames == 144000u;
    if (audio_service_stop_app(diagnostic_app) != 0) decode_ok = 0;
#else
    int decode_ok = 1;
#endif
    __asm__ volatile(
        "1: rdtsc\n"
        "shlq $32, %%rdx\n"
        "orq %%rdx, %%rax\n"
        "cmpq %0, %%rax\n"
        "jb 1b\n"
        : : "r"(deadline) : "rax", "rdx", "cc", "memory");
    int rc = audio_service_get_status(&after);
    int ok = decode_ok && rc == 0 && after.playing && !after.last_error &&
        after.playback_id == before.playback_id &&
        after.played_frames > before.played_frames + 12000u &&
        __atomic_load_n(&worker_dispatches, __ATOMIC_RELAXED) == dispatches;
    scheduler_preempt_enable();
    audio_service_poll();
    return ok ? 0 : -1;
}
#endif

int audio_service_start_worker(void) {
    runtime_unavailable = 1;
    if (!scheduler_running() || !task_current() ||
        x64_platform_timer_start_scheduler() != 0) {
        x64_platform_timer_set_service_hook(0);
        audio_service_stop();
        return -1;
    }
    scheduler_preempt_disable();
    if (!audio_worker) {
        audio_worker = task_create("audio-pump", audio_worker_entry, 0,
                                   TASK_PRIORITY_HIGH);
        if (audio_worker) {
            /* No borrowed desktop session pointer survives desktop teardown.
             * This worker touches only owned PCM/mixer/HDA, never the VFS. */
            audio_worker->active_session = 0;
            scheduler_add(audio_worker);
        }
    }
    scheduler_preempt_enable();
    runtime_unavailable = !audio_worker;
    x64_platform_timer_set_service_hook(audio_worker ? audio_timer_service : 0);
    if (runtime_unavailable) audio_service_stop();
    return audio_worker ? 0 : -1;
}

int audio_service_init(void) {
    if (runtime_unavailable || !enter()) return -1;
    int rc = audio_engine_init();
    leave(); return rc;
}
int audio_service_play_wav_memory(uint32_t app, const uint8_t *data, size_t size) {
    if (!app || runtime_unavailable ||
        __atomic_test_and_set(&preparation_busy, __ATOMIC_ACQUIRE)) return -1;
    struct audio_prepared_source prepared = {0};
    int rc = -1;
    if (audio_service_init() == 0) {
        (void)audio_prepare_memory(data, size, &prepared);
        scheduler_preempt_disable();
        if (enter()) {
            rc = audio_engine_start_prepared(app, &prepared, 0);
            leave();
        }
        scheduler_preempt_enable();
    }
    audio_prepared_release(&prepared);
    __atomic_clear(&preparation_busy, __ATOMIC_RELEASE);
    return rc;
}
int audio_service_play_wav_file(uint32_t app, const char *path) {
    if (!app || runtime_unavailable ||
        __atomic_test_and_set(&preparation_busy, __ATOMIC_ACQUIRE)) return -1;
    struct audio_prepared_source prepared = {0};
    int rc = -1;
    if (audio_service_init() == 0) {
        (void)audio_prepare_file(path, &prepared);
        scheduler_preempt_disable();
        if (enter()) {
            rc = audio_engine_start_prepared(app, &prepared, 1);
            leave();
        }
        scheduler_preempt_enable();
    }
    audio_prepared_release(&prepared);
    __atomic_clear(&preparation_busy, __ATOMIC_RELEASE);
    return rc;
}
int audio_service_play_test_tone(uint32_t app) {
    if (runtime_unavailable || !enter()) return -1;
    int rc = audio_engine_play_test_tone(app);
    leave(); return rc;
}
void audio_service_stop(void) {
    if (!enter()) return;
    audio_engine_stop();
    leave();
}
int audio_service_stop_app(uint32_t app) {
    if (!enter()) return -1;
    int rc = audio_engine_stop_app(app);
    leave(); return rc;
}
int audio_service_set_global_volume(uint16_t volume) {
    if (!enter()) return -1;
    int rc = audio_engine_set_global_volume(volume);
    leave(); return rc;
}
int audio_service_set_app_volume(uint32_t app, uint16_t volume) {
    if (!enter()) return -1;
    int rc = audio_engine_set_app_volume(app, volume);
    leave(); return rc;
}
int audio_service_get_status(struct audio_service_status *out) {
    if (!out || !enter()) return -1;
    int rc = audio_engine_get_status(out);
    if (runtime_unavailable && rc == 0) {
        out->available = 0;
        out->last_error = -6;
    }
    leave(); return rc;
}
int audio_service_get_app_status(uint32_t app, struct audio_service_status *out) {
    if (!out || !enter()) return -1;
    int rc = audio_engine_get_app_status(app, out);
    if (runtime_unavailable && rc == 0) {
        out->available = 0;
        out->last_error = -6;
    }
    leave(); return rc;
}
int audio_service_get_output_status(struct audio_output_status *out) {
    if (!out || !enter()) return -1;
    int rc = audio_engine_get_output_status(out);
    leave(); return rc;
}
int audio_service_get_app_levels(uint32_t app, uint16_t *left, uint16_t *right) {
    if (!app || !left || !right) return -1;
    scheduler_preempt_disable();
    int rc = -1;
    if (enter()) {
        rc = audio_engine_get_app_levels(app, left, right);
        leave();
    }
    scheduler_preempt_enable();
    return rc;
}
