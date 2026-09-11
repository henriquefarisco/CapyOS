#include "audio/audio_engine.h"
#include "arch/x86_64/platform_timer.h"
#include "kernel/scheduler.h"
#include "kernel/task.h"

/* The current scheduler dispatches tasks on the BSP. The worker never yields
 * or permits preemption while holding this gate, so foreground stop cannot
 * encounter a suspended worker owning the engine. Foreground commands can do
 * synchronous decode/VFS I/O with the gate held: the worker skips, never spins.
 * Release/acquire also makes rejection safe for a future concurrent caller;
 * simultaneous control clients are not supported by this single-player API.
 * The timer may interrupt either context but skips an owned gate. Only the
 * bounded poll_irq phase runs there; cleanup never runs in an IRQ. */
static unsigned char engine_busy;
static struct task *audio_worker;
static int runtime_unavailable;
#ifdef CAPYOS_MEDIA_PLAYER_SMOKE
#include "arch/x86_64/timebase.h"
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
    __asm__ volatile(
        "1: rdtsc\n"
        "shlq $32, %%rdx\n"
        "orq %%rdx, %%rax\n"
        "cmpq %0, %%rax\n"
        "jb 1b\n"
        : : "r"(deadline) : "rax", "rdx", "cc", "memory");
    int rc = audio_service_get_status(&after);
    int ok = rc == 0 && after.playing && !after.last_error &&
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
    if (runtime_unavailable || !enter()) return -1;
    int rc = audio_engine_play_wav_memory(app, data, size);
    leave(); return rc;
}
int audio_service_play_wav_file(uint32_t app, const char *path) {
    if (runtime_unavailable || !enter()) return -1;
    int rc = audio_engine_play_wav_file(app, path);
    leave(); return rc;
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
