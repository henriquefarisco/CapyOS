# Etapa 10: audio development checkpoint

Development branch: `feature/etapa-10-audio-multimedia` in CapyOS,
CapyCodecs and CapyUI. This is development evidence, not a release acceptance
record. The published Etapa 9 release is not replaced by these lab artifacts.

## Review fixes (2026-09-13, unreleased)

- `ISO_REUSE_X64_VARIANT=1` defaults to `build/ci/CapyOS-Smoke-UEFI.iso`
  and its own last-built pointer. VMware and A/B diagnostic consumers select
  these outputs explicitly; a missing diagnostic ISO cannot fall back to an
  older official image. The canonical installer rejects reused variants,
  diagnostic flags and known boot hooks, including FP/TWO_BUSY.
- `smoke-x64-fp-context` has dedicated FP ISO/pointer names and no global clean.
  The variant fingerprint rebuilds its affected objects while preserving the
  canonical ISO. Its XMM0 cookie tests cooperative context switches, not the
  complete FP control/register state or all preemptive/SMP paths.
- Full desktop builds check source completeness even when the kernel/object
  cache exists. CapyUI owns the player and its desktop wiring; the coordinated
  local source commits are CapyUI `1bfcb24321ccc9ff592e8186df703b7a00d7be95`
  and CapyCodecs `551f24050957631d4d8199034cda8e9e55ad541c` (2026-09-14).
  A clean-source build must use these owners, not the older published sources.
  Do not make the player optional to hide an incomplete export.
- Private Vorbis synthesis sizes overlap scratch for both the previous and
  current block. Residue decoding expands a full classword before selecting
  its partial-group prefix. Regression gates cover block transitions,
  multiclass residues and an Ogg with transients against FFmpeg.
- The full QEMU installer harness now recognizes an exact display/serial pair
  as one disk. Same-format repeats, conflicting indices/identities and equal
  capacities remain rejected. This fixes a harness refusal before disk writes,
  not an installer storage-policy relaxation. A/B console test doubles now
  model the command echo required by the existing anti-stale-response policy.

Run `make test-installer-variant`, `make audio-selftest`, CapyUI `make validate`
and `make lint-desktop-session`, and CapyCodecs `make validate`,
`make vorbis-reference-test` and `make vorbis-pcm-reference-test`.
These fixes do not expose Ogg in the public WAV-only ABI/player, change signing
policy, or close the Etapa 10 acceptance criteria. Local runtime evidence and
remaining delivery boundaries are recorded in the review-fix report under
`build/fix-review-20260913/`.

The coordinated commits are local development closure only: no push, merge,
tag or publication is implied. Public versions and immutable package pins stay
unchanged; a later release must coordinate them through the publishing gates.

## Implemented surface

- CapyCodecs owns pure `capy-codec-audio` ABI v1 and bounded WAV decoding.
- CapyOS owns the bounded mixer, audio service, Intel HDA controller/DMA and
  runtime smoke.
- CapyUI owns the initial eight-item Media Player queue and desktop/file-manager
  entry points.

WAV playback now feeds a 64 KiB cyclic DMA ring in retired 4096-byte fragments
and stops at natural EOF. The source is bounded to 8 MiB and fully decoded in
memory: this is streaming output, not unbounded/incremental disk decoding.
The file path temporarily holds both encoded and decoded buffers (up to roughly
16 MiB, plus fixed mixing/DMA storage). Only 48 kHz mono/stereo S16 is accepted
by the service. CapyCodecs remains the sole WAV parser, using its unchanged ABI.

The service plays one active source; the mixer core supports multiple streams.
WAV volume changes apply to upcoming fragments without restarting playback,
with up to one ring of queued-gain latency. The diagnostic tone still loops and
restarts on volume change. This is not a finished multimedia stack.

## Reproduction and artifact discipline

Use `make audio-selftest CC=gcc` under Linux/WSL for the focused host checks.
Build the lab kernel and ISO with identical smoke flags in both invocations:

```sh
make all64 iso-uefi TOOLCHAIN64=elf PROFILE=full \
  ISO_REUSE_X64_VARIANT=1 EXTRA_CFLAGS64=-DCAPYOS_AUDIO_PLAYBACK_SMOKE
strings build/capyos64.bin | grep -F '[smoke] audio-playback-roundtrip ready'
```

