#ifndef KERNEL_BROWSER_SMOKE_H
#define KERNEL_BROWSER_SMOKE_H

/*
 * Smoke kernel-side do F3.3d (browser isolation).
 *
 * Quando o build define `CAPYOS_BOOT_RUN_BROWSER_SMOKE`, o kernel
 * exercita TODO o caminho ponta-a-ponta do browser:
 *
 *   1. `browser_engine_spawn()` cria 2 pipes, o processo capybrowser
 *      ring 3 e instala fds 0/1.
 *   2. Um `chrome_runtime` e inicializado e injeta `pipe_write` /
 *      `pipe_read` do kernel como pipe ops.
 *   3. Uma kernel task `browser-poller` e adicionada ao scheduler.
 *      Ela envia NAVIGATE, drena eventos e, ao receber NAV_READY,
 *      manda SHUTDOWN. Apos detectar EOF, imprime
 *      `[browser-smoke] OK` no debugcon e halta.
 *   4. O engine main_thread tambem entra no scheduler. O scheduler
 *      preemptivo intercala execucao das duas tasks.
 *
 * O harness QEMU em `tools/scripts/smoke_x64_browser_spawn.py`
 * espera por marcadores deterministicos no debugcon:
 *   - `[browser-smoke] spawn pid=N`
 *   - `[browser-smoke] navigate-sent`
 *   - `[browser-smoke] event NAV_STARTED`
 *   - `[browser-smoke] event FRAME`
 *   - `[browser-smoke] event NAV_READY`
 *   - `[browser-smoke] event PONG`
 *   - `[browser-smoke] shutdown-sent`
 *   - `[browser-smoke] engine-eof`
 *   - `[browser-smoke] OK`
 *
 * Falhas explicitas:
 *   - `[browser-smoke] FAIL spawn rc=N`
 *   - `[browser-smoke] FAIL protocol-err`
 *   - `[browser-smoke] FAIL timeout`
 *
 * Funcao noreturn: bloqueia o boot CPU em `hlt` apos enfileirar
 * as duas tasks. O scheduler preemptivo (APIC 100Hz) intercala
 * poller (ring 0) e engine (ring 3) ate o smoke completar e
 * halter via `cli; hlt` na poller task.
 */

void kernel_boot_run_browser_smoke(void) __attribute__((noreturn));

#endif /* KERNEL_BROWSER_SMOKE_H */