Changing flags alone does not guarantee that existing objects are rebuilt. Use
the maintained clean smoke target when switching variants, or explicitly rebuild
the affected object during diagnosis. Do not run concurrent builds over this
build directory. ISO_REUSE_X64_VARIANT preserves the variant-selection policy,
but does not prevent Make from recompiling a newer source without missing flags.

The QEMU gate records PCM through the WAV backend and checks non-silence and
gaps, in addition to serial success. For VMware, provision a disposable disk with
the exact kernel, convert it with qemu-img, and configure UEFI, SATA and
`sound.virtualDev = "hdaudio"`. The generic VMware harness does not provision
or update a supplied disk: its existence check is not proof of kernel identity.
Use `--fail-marker '[smoke] audio-playback-roundtrip FAIL'` with the ready marker.

## Diagnosis on 2026-09-05

The complete host unit suite passed (`build/ci/etapa10-unit-run.log`). The focused
audio selftest and six Python harness regressions also passed. These host checks
do not execute actual HDA MMIO.

1. A fixed spin deadline could end before the two seconds required by the smoke.
   It was replaced with the existing monotonic timebase and a five-second bound.
2. An ISO rebuild silently removed the smoke flag from a recompiled kernel.
   Both audio targets now carry the flag through ISO generation and reject a
   binary without the marker. The boot without the marker is invalid evidence.
3. VMware stopped at the first 4096-byte descriptor with BCIS set. Acknowledging
   the RW1C completion in polling allowed progress to the entire 48000-byte
   buffer. Error bits remain available for diagnostics.
4. The smoke rejected LPIB equal to CBL. Intel documents counting through CBL
   before wrap; the test now rejects only positions above CBL and still requires
   subsequent decreases, at least two wraps, two seconds and clean stop.

References: [Intel HDA specification](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf),
[Intel SDLPIB/SDCBL register descriptions](https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/7-series-chipset-pch-datasheet.pdf).

Evidence before the LPIB boundary correction:

- QEMU timed smoke: 85250 non-silent stereo frames, no long gaps detected.
  Kernel SHA-256: `ef52d589dfb0c39548c4f91ed5357a96849df4721b7731f28df2336f5ef900c0`.
- QEMU BCIS acknowledgement smoke: 95820 non-silent stereo frames, no long gaps.
  Kernel SHA-256: `24287c5995e78fb67e09b7d0479610073d005051d1ed06a5fb268140c78a5fce`.
- VMware failures retained in `build/ci/etapa10-vmware-audio/` as
  `serial-failure-1.log`, `serial-failure-timed.log` and `serial-failure-ack.log`.

After the boundary correction, QEMU passed with 95573 non-silent stereo frames
and VMware passed the two-second/multiple-wrap/clean-stop tone smoke. Both used
kernel SHA-256 `18ec67e515b860b1a1c634b6283ad9e33dc286ecf87b2c8d2292cdba8f591a16`.
Logs: `build/ci/etapa10-hda-boundary.log`,
`build/ci/etapa10-vmware-audio/summary-boundary.log`.
VMware configuration: Workstation 26, virtual HW 22, UEFI with Secure Boot off,
2 vCPUs, 1024 MiB RAM, SATA disposable VMDK, E1000 NAT, HDA using Windows Wave
backend, COM1 file logging. This is DMA evidence, not host-output capture.

The subsequent cooperative service pump runs in the existing desktop owner
loop, including frames with no input/damage. Its host tests cover normal
completion servicing, descriptor-error stop and no hardware access while idle.
This service path then passed QEMU and VMware with kernel SHA-256
`d30cb3613950ed5d52c859e32c560941626eabdfab96ac2dfdca7b76d7a0a753`.
QEMU captured 95664 non-silent stereo frames without long gaps; capture SHA-256
`72ecb269e0f5b0ccd0ca340cd100790056bfdca540a68ae2e19dcc3949ee39d1`.
Logs: `build/ci/etapa10-hda-pump.log` and
`build/ci/etapa10-vmware-audio/summary-pump.log`. The VM was powered off after
the successful run (`vmrun list`: zero running VMs). The service is exercised
by the pre-login smoke; interactive Media Player/desktop validation is still
pending. No public push, release, tag or protection change was made.

Earlier pump reproduction inputs retained locally: `build/ci/etapa10-hda-pump.img`,
`build/ci/etapa10-vmware-audio/audio-pump.vmdk` and its `audio.vmx`.
Obsolete retry/timed/ack raw disks and retry/ack VMDKs were removed; their logs
and captured WAV evidence remain. Those disposable images are regenerable.

## Remaining acceptance work

- Validate the cooperative service pump through the UI, playlist progression
  and concurrent multi-app playback. Long files need incremental source I/O.
- Implement and validate AC97 and USB Audio class as required by the plan.
- Add a bounded pure OGG/Vorbis decoder and runtime WAV/OGG playback evidence.
- Complete playlist progression and basic visualization, with UI runtime tests.
- Synchronize versions, pins, contracts and matrix after the integrated slice is
  validated; run required release gates before any publication.

No full Etapa 10 acceptance, physical-machine portability or VMware host-output
capture has been established by these smokes.

## Streaming output slice (2026-09-05)

The ring uses byte progress and retired-fragment counts. It rejects positions
above CBL, polling gaps >=250 ms (ring duration is about 341 ms), DMA stalls and
unsafe refill windows. Stop, EOF, decode errors and allocation failures release
the service-owned decoded source; DMA storage has independent lifetime.
Manual stop and natural EOF are distinguished by `audio_service_status.completed`.
The file loader accepts bounded short reads (64 KiB requests, at most 512 calls),
rejects directories/oversized files and stops old playback before synchronous I/O.

`make audio-selftest CC='gcc -fsanitize=address,undefined -fno-omit-frame-pointer'`
passed. Coverage includes three ring cycles, gain change without restart, EOF,
poll deadline, invalid position, stalled DMA, resource cleanup, short VFS reads,
truncated reads, allocation failures and seven Python capture/harness checks.

The in-memory WAV smoke passed in QEMU and VMware with kernel SHA-256
`b130695d66c6a5bf9e7f1bc0b7639710b367e71fe20a129434d0a103ad620f27`.
The three-second 144000-frame WAV contains 240/300/400 Hz sections. QEMU captured
142366 non-silent stereo frames with transition counts `[239, 299, 399]` in the
three half-second sample windows and no long silence gaps. Capture SHA-256:
`e35f8ca140819132c4b80807c6c3af5f8e1796ceadc658a9a5152a41a4dfd108`.
Logs: `build/ci/etapa10-stream.log` and
`build/ci/etapa10-vmware-audio/summary-stream.log`. VMware observed EOF and clean
stop; no host PCM capture was made there. Both VMs stopped after the test.

The subsequent VFS fixture build also passed QEMU and VMware, using the same
`audio_service_play_wav_file` entry point as Media Player. The lab creates
`/audio-smoke.wav` without overwriting an existing path, writes the three-section
fixture, reads it through the VFS service path, streams to EOF, stops and removes
the fixture. Required serial markers are file-loaded, EOF frames=144000 and ready.
Kernel SHA-256:
`e19ba1b67d74ef52dc3105fc3108b43bbd32def0ff113b8d986a6dbde7d2870e`.
QEMU captured 142553 non-silent stereo frames, transition counts `[239, 299, 399]`
and no long gaps. Capture SHA-256:
`f126be97e67a3f0d6584075a320d15c90d7dabbffb4762fc60e3ded966a0c1b4`.
Evidence: `build/ci/etapa10-stream-file.log`, `build/ci/etapa10-stream-file.wav`,
`build/ci/etapa10-vmware-audio/summary-stream-file.log`.
Final disposable inputs: `build/ci/etapa10-stream-file.img` and
`build/ci/etapa10-vmware-audio/audio-stream-file.vmdk` (referenced by `audio.vmx`).
The intermediate in-memory-only stream raw/VMDK images were removed; logs and
capture remain. QEMU and VMware were stopped. The final sanitizer run additionally
covered the HDA wallclock wrap. No external codec ABI, production version or
release changed; implementation remains on the development branch.

Next: wire EOF/progress into the interactive Media Player and validate that UI
path, then compressed codec, AC97/USB Audio and the remaining milestone gates.

## Playlist/progress implementation (2026-09-05, runtime gate failing)

Media Player now advances from the playing track on natural EOF, independently
of the selected row, and paints percentage progress. Manual stop, playback
failure and another playback owner cancel automatic advancement. A service
playback ID prevents stale EOF from advancing a different playlist; gain changes
preserve that identity. Window close stops only playback owned by that window.

The actual UI callbacks are covered by `tests/audio/test_media_player.c`, including
progress pixels, selection changes, exactly-once EOF, stop, failure and ownership.
The complete `audio-selftest` passed with AddressSanitizer and UndefinedBehaviorSanitizer,
including explicit identity preservation across tone gain changes.

`smoke-x64-qemu-media-player-playlist` builds a gated kernel which creates two
disposable WAV fixtures, opens the real desktop and player, pumps audio and
requires intermediate progress for both tracks and final EOF after rendered
frames. It removes both fixtures after desktop shutdown. This is separate from
the captured-PCM continuity gate: playlist transition is not claimed gapless.
The new gate failed in QEMU; it must not be reported as accepted.

### First desktop failure and localized boundary

The first kernel (`70a94236b64f1c0f14fc84416389b5452bd43e485cac549c69582360fbe11196`)
booted the desktop but stopped playback with `stream underrun or invalid position`.
Evidence: `build/ci/etapa10-player.log` and `.debugcon.log`.

The diagnostic kernel SHA-256 is
`d1e126dda09aa9e868481b3d62a183d9f4e4adbfa3b0fc495f701286663af35e`.
`build/ci/etapa10-player-diag.log` shows stream start and entry to background work
at guest tick 1071, then return at tick 1125. The HDA poll gap was 12987032 ticks
(541.1 ms at 24 MHz), with valid ring position 44224. This exceeds both the
250 ms fail-closed poll deadline and the approximately 341 ms ring duration.
The foreground call to `x64_kernel_runtime_poll_background()` executes synchronous
service/work-queue I/O while desktop preemption is disabled. The serialized log
shows encrypted storage operations during this interval. Attribution to a specific
service callback has not yet been instrumented.

This is a real scheduling/integration defect, not a playlist unit-test failure or
permission to relax the deadline. Do not merely skip background services or drain
them before a short smoke to hide the problem. Next work must isolate bounded
audio servicing from synchronous background I/O, define serialized ownership of
start/stop/gain/status/refill, and test stop/close/EOF races. Re-run sanitizer tests,
the two-track desktop gate and the captured-PCM gate before escalating to VMware.
Current changes still have no public release or external codec ABI impact.

The diagnostic disposable disk is `build/ci/etapa10-player-diag.img`; the original
failed-run disk was removed after shutdown, retaining logs. No VMware UI gate was
run because the lower-cost QEMU gate failed. Earlier WAV-only VMware evidence
does not establish acceptance of this desktop integration.

## Native desktop audio servicing (2026-09-06)

The desktop failure was traced through three independent timing boundaries:

1. Synchronous background I/O ran inside the compositor preemption guard.
2. The first software-rendered frame could itself exceed the DMA ring duration.
3. Native boot left PIT IRQ0 deferred; APIC initialization was also deliberately
   deferred. Selecting PRIORITY policy alone did not deliver scheduler ticks.
   Desktop entry could additionally inherit IF=0 from the bootstrap.

Changes remain on `feature/etapa-10-audio-multimedia` in CapyOS/CapyUI:

- `audio_runtime.c` serializes the engine with acquire/release access and a
  non-spinning high-priority task pump. The task has no borrowed session pointer,
  performs no VFS/decode operation, and never yields while owning the engine.
  Foreground start/stop/gain remain a single-control-owner interface. Concurrent
  commands are rejected, not queued; this is not the multi-app mixer service yet.
- Background polling occurs with the compositor quiescent, outside its guard.
  A bounded, non-yielding render service hook pumps audio between scanline batches
  and paint phases without permitting concurrent window mutation. Shutdown clears
  that hook. Arbitrarily slow application paint callbacks are not solved by this
  mechanism and remain subject to the existing underrun deadline.
- Late desktop activation uses the already-programmed native PIT, preserving the
  pre-activation tick time domain. The PIC receives EOI before deferred scheduling.
  Firmware/deferred-IDT and APIC-owned paths are refused rather than overwritten.
  After native readiness, desktop entry enables IRQs and restores caller IF on exit.
- IRQ entry saves/restores aligned 512-byte FXSAVE64 state on the interrupted
  task stack. The kernel enables x87/SSE in entry64 and does not enable AVX.
- A missing native timer/worker disables playback safely; it does not block the
  desktop. Stop/status remain available and later initialization can retry.

The regression smoke now verifies actual preemption (no foreground yield), with
XMM15 deliberately clobbered by the worker and recovered in the foreground, then
plays two WAV files to EOF with intermediate progress and real compositor frames.
It continues running background services and does not relax the 250 ms deadline.

Validation before the final optional-audio fallback adjustment:

- Full host unit binary passed, including the new compositor hook cadence/idle/
  shutdown tests: `build/ci/etapa10-player-unit2.log`. The earlier unit build was
  interrupted with SIGHUP and was rerun; it is not counted as a pass.
- ASan/UBSan `audio-selftest` passed, including serialized reentry during VFS I/O,
  worker allocation failure/idempotence, ordered device/EOI/scheduling, engine,
  HDA core, UI playlist and eight Python capture/negative tests.
- QEMU and VMware passed with kernel SHA-256
  `43fc6339f2e1a2969caf3946296657cc3f1ee43b4c295c6d03c97d3877449ab9`.
  Evidence: `etapa10-player-native.log`, `etapa10-player-final.log` under
  `build/ci`, and `build/ci/etapa10-vmware-audio/summary-player-native.log`.
- QEMU's first WAV capture contains 283549 active stereo frames, no silent gap,
  and the six expected sections. The original playlist verifier incorrectly
  required a loading gap. It now accepts concatenated PCM or one loading gap,
  while rejecting missing/replayed sections, internal gaps and stereo mismatch.
  Both forms have synthetic tests. The end-to-end capture gate was rerun and
  passed; this is not a claim of gapless wall-clock playback between files.
  First capture SHA-256:
  `678a24bad4cacbe3a9f81efc2b2d4a5f2b1ea1d4fc3fcc0b5ed469f67a42e833`.

The fallback-adjustment rerun exposed an intermittent 553.7 ms service gap
(`build/ci/etapa10-player-closure.log`, kernel SHA-256
`8e25c3771c270a0255d2b66c2cad2f3f783e44e1b0d586018dc6a89d360b90b4`).
The earlier successful runs do not establish that task/render-hook servicing
alone is sufficient. This failure supersedes that interpretation.
No production version, external codec ABI, pin, release or remote branch changed.
Next milestone work still includes OGG/Vorbis, AC97/USB Audio, multi-app servicing,
incremental long-file input, wider desktop regressions and coordinated release gates.

## Bounded timer servicing under protected frames (2026-09-06)

The native timer now invokes a bounded audio phase after PIC EOI and before
scheduling. It tries the same engine gate, never spins, and performs only device
status reads, position accounting and at most one ring of fragment refills.
It never decodes, accesses the VFS, allocates, frees, logs, waits for hardware,
or mutates desktop state. EOF/error requests clear RUN without waiting and leave
a pending result. The task-context pump confirms the stop and performs cleanup.
Manual stop/new playback cancels a pending result from the old generation.
The 250 ms gap rejection and unsafe-fragment checks remain unchanged.

The host tests assert that IRQ completion/error retains PCM until task cleanup,
does not call blocking stop or allocation/logging, and skips the foreground-owned
engine during VFS reentry. ASan/UBSan `audio-selftest` passed, including HDA core,
engine, runtime, player, IRQ ordering and eight capture-contract tests:
`build/ci/etapa10-player-guarded-host.log`.

The real desktop smoke additionally holds the scheduling guard for 500 ms while
playing, without polling/yielding, and requires playback progress with zero task
dispatches. This is longer than the 341 ms DMA ring and exercises IRQ servicing
independently of both the worker and compositor hooks. IRQs remain enabled.
The existing timer-preemption/SIMD test and two-track rendering/EOF gate remain.

Artifact with the protected-frame stress test:

- Kernel SHA-256:
  `a7d26f46e9674728d231ab5ee07f618a9fae125af331c0bc494a29ae38292705`.
- ISO SHA-256:
  `b4ef80c6a8e53271d514f295289a9edb09aa86689ba962830d58a1cf2015fa23`.
- Initial QEMU cold-boot gate passed, including `guarded-DMA ready`, two rendered
  tracks, EOF and captured stereo frequency sections `[239,299,399]` followed by
  `[240,300,400]`: `build/ci/etapa10-player-guarded.log` and `.wav`.
- A second QEMU run provisioned a new disk and passed the same runtime and
  captured-audio gates: `build/ci/etapa10-player-guarded-repeat.log` and `.wav`.
  Capture SHA-256: first
  `c804a82010dfb1b8e34d323b2596a9e897562c4377db84efdbde7f69b3824329`,
  repeat `f419c092bb0168be224b8c57aa805796e8317ef159a5beda7b7ecc618a909045`.
- VMware Workstation 26.0.0 passed all four ordered markers (preemption/SIMD,
  guarded DMA, rendered two tracks, final ready) with the same kernel converted
  to `build/ci/etapa10-vmware-audio/audio-guarded.vmdk`. Configuration: UEFI,
  Secure Boot off, hardware 22, two CPUs, 1024 MiB, SATA, E1000 NAT and HDA.
  Evidence: `build/ci/etapa10-vmware-audio/summary-player-guarded.log`.
  The generic harness's DHCP wording is not a claim of a separate DHCP gate;
  this invocation explicitly checked the four audio/desktop markers. VMware
  has runtime/EOF evidence, not an independently analyzed host audio capture.
- The existing full host unit binary was executed again and passed:
  `build/ci/etapa10-player-guarded-unit.log`. Audio runtime/IRQ behavior is covered
  separately by the freshly rebuilt sanitizer tests and the VM gates above.
- All QEMU/VMware test processes stopped. Seven intermediate 2 GiB raw images
  (`bounded`, `capture`, `irq`, `native`, `pump`, `timer`, `worker`) were removed;
  logs/captures, the diagnostic and intermittent-failure disks, and final guarded
  raw/VMDK were preserved. Deleted scratch images require regeneration.

This is development evidence, not a published ISO or complete Etapa 10 acceptance.

## Ogg framing foundation (2026-09-06)

CapyCodecs now has a private, allocation-free Ogg packet reader with explicit
input/page/packet limits, page CRC validation and reconstruction across pages.
It rejects missing EOS, invalid continuation/sequence, multiplexing/chaining and
trailing data. The reader does not decode Vorbis and is not wired into the public
audio ABI or the CapyOS player. Production pins/versions/features remain unchanged.

Owner `make validate`, ASan/UBSan, a libogg-generated CRC fixture, all-prefix
truncation tests and 10000 deterministic malformed inputs passed. Freestanding
compilation produced no undefined symbols. Details and rerunnable test entry
points: `CapyCodecs/docs/40-implementation/ogg-reader.md`. This host-only internal
change does not replace the still-required Vorbis PCM and official VM gates.
The overall estimate remains approximately 68% until that integration exists.

## Codec progression and runtime remeasurement (2026-09-08)

CapyCodecs added a private caller-owned floor1 prediction/render plan and bounded
integer curve reconstruction. Output is inverse-dB table indices, not PCM or
spectral gains. Its tests passed 50000 mutations and an independent specification
oracle with 68540 curves / 2014903 bins. VQ reference coverage expanded to 110240
float patterns and 10880 vectors from 512 Xiph books; float unpacking now uses at
most ten exponentiation iterations rather than 788 sequential multiplies.
The full CapyCodecs suite was rebuilt with ASan/UBSan and passed. Freestanding
compilation and stack reports passed (416 bytes per floor1 function; 1152 bytes
per VQ expansion frame). See the owning repository's
`docs/40-implementation/vorbis-floor1.md` for raw host measurement methodology,
hashes and limitations. ABI/features/pins and the runtime adapter did not change.

Existing integrated WAV runtime was checked separately, using the already-built
guarded-DMA development kernel and ISO whose hashes were reverified above
(`a7d26f46...` kernel, `b4ef80c6...` ISO). No new kernel build or release is claimed:

- Fresh QEMU 10.2.1 disk boot, Q35/UEFI/OVMF, 512 MiB, SATA, E1000 and HDA:
  preemption/SIMD, guarded DMA, two rendered tracks and final ready passed.
  `build/ci/etapa10-player-measure-20260908-run.log` records captured sections
  `[239,301,400]` and `[240,300,400]`. The S16 stereo 48 kHz capture contains
  283794 frames (5.912375 seconds), equal channels, peak/RMS 6000, and no internal
  below-threshold gap. Capture SHA-256:
  `7bcbf0d2f239b388ce6ee28d1f7b158273eb959186a058eacf3c3d94e2f82e1c`.
  This passes the maintained capture-duration tolerance, not sample-exact six
  seconds or gapless wall-clock transitions; the backend can omit stopped time.
- A second fresh-disk QEMU run passed again, with sections `[240,300,400]` twice:
  `build/ci/etapa10-player-measure-repeat-20260908-run.log`. Its capture has
  281406 frames (5.862625 seconds), equal channels and zero internal gap, SHA-256
  `6056b4cf6f711cfec82bfd3c63b42f0672b3e7d26566ddb8d70089a73344a7e8`.
  Both runs used the harness default TCG accelerator (no override configured).
  The measured 49.75 ms capture-length difference is retained, not removed from
  evidence or treated as exact timing. Both disposable raw disks and temporary
  firmware state were removed by the harness after stopping QEMU; logs and WAVs
  remain. Regeneration is required to recover those scratch disks.
- VMware Workstation 26.0.0 build 25388281, existing disposable `audio.vmx` with
  guarded VMDK, UEFI/Secure Boot off, 2 CPUs, 1024 MiB, SATA, E1000 NAT and HDA:
  the same four runtime markers passed in order. Logs:
  `build/ci/etapa10-vmware-audio/player-measure-20260908-run.log` and
  `summary-player-measure-20260908.log`. This is runtime/EOF evidence, not a fresh
  install or analyzed VMware host-audio capture. Generic harness DHCP wording
  is not an additional DHCP acceptance claim.
- `make audio-selftest CC='gcc -fsanitize=address,undefined -fno-omit-frame-pointer -g'`
  passed IRQ ordering, HDA, engine/runtime, player and eight capture-contract
  tests: `build/ci/etapa10-audio-sanitized-20260908.log`.

Final read-only checks found no running QEMU process and VMware reported zero
running VMs. Existing source changes and diagnostic disks were preserved.

These tests do not exercise Vorbis in the kernel/player. The estimate remains
about 68%, with PCM integration, floor packet/gain stages, residue, transform/
overlap and the remaining stage acceptance criteria still open.

## Installer/desktop lifecycle regression (2026-09-08)

The first installed boot entering the desktop before setup was not an accepted
Etapa 10 intermediate state. The canonical installer ISO had been rebuilt from
an audio/media-player smoke kernel whose intentional pre-login hook starts the
desktop. Audio smoke targets now write dedicated images under `build/ci`, and
the canonical ISO recipe fails closed when either Etapa 10 boot hook is present.

The FULL-profile installer gate was also too permissive: it accepted either a
shell or desktop after login. QEMU and VMware installer drivers now have a
`--require-desktop-after-login` contract, enabled by all maintained FULL module
targets. Serial command validation is framed after the complete echoed command,
so a late prompt from the preceding interaction cannot truncate the next input
or create a false result.

Fresh runtime evidence with the clean canonical artifacts:

- Kernel SHA-256:
  `5e01dd46867a655879fdc8a9fea6f4a141ccc264456b9c26d860f29318efb407`.
- ISO SHA-256:
  `ad7e67068eef7cd6c0fb5b521d6db4a39b07a676306edf2effcf2154aa3b7331`.
- QEMU/WSL, Q35/UEFI/OVMF, fresh 2 GiB SATA disk, NAT/E1000: interactive
  first boot, 14/14 FULL modules, desktop autostart after login and persistence
  after the final reboot passed. Evidence is in
  `build/ci/smoke_x64_iso_install.{boot1,marker-write,boot2}.log`.
- VMware Workstation, UEFI, two fresh VMDKs, NAT/E1000: the same FULL lifecycle
  and persistence checks passed; the selected 2 GiB target changed, the 3 GiB
  guard remained byte-identical and the ISO hash before/after matched. Evidence:
  `build/ci/installer-wizard-desktop-full-evidence.manifest` and its referenced
  `smoke_x64_vmware_installer_3c56aadb34dd.*.log` files. The disposable VMware
  directory was removed after success.

This closes the installer/desktop regression only. At this checkpoint it did
not change the approximately 68% completion estimate or the remaining
codec/audio acceptance work for Etapa 10.

## Vorbis synthesis primitives (2026-09-09)

CapyCodecs now retains mapping type 0 state and implements the private inverse
coupling and floor1 inverse-dB/dot-product stages. The setup validator reuses the
retained mapping parser instead of maintaining a discard-only duplicate.
Resource bounds are 8 channels, 16 submaps, 256 coupling steps and 4096 spectral
bins; coupling and floor multiplication use caller-owned scratch and commit
atomically. Public audio ABI/features/version and CapyOS integration are
unchanged because no complete PCM decoder exists yet.

Owner evidence passed: strict `make validate`, focused ASan/UBSan, complete
libvorbis/specification oracle suite, 5000 retained mappings plus 14472 truncated
bit prefixes, 330501 inverse-coupled scalars, the pinned Xiph floor table and
129827 floor products. Freestanding analysis found only the mapping parser's
expected private bit-reader dependency; static frames are 672 bytes (mapping),
40 bytes (coupling) and 16 bytes (floor gain). CapyOS `make audio-selftest` and
`make all64 TOOLCHAIN64=elf` also passed against the updated sibling checkout.

No QEMU/VMware run is claimed for these private-only paths. Next is retained
aggregate setup/mode state and complete residue scheduling, followed by inverse
MDCT, window/overlap, bounded PCM output and then real player VM gates. This
advances the engineering estimate to approximately 72%, not Etapa acceptance.

## Retained setup and residue scheduling (2026-09-09)

The private Vorbis setup workspace now retains both Floor0/Floor1
configuration, all residue classifications/stage books, mapping state and mode
blockflags/references after successful framing. Its fixed maximum footprint is
164032 bytes and is decoder-owned, not placed on the call stack. Codebook lookup
payloads remain borrowed from the immutable setup packet. Partial workspace
state must be discarded after any hard parse failure.

CapyCodecs also schedules complete residue Types 0, 1 and 2: classword unpack,
eight passes, active-channel suppression, interleaved Type 2 application,
aggregate vector budget and nominal partial packet exhaustion. A separate
packet-prefix primitive selects retained modes and computes short/long window
transition bounds before the future transform.

Owner evidence passed: focused unit tests, ASan/UBSan for retained setup and
residue scheduling, GCC freestanding/analyzer builds, strict `make validate`,
all prior Xiph/specification oracles, 18 libvorbisenc setup packets with every
truncated byte prefix, and an independent 2000-schedule/300126-scalar residue
oracle. The residue scheduler frame report is 288 bytes with bounded dynamic
stack metadata; its bulk spectral/classification memory remains explicit caller
storage.

This remains private CapyCodecs work: audio ABI/features/version, package pin,
CapyOS player and release state did not change, so no VM playback gate is
claimed for this slice. Next is IMDCT/window coefficients and overlap-add,
followed by complete packet-to-PCM orchestration and QEMU/VMware playback.
Engineering progress is approximately 82%; Etapa 10 is not yet accepted.

## Vorbis window and overlap boundary (2026-09-09)

CapyCodecs now applies all legal Vorbis short/long window shapes without libm
and performs variable-size overlap-add with exact finished-sample counts. The
implementation uses caller-owned transactional scratch, rejects non-finite or
out-of-budget values and supports block sizes 64..8192. The independent oracle
matched 65280 binary32 window coefficients and 65280 overlap samples; focused
ASan/UBSan and freestanding/analyzer builds passed with no host symbols.

The maintained WSL host benchmark retained 303 batch samples for an
8192-sample window: median 166937.95 ns, p95 187277.7 ns and p99 206062.45 ns.
This is development throughput evidence, not VM real-time latency or an
invented acceptance threshold.

An integration constraint is now explicit: CapyOS ring-3 builds disable
x87/SSE/MMX because user-task context switches still do not preserve FP/SIMD
state. Kernel interrupt FXSAVE/FXRSTOR does not make floating-point userland
safe. The private float Vorbis path therefore remains disconnected until a
dedicated architectural context-switch gate is completed or a validated
fixed-point backend is selected. Public ABI/features/version and the player did
not change, and no VM playback claim is made for this private layer.

The exact current userland flags were probed against `vorbis_window.c`; GCC
rejected the build with `SSE register return with SSE disabled`, confirming the
guard is active rather than merely documented.

Next is the fast inverse MDCT plus the complete packet-to-PCM orchestrator.
Engineering progress is approximately 86%; Etapa 10 remains open.

## Fast inverse MDCT (2026-09-10)

CapyCodecs now has a bounded inverse MDCT for every legal Vorbis block size
64..8192. The Xiph-derived butterfly/bit-reversal structure retains its BSD
notice, while initialization was rewritten for caller memory and freestanding
polynomial trig generation. The maximum plan is 49152 bytes and per-channel
transform scratch is at most 32768 bytes. Plan/input/output/scratch overlap,
dimensions, indices and finite/range properties fail closed before output is
published.

The direct-form independent oracle matched 42 transforms and 51648 samples with
maximum absolute error `7.76e-07`. Focused ASan/UBSan and GCC freestanding
analysis passed; there are no unresolved symbols and the maximum frame is 192
bytes. The retained WSL benchmark for the final integrity-checked 8192-point
transform has 303 batch samples: median 45155 ns, p95 46595 ns and p99 54050 ns.

Public ABI/features/version and CapyOS runtime remain unchanged. The ring-3
FP/SIMD restriction still blocks safe player integration, so no VM playback
claim is made. Next is complete packet orchestration followed by an explicit
choice between per-task FP/SIMD state and a fixed-point codec backend.
Engineering progress is approximately 94%; Etapa 10 remains open.
