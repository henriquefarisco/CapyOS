#!/usr/bin/env python3
"""Audit CapyOS source layout for monoliths and boundary drift.

The goal is not to fail the current tree immediately. It gives us a stable,
repeatable map for incremental clean-code refactors.
"""

from __future__ import annotations

import argparse
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

INCLUDE_RE = re.compile(r'#\s*include\s+"([^"]+)"')


DEFAULT_ROOTS = ("src", "include", "tests", "tools", ".")
GENERATED_OR_VENDOR = (
    "build/",
    "third_party/",
    ".git/",
    ".codex/",
    ".claude/",
)

STATIC_DATA_EXCEPTIONS = {
    Path("src/security/tls_trust_anchors.c"),
}

MONOLITH_BASELINE_EXCEPTIONS = {
    # src/apps/settings.c was split in the 2026-05-15 refactor into
    # settings.c (lifecycle + click router), settings_view.c (paint
    # primitives + per-tab rendering) and settings_actions.c
    # (inline-prompt callbacks). All three translation units sit
    # under the 900-line limit.
    # src/apps/file_manager.c was split in the 2026-05-15 refactor
    # into file_manager.c (lifecycle + ops + click router),
    # file_manager_view.c (paint primitives) and file_manager_dnd.c
    # (drag-and-drop + context menu). All three translation units
    # sit under the 900-line limit.
    # src/security/crypt.c was split in the 2026-05-15 refactor into
    # crypt.c (core), crypt_kdf.c (PBKDF2 + AES-XTS key derivation),
    # crypt_aes_xts.c (AES + XTS + block adapter) and crypt_hkdf.c
    # (HKDF). All four translation units sit under the 900-line limit.
    # src/security/ed25519.c completed its multi-step split in PR A.1,
    # A.2 and A.3 of the dedicated monolith residual plan
    # (docs/plans/active/monolith-residual-dedicated-plan.md §2):
    #   - PR A.1 (done): group constants + Edwards point arithmetic +
    #     scalar mult moved to src/security/ed25519_group.c (306 LOC),
    #     with the shared surface (typedef ge_p3, ED_D / ED_SQRTM1,
    #     ge_wipe, ge_dbl, ge_neg_p, ge_scalarmult_base,
    #     ge_double_scalarmult) exposed via the new
    #     src/security/internal/ed25519_internal.h.
    #   - PR A.2 (done): compressed point codec (ge_encode / ge_decode)
    #     moved to src/security/ed25519_encode.c (208 LOC).
    #   - PR A.3 (done): scalar arithmetic mod L (load_3/load_4,
    #     sc_reduce64, sc_muladd, sc_is_canonical, ED_L_BYTES) moved
    #     to src/security/ed25519_scalar.c (836 LOC). ed25519.c is now
    #     286 LOC (public APIs + SHA-512 helpers + local wipe_bytes)
    #     and DROPS OUT of this baseline list — it is no longer a
    #     monolith.
    # The internal header sits at 139 LOC. All five translation units
    # now sit under the 900-line limit.
    # src/auth/user.c was split in the 2026-05-15 refactor into
    # user.c (record lifecycle), user_helpers.c, userdb_io.c and
    # userdb_auth.c. All four translation units sit under the
    # 900-line limit on their own.
    # src/auth/login_runtime.c was split in the 2026-05-15 refactor
    # across PRs C.0-C.65 of the Estagio C dedicated plan into 65
    # focused translation units plus an internal helper header:
    #   - Phase 1 (PR C.0): src/auth/internal/login_runtime_internal.h
    #     (74 LOC) carrying 5 shared static-inline helpers.
    #   - Phase 2 (PR C.1-C.6, 6 files): contract_policy.c,
    #     credential_buffer.c, credential_input.c,
    #     credential_interaction.c, credential_view_model.c,
    #     session_pipeline.c.
    #   - Phase 3 (PR C.7-C.15, 9 files): render_action_ui_event.c,
    #     route_controller.c, presenter_binding.c, mount_commit.c,
    #     handoff_dispatch.c, queue_activation.c, frame_surface.c,
    #     compositor_damage.c, present_plan.c.
    #   - Phase 4 (PR C.16-C.41, 26 files): one per pipeline stage
    #     (schedule_plan.c through expiry_plan.c).
    #   - Phase 5 (PR C.42-C.46, 5 files): purge_plan.c through
    #     release_plan.c.
    #   - Phase 6 (PR C.47-C.63, 17 files): gui_plan.c, window_plan.c
    #     and 15 window_*_plan.c files.
    #   - Phase 7 (PR C.64-C.65, 2 files): pipeline_safety.c and
    #     view_model.c.  src/auth/login_runtime.c is now 282 LOC
    #     (facade with `login_runtime_run` + 5 file-local helpers)
    #     and DROPS OUT of this baseline list — it is no longer a
    #     monolith.  All 65 new translation units sit under the
    #     900-line limit on their own (total 27616 LOC).
    # src/services/update_agent.c was split in the 2026-05-15 refactor
    # into update_agent.c (globals + helpers + IO + fetchers + init),
    # update_agent_parse.c (manifest parsing + validators + branch URL
    # builders), update_agent_apply.c (poll + import_manifest gate) and
    # update_agent_prepare.c (fetch_remote_manifest + download_payload +
    # prepare_* + stage_latest + clear_stage + set_pending_activation).
    # All four translation units sit under the 900-line limit and
    # continue sharing globals/views through
    # src/services/internal/update_agent_internal.h together with the
    # pre-existing update_agent_transact.c (boot slot integration).
    # src/arch/x86_64/kernel_main.c was split in the 2026-05-29 refactor:
    # the late boot-stage bodies (Linux-ABI shim registration, Stage 4
    # keyboard/setup, Stage 7 input probe, Stage 8 network/boot-policy, and
    # the login_runtime_ops builder) moved into the new sibling TU
    # src/arch/x86_64/kernel_boot_stages.c (declared in
    # include/arch/x86_64/kernel_main_internal.h, wired into CAPYOS64_OBJS
    # next to kernel_main.o). The fragile pre-COM1 / early-post-EBS path
    # (handoff + framebuffer validation, Stage 1-3, and the
    # CAPYOS_BOOT_RUN_HELLO / CAPYOS_PREEMPTIVE_* #ifdef blocks) stays
    # inline. kernel_main.c is now 655 LOC and DROPS OUT of this baseline
    # list -- it is no longer a monolith.
    # src/gui/desktop/taskbar.c was split in the 2026-05-15 refactor
    # into taskbar.c (main bar + clock + tray + click router),
    # taskbar_menu.c (menu data model + popup rendering) and
    # taskbar_menu_input.c (menu event handlers: toggle, click,
    # hover, scroll, keyboard). All three translation units sit
    # under the 900-line limit.
    # include/auth/login_runtime.h started a multi-PR split in PR 1 of
    # Estagio B+C+D of the dedicated monolith residual plan
    # (docs/plans/active/monolith-residual-dedicated-plan.md §B.3):
    #   - PR 1 (done): top-of-pipeline structs
    #     (login_window_credential_screen_pipeline_safety_report,
    #     login_window_contract, login_window_view_model,
    #     login_runtime_ops) moved to
    #     include/auth/login_runtime/pipeline_contract.h (152 LOC).
    #     login_runtime.h reduced from 10881 to 10784 LOC.
    #   - PR 2 (done): auth-core structs
    #     (login_recovery_resume_policy, login_window_credential_policy,
    #     _buffer, _submit_gate, _submit_attempt, _auth_submit +
    #     login_window_credential_authenticate_fn typedef) moved to
    #     include/auth/login_runtime/auth_core.h (148 LOC) and
    #     pipeline_contract.h now includes it directly to become
    #     standalone. login_runtime.h reduced from 10784 to 10692 LOC.
    #   - PR 3 (done): input-layer + audit/view-model partials
    #     (input_layer.h 157 LOC; audit_view.h 157 LOC). 9 structs
    #     moved. login_runtime.h: 10692 -> 10458.
    #   - PR 4 (done): recovery_screen.h (179 LOC) with 4 structs
    #     (recovery_view_model, screen_view_model, screen_session,
    #     screen_render_plan). login_runtime.h: 10458 -> 10318.
    #   - PR 5 (done): action_route.h + controller_handoff.h +
    #     presenter_mount.h with 9 structs (3+3+3).
    #     login_runtime.h: 10318 -> 9945.
    #   - PR 6 (done): commit_dispatch.h + frame_compositor.h with
    #     9 structs (5+4). login_runtime.h: 9945 -> 9419.
    #   - PR 7 (done, split as 7a/7b/7c): present_scanout.h (403 LOC)
    #     with 4 structs (present_plan, schedule_plan, vsync_plan,
    #     scanout_plan); display_blit.h (589 LOC) with 4 structs
    #     (display_plan, output_plan, blit_plan, framebuffer_plan);
    #     flush_sync.h (679 LOC) with 5 structs (flush_plan,
    #     barrier_plan, fence_plan, timeline_plan, sync_plan).
    #     login_runtime.h: 9419 -> 9043 (7a) -> 8473 (7b) -> 7814 (7c).
    #   - PR 8 (done 2026-05-15): deadline_cleanup.h (807 LOC) with
    #     5 structs (deadline_plan, completion_plan, ack_plan,
    #     retire_plan, cleanup_plan) and seal_ledger.h (1050 LOC) with
    #     5 structs (seal_plan, audit_plan, record_plan, receipt_plan,
    #     ledger_plan). Both partial headers are wired into the facade
    #     and the 10 inline duplicate definitions have been physically
    #     deleted (no `#if 0` wrapper remaining). login_runtime.h:
    #     7814 -> 6009 LOC (-1805 LOC).
    #   - PR 11 add-on (done 2026-05-15, outside original 10 PR plan):
    #     function_declarations.h (358 LOC) carrying the 60+ public
    #     function prototypes of the login runtime pipeline build
    #     chain. The facade now ends with a single `#include` for this
    #     partial right before its `#endif`. login_runtime.h:
    #     6009 -> 5699 LOC (-310 LOC).
    #   - PR 12 add-on (done 2026-05-15, outside original 10 PR plan):
    #     version_constants.h (130 LOC) carrying the 90+ public
    #     `LOGIN_*_VERSION` ABI stamps, password buffer limits, and
    #     input/screen action codes. The facade includes it first
    #     (right after the standard headers) so every downstream
    #     struct + function prototype sees these compile-time
    #     constants. login_runtime.h: 5699 -> 5609 LOC (-90 LOC).
    #   - PR 9a (done 2026-05-15): journal_retention.h (1135 LOC)
    #     with 4 structs (journal_plan, archive_plan, retention_plan,
    #     expiry_plan). Partial header wired into facade and the 4
    #     inline duplicates physically deleted. login_runtime.h:
    #     5609 -> 4474 LOC (-1135 LOC).
    #   - PR 9b (done 2026-05-15): purge_reclaim.h (948 LOC) with
    #     5 structs (purge_plan, tombstone_plan, compaction_plan,
    #     reclaim_plan, release_plan). Partial header wired into
    #     facade and the 5 inline duplicates physically deleted.
    #     login_runtime.h: 4474 -> 3553 LOC (-921 LOC).
    #   - PR 10 (done 2026-05-15, split as 10a/10b/10c/10d):
    #     gui_window.h (485 LOC) with 4 structs (gui/window/
    #     window_surface/window_compositor); window_display.h
    #     (1010 LOC) with 5 structs (window_damage/present/schedule/
    #     vsync/scanout); window_output.h (1264 LOC) with 5 structs
    #     (window_display/output/blit/commit/flip); window_input.h
    #     (770 LOC) with 3 structs (window_vblank/event/input). All
    #     four partial headers are wired into the facade and the 17
    #     inline duplicate definitions have been physically deleted
    #     (no `#if 0` wrapper remaining). login_runtime.h:
    #     3553 -> 3096 (10a) -> 2115 (10b) -> 882 (10c) -> 142 LOC
    #     (10d), i.e. -3411 LOC across PR 10.
    #     The facade now carries ONLY the public guard, 4 standard
    #     includes (stddef + 3 module sibling headers) and 19
    #     `#include` directives for the partial headers — it DROPS
    #     OUT of this baseline list (142 LOC well under the 900-line
    #     limit).
    #   - PR 11 (done 2026-05-15, split as 11a/11b/11c/11d/11e):
    #     per-struct refactor of the 5 partial headers that still
    #     exceeded the 900-line limit after PR 10 (purge_reclaim.h
    #     948, window_display.h 1010, seal_ledger.h 1050,
    #     journal_retention.h 1161, window_output.h 1264). Each
    #     became a thin aggregator (~28 LOC) that `#include`s 4-5
    #     one-struct-per-file partials (87 to 372 LOC each). Total
    #     across PR 11: 5 aggregators + 24 per-struct partials, all
    #     under the 900-line limit. Every header under
    #     `include/auth/login_runtime/` is now audit-compliant
    #     without baseline exception.
    # tests/test_capylibc_net.c was split in the 2026-05-15 refactor
    # into test_capylibc_net.c (sockets/DNS + fake stubs + entry +
    # last_error reset), test_capylibc_net_url.c (URL parser + HTTP
    # request builder + HTTP status line + HTTP header parser) and
    # test_capylibc_net_http.c (capy_http_get end-to-end). All three
    # translation units sit under the 900-line limit and share the
    # recording fake state + TEST/PASS/FAIL macros through
    # tests/userland/test_capylibc_net_internal.h.
    # src/gui/desktop/desktop_icons.c was preventively split in the
    # 2026-05-16 refactor. The file sat at 823/900 LOC, 77 lines
    # from the audit ceiling. The context-menu block (~140 LOC of
    # action dispatch + inline-prompt callbacks: `di_ctx_pick`,
    # `di_rename_submit`, `di_create_submit`, and the public entry
    # `desktop_icons_handle_context`) was extracted into a new
    # sibling TU `src/gui/desktop/desktop_icons_context.c` (170 LOC).
    # To support cross-TU sharing of `g_di` (the global icon state)
    # and the helpers consumed by the context menu code, a new
    # internal header
    # `src/gui/desktop/internal/desktop_icons_internal.h` (71 LOC)
    # was introduced. It declares the named `struct di_state`
    # (replacing the previous anonymous struct), the
    # `struct di_entry` element type, the six `DI_CTX_*` action
    # constants, the `extern struct di_state g_di;` linkage,
    # and the five helpers that the sibling consumes:
    # `di_strcpy`, `di_join`, `di_icon_position`, `di_is_text`,
    # `di_request_delete_selected`. The parent file
    # `desktop_icons.c` promoted those five helpers from `static`
    # to extern linkage and now includes the internal header. The
    # public ABI in `include/gui/desktop_icons.h` is unchanged.
    # Byte-for-byte parity of the four extracted functions verified
    # via inspection. The parent is now 659 LOC (241 headroom).
    # include/auth/login_runtime/deadline_cleanup.h was
    # preventively split in the 2026-05-16 refactor. The file sat at
    # 807/900 LOC, 93 lines from the audit ceiling. The five inline
    # struct definitions (deadline_plan, completion_plan, ack_plan,
    # retire_plan, cleanup_plan) were extracted byte-for-byte into
    # five new per-struct partial headers, mirroring the existing
    # pattern of peer plan headers (e.g. archive_plan.h,
    # expiry_plan.h, purge_plan.h):
    #   - include/auth/login_runtime/deadline_plan.h (160 LOC)
    #   - include/auth/login_runtime/completion_plan.h (165 LOC)
    #   - include/auth/login_runtime/ack_plan.h (172 LOC)
    #   - include/auth/login_runtime/retire_plan.h (181 LOC)
    #   - include/auth/login_runtime/cleanup_plan.h (190 LOC)
    # The original `deadline_cleanup.h` is now a thin 40-LOC
    # aggregator that just includes the five new partials. The only
    # external consumer (`include/auth/login_runtime.h`, the master
    # aggregator) still includes `deadline_cleanup.h` and gets all
    # five struct definitions transitively. Forward declarations in
    # `function_declarations.h` continue to work because the
    # function prototypes use pointer types (no full definition
    # required at decl site). Byte-for-byte parity of each of the
    # five struct bodies verified via diff against the original.
    # All six files (1 aggregator + 5 partials) sit comfortably
    # under the 900-line ceiling.
    # tests/auth/test_login_runtime_credential_retention_expiry.c
    # was preventively split in the 2026-05-16 refactor. The file
    # sat at 832/900 LOC, 68 lines from the audit ceiling. This is a
    # second-generation companion split: the parent companion was
    # itself carved out of `tests/auth/test_login_runtime.c` at the
    # 2026-05-16 monolith refactor (PR D.28 of the Estagio D
    # dedicated plan), and now the 4 expiry plan tests + the
    # `build_loginwindow_credential_screen_expiry_plan_for_action`
    # helper were extracted into a new sibling companion
    # `tests/auth/test_login_runtime_credential_expiry_plan.c`
    # (457 LOC). The new companion registers its own `_cases()`
    # entry (`test_login_runtime_credential_expiry_plan_cases`) in
    # `tests/auth/test_login_runtime_internal.h` and is invoked by
    # `run_login_runtime_tests` in
    # `tests/auth/test_login_runtime.c` directly after the existing
    # `_retention_expiry_cases()` call. The expiry helper
    # `build_loginwindow_credential_screen_expiry_plan_for_action`
    # moved with the tests since its only consumers were the 4
    # expiry tests + the external test_login_runtime_credential_purge.c
    # file (which links against the same test binary, so the
    # external linkage is preserved transparently). The retention
    # helper `build_loginwindow_credential_screen_retention_plan_for_action`
    # stays in the parent and is consumed via internal header by
    # the new sibling. Byte-for-byte parity of the 4 test bodies +
    # expiry helper verified via diff (only difference: 1 trailing
    # blank line, cosmetic). The parent companion is now 418 LOC
    # (482 headroom); the new sibling has 457 LOC (443 headroom).
    # tests/kernel/linux_compat/test_linux_vfs_router.c was
    # preventively split in the 2026-05-16 refactor. The file sat at
    # 841/900 LOC, 59 lines from the audit ceiling. The 34 router
    # tests for the "special fd" subsystem families (eventfd,
    # signalfd, timerfd, memfd_secret + memfd family, pidfd, inotify,
    # epoll, fanotify, userfaultfd, landlock ruleset) were extracted
    # into a new sibling TU
    # `tests/kernel/linux_compat/test_linux_vfs_router_specialfd.c`
    # (520 LOC). Each TU carries its own private copy of the
    # in-process fixture helpers (`install_router`, `fake_csprng`,
    # `router_meminfo`, `router_cpuinfo`, `router_pid_exists`,
    # `tests_run`/`tests_passed` counters, `TEST`/`PASS`/`FAIL`
    # macros) plus the sibling-specific helpers (`router_now_ns`,
    # `g_router_now_ns`, `create_landlock_ruleset_fd`) — all
    # `static` so there is no link-time collision. The parent keeps
    # the /dev, /proc, /tmp and prefix priority test groups
    # (35 tests). The runner
    # `test_linux_vfs_router_specialfd_run` is declared and invoked
    # from `tests/test_runner.c` directly after
    # `test_linux_vfs_router_run`. Byte-for-byte parity of the 34
    # test bodies (lines 394-749 of the original, 122-477 of the
    # new file) verified via diff. The parent is now 441 LOC
    # (459 headroom); the new sibling has 520 LOC (380 headroom).
    # tests/auth/test_login_runtime_credential_input_view.c was
    # preventively split in the 2026-05-16 refactor. The file sat at
    # 856/900 LOC, 44 lines from the audit ceiling. This is a
    # second-generation companion split: the parent companion was
    # itself carved out of `tests/auth/test_login_runtime.c` at the
    # 2026-05-15 monolith refactor (PR D.3 of the Estagio D dedicated
    # plan), and now the 8 panel+interaction tests
    # (`login_window_credential_panel_build`: 4 + 4 covering the
    # ready masked panel, the panel reflecting append input, the
    # panel reflecting submit/cancel, the panel failing closed on
    # unsafe policy or blocked input, plus
    # `login_window_credential_interaction_step`: the append/rebuild
    # pipeline, the submit wipe gate, the cancel wipe, the
    # missing-policy/unknown-action fail-closed default) were
    # extracted into a new sibling companion
    # `tests/auth/test_login_runtime_credential_input_view_panel.c`
    # (473 LOC). The new companion registers its own `_cases()`
    # entry (`test_login_runtime_credential_input_view_panel_cases`)
    # in `tests/auth/test_login_runtime_internal.h` and is invoked
    # by `run_login_runtime_tests` in `tests/auth/test_login_runtime.c`
    # directly after the existing `_input_view_cases()` call. Both
    # companions share the same fixture and helpers
    # (`reset_test_state`, `build_ops`, `expect_true`,
    # `strings_equal`) declared in the internal header.
    # Byte-for-byte parity of the 8 test bodies (lines 405-835 of
    # the original, 30-460 of the new file) verified via diff. The
    # parent companion is now 416 LOC (484 headroom); the new
    # sibling has 473 LOC (427 headroom).
    # tests/auth/test_login_runtime_credential_screen.c was
    # preventively split in the 2026-05-16 refactor. The file sat at
    # 857/900 LOC, 43 lines from the audit ceiling. This is a
    # second-generation companion split: the parent companion was
    # itself carved out of `tests/auth/test_login_runtime.c` at the
    # 2026-05-15 monolith refactor (PR D.6 of the Estagio D dedicated
    # plan), and now the 6 view_model tests
    # (`login_window_credential_screen_view_model_build`: ready safe
    # login screen + text recovery + resume ready + unsafe session
    # block + unsafe recovery block + enabled GUI submit block) were
    # extracted into a new sibling companion
    # `tests/auth/test_login_runtime_credential_screen_view_model.c`
    # (380 LOC). The new companion registers its own `_cases()`
    # entry (`test_login_runtime_credential_screen_view_model_cases`)
    # in `tests/auth/test_login_runtime_internal.h` and is invoked
    # by `run_login_runtime_tests` in `tests/auth/test_login_runtime.c`
    # directly after the existing `_screen_cases()` call. Both
    # companions share the same fixture and helpers
    # (`reset_test_state`, `build_ops`, `expect_true`,
    # `strings_equal`, `g_runtime_maintenance_active`) declared in
    # the internal header. Byte-for-byte parity of the 6 test bodies
    # (lines 33-374 of the original, 28-369 of the new file)
    # verified via diff. The parent companion is now 509 LOC
    # (391 headroom); the new sibling has 380 LOC (520 headroom).
    # src/gui/desktop/desktop.c was preventively split in the
    # 2026-05-16 refactor. The file sat at 859/900 LOC, 41 lines
    # from the audit ceiling. The single biggest function
    # `desktop_handle_mouse` (~253 LOC of mouse event loop dispatch
    # across compositor, taskbar, inline_prompt, context_menu and
    # desktop_icons) was extracted into the new sibling TU
    # `src/gui/desktop/desktop_mouse.c` (292 LOC). The single static
    # helper used by the mouse handler (`desktop_overlay_active`,
    # 4 lines) is duplicated as `static` in the new TU to preserve
    # the per-TU "no link-time coupling" pattern. The public
    # declaration of `desktop_handle_mouse` in
    # `include/gui/desktop.h` is unchanged, so the call site in
    # `desktop_run_frame` (still in `desktop.c`) resolves via the
    # normal extern linkage. Byte-for-byte parity of the function
    # body (lines 527-779 of the original, 40-292 of the new file)
    # verified via diff. The parent TU is now 605 LOC (295 headroom);
    # the new sibling has 292 LOC (608 headroom).
    # tests/security/test_volume_provider.c was preventively split
    # in the 2026-05-16 refactor. The file sat at 867/900 LOC. The
    # eight rekey orchestration tests (4 preflight + 4 plan,
    # covering header-managed and legacy paths) were extracted into
    # the new sibling test TU
    # `tests/security/test_volume_provider_rekey.c` (488 LOC). Each
    # test TU keeps its own private copy of the in-RAM block-device
    # fixture (`struct ram_dev`, `ram_alloc`/`ram_free`,
    # `ram_read_block`/`ram_write_block`, `g_ram_ops`) and
    # assertion helpers (`expect_int`, `expect_true`,
    # `test_put_u32_le`, `write_legacy_capyfs_super`) — all are
    # `static` so there is no link-time collision between the two
    # TUs. The new runner `run_volume_provider_rekey_tests` is
    # declared and invoked from `tests/test_runner.c` directly
    # after `run_volume_provider_tests`. Byte-for-byte parity of
    # the 8 test bodies (lines 540-840 of the original, 170-470 of
    # the new file) verified via diff. The parent TU is now
    # 555 LOC (345 headroom); the new sibling has 488 LOC (412
    # headroom). The split mirrors the source-side split between
    # `volume_provider.c` (install/open) and
    # `volume_provider_rekey.c` (preflight/plan/dry-run-execute/
    # checkpoint).
    # src/shell/commands/system_control/jobs_updates.c was
    # preventively split in the 2026-05-16 refactor. The file sat
    # at 892/900 LOC, eight lines from the audit ceiling. Four
    # commands that only consume the persistent update_agent surface
    # (`cmd_update_arm`, `cmd_update_clear`,
    # `cmd_update_import_manifest`, `cmd_update_channel`) were
    # extracted into the new sibling TU
    # `src/shell/commands/system_control/updates_arm.c` (270 LOC).
    # Two helpers shared between the parent and the new sibling
    # (`update_runtime_writer`, `refresh_update_agent_service_state`)
    # were promoted from `static` to extern linkage and declared in
    # `src/shell/commands/system_control/internal/system_control_internal.h`
    # under a new "jobs_updates shared helpers" section. The
    # remaining two static helpers (`update_runtime_bytes_writer`,
    # `update_runtime_remover`) only serve callers that stayed in
    # the parent and kept their static scope. Byte-for-byte parity
    # of every extracted command body was verified via diff. The
    # parent TU is now 642 LOC (258 LOC headroom); the new sibling
    # has 270 LOC (630 LOC headroom). Public command table wiring
    # in `power_runtime_registry.c` is unchanged because the
    # declarations stay in the internal header.
    # src/security/volume_provider_rekey_execute.c and its mirror
    # test were preventively split in the 2026-05-16 refactor. The
    # source file sat at 893/900 LOC and the test at 896/900 LOC,
    # both within 7 LOC of the audit ceiling. The largest function
    # `volume_provider_rekey_execute_copy_step` (307 LOC, the reverse
    # block copy + re-encrypt + checkpoint update step) was extracted
    # into the new sibling TU
    # `src/security/volume_provider_rekey_copy.c` (424 LOC) following
    # the per-TU "no link-time coupling" pattern: own static copies
    # of `vp_rekey_exec_wipe`, `vp_rekey_block_same`,
    # `vp_rekey_checkpoint_same`, `vp_rekey_offset_view` and
    # `vp_rekey_offset_ops`. The 5 mirror tests `test_copy_step_*`
    # were extracted into
    # `tests/security/test_volume_provider_rekey_copy.c` (442 LOC)
    # with its own fixture infrastructure (`rekey_copy_ram_dev` etc.)
    # and own `run_volume_provider_rekey_copy_tests` entry registered
    # in `tests/test_runner.c` next to the existing
    # `_rekey_execute_tests` entry. Byte-for-byte parity of the copy
    # step function body was verified via diff before deletion. The
    # source file is now 585 LOC (315 headroom); the test file is
    # 723 LOC (177 headroom). Public API in
    # include/security/volume_provider.h is unchanged.
    # src/security/volume_provider.c was preventively split in the
    # 2026-05-16 refactor. The file was not in this baseline list
    # (it sat at 899/900 LOC, one line under the audit ceiling), but
    # any future security fix or feature addition would have tripped
    # the audit. Rekey orchestration (preflight + plan + dry-run
    # execute + checkpoint init/serialize/parse + the three static
    # validators + vp_capyfs_plain_super_valid) moved into the new
    # sibling translation unit src/security/volume_provider_rekey.c
    # (679 LOC). volume_provider.c is now 296 LOC (install + open +
    # vp_wipe + constants, ~604 LOC of headroom). Each TU keeps its
    # own static copies of vp_wipe / vp_get_u32_le / vp_put_u32_le /
    # vp_crc32 to preserve the established per-TU "no link-time
    # coupling" pattern already used by volume_header.c and the
    # write-enabled rekey siblings. Public API in
    # include/security/volume_provider.h is unchanged. Byte-for-byte
    # parity of every extracted function body was verified via diff
    # before the originals were deleted. The TEST_SRCS line in the
    # Makefile links both TUs into every test_volume_provider*
    # binary so the existing tests exercise the split surface.
    # tests/auth/test_login_runtime.c was split in the 2026-05-16
    # refactor across PRs D.0-D.47 of the Estagio D dedicated plan
    # (docs/plans/active/monolith-residual-dedicated-plan.md §D)
    # into 46 focused companion translation units plus an internal
    # helper header. Each companion file mirrors 1:1 a stage of the
    # 58-stage credential screen pipeline (controller -> presenter
    # -> ... -> pipeline_safety_report) and exposes its `build_*`
    # helper through tests/auth/test_login_runtime_internal.h so
    # downstream stages can chain on the previous fixture without
    # duplicating setup. tests/auth/test_login_runtime.c is now
    # 529 LOC (5 legacy maintenance/recovery tests + common test
    # helpers + `run_login_runtime_tests` orchestrator calling 47
    # `_cases()` entry points) and DROPS OUT of this baseline list
    # -- it is no longer a monolith. All 46 companion translation
    # units sit under the 900-line limit on their own
    # (total 27 750 LOC). Every test preserves byte-for-byte parity
    # with the pre-refactor monolith (verified via `diff` for each
    # of the 41 carve PRs before deletion).
    # tests/test_crypt_vectors.c was split in the 2026-05-15 refactor
    # into test_crypt_vectors.c (helpers + entry + SHA-256, PBKDF2,
    # AES-XTS, block0 wrapper chain, constant-time, SHA-256 clear),
    # test_crypt_vectors_aead.c (ed25519, HKDF, ChaCha20 block, ChaCha20
    # encrypt, Poly1305, ChaCha20-Poly1305 AEAD) and
    # test_crypt_vectors_kdf.c (X25519 RFC 7748, BLAKE2b RFC 7693,
    # Argon2id, crypt_derive_xts_keys_argon2id). All three translation
    # units sit under the 900-line limit and share hex helpers through
    # tests/test_crypt_vectors_internal.h.
    # tests/test_gui_event.c was split in the 2026-05-15 refactor into
    # test_gui_event.c (entry + queue/FIFO/poll/peek/dispatch/ready/
    # backpressure coverage) and test_gui_event_helpers.c (push
    # helpers, coalescing, discard, snapshot, reset, overflow). Both
    # translation units sit under the 900-line limit and share the
    # TEST/PASS/FAIL macros and `make_event` helper through
    # tests/test_gui_event_internal.h.
    # tests/test_capylibc_tls.c was split in the 2026-05-15 refactor
    # into test_capylibc_tls.c (lifecycle: init/config_resolve/context
    # prepare/reset/clear + managed slot acquire/release/free +
    # connect/IO/security_info/names + entry), test_capylibc_tls_trust.c
    # (default trust anchor catalog, slot table, descriptors, bundle,
    # material summary, store manifest) and test_capylibc_tls_backend.c
    # (default backend plan, BearSSL reserved state, BearSSL adapter
    # contract, capy_tls_backend_connect state machine). All three
    # translation units sit under the 900-line limit and share the
    # TEST/PASS/FAIL macros + counter externs + fake_ctx helper through
    # tests/userland/test_capylibc_tls_internal.h.
    # tests/test_gui_window_dispatcher.c was split in the 2026-05-15
    # refactor into test_gui_window_dispatcher.c (fixture + run-counter
    # globals + entry + tests: noop+key, key-up, scroll+paint, mouse,
    # mouse capture, mouse capture reset) and
    # test_gui_window_dispatcher_lifecycle.c (snapshot/health,
    # focus+blur lifecycle, compositor-owned lifecycle, context menu,
    # timer, miss/ignore). Both translation units sit under the
    # 900-line limit and share macros, counter externs, callback
    # declarations and fixture helpers through
    # tests/test_gui_window_dispatcher_internal.h.
}

EXT_LANGUAGE = {
    ".c": "c",
    ".h": "c-header",
    ".inc": "c-fragment",
    ".S": "asm",
    ".s": "asm",
    ".py": "python",
    ".sh": "shell",
    ".ld": "linker-script",
    ".md": "markdown",
    ".yaml": "yaml",
    ".yml": "yaml",
    ".json": "json",
    ".txt": "text",
}


@dataclass
class FileInfo:
    path: Path
    language: str
    lines: int


def is_ignored(path: Path) -> bool:
    text = path.as_posix()
    return any(text.startswith(prefix) or f"/{prefix}" in text for prefix in GENERATED_OR_VENDOR)


def iter_files(repo: Path, roots: Iterable[str]) -> Iterable[Path]:
    for root_name in roots:
        root = repo / root_name
        if not root.exists():
            continue
        iterator = root.iterdir() if root_name == "." else root.rglob("*")
        for path in iterator:
            if path.is_file() and not is_ignored(path.relative_to(repo)):
                if root_name == "." and path.suffix not in {".sh", ".py"}:
                    continue
                yield path


def count_lines(path: Path) -> int:
    try:
        with path.open("r", encoding="utf-8", errors="ignore") as handle:
            return sum(1 for _ in handle)
    except OSError:
        return 0


def collect(repo: Path, roots: Iterable[str]) -> list[FileInfo]:
    infos: list[FileInfo] = []
    for path in iter_files(repo, roots):
        language = EXT_LANGUAGE.get(path.suffix, "other")
        infos.append(FileInfo(path.relative_to(repo), language, count_lines(path)))
    return infos


def is_static_data_exception(path: Path) -> bool:
    return path in STATIC_DATA_EXCEPTIONS


def is_monolith_baseline_exception(path: Path) -> bool:
    return path in MONOLITH_BASELINE_EXCEPTIONS


def module_name(path: Path) -> str:
    parts = path.parts
    if not parts:
        return "."
    if parts[0] == "src" and len(parts) >= 2:
        if parts[1] == "arch" and len(parts) >= 3:
            return "/".join(parts[:3])
        return "/".join(parts[:2])
    if parts[0] == "include" and len(parts) >= 2:
        return "/".join(parts[:2])
    if parts[0] == "tools" and len(parts) >= 2:
        return "/".join(parts[:2])
    return parts[0]


def owning_module_of_internal_include(include_path: str) -> str | None:
    """Return the src/ module that owns an internal header, or None if not internal."""
    if "/internal/" not in include_path:
        return None
    parts = include_path.split("/")
    try:
        idx = parts.index("internal")
    except ValueError:
        return None
    prefix = parts[:idx]
    if not prefix:
        return None
    if prefix[0] == "arch" and len(prefix) >= 2:
        return "src/" + "/".join(prefix[:3])
    return "src/" + prefix[0]


def check_internal_boundary(repo: Path, infos: list[FileInfo]) -> list[str]:
    """Detect files that include another module's internal/ headers."""
    violations: list[str] = []
    for info in infos:
        if info.language not in {"c", "c-header", "c-fragment"}:
            continue
        if not info.path.parts or info.path.parts[0] != "src":
            continue
        file_mod = module_name(info.path)
        try:
            content = (repo / info.path).read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for m in INCLUDE_RE.finditer(content):
            inc = m.group(1)
            if inc.startswith("."):
                continue
            owner = owning_module_of_internal_include(inc)
            if owner is None or owner == file_mod:
                continue
            violations.append(
                f"cross-module internal include: {info.path} "
                f"includes \"{inc}\" (owned by {owner})"
            )
    return violations


def print_section(title: str) -> None:
    print(f"\n## {title}")


def language_family(language: str) -> str:
    if language in {"c", "c-header", "c-fragment"}:
        return "c-family"
    return language


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit source organization.")
    parser.add_argument("--repo", default=".", help="Repository root")
    parser.add_argument("--max-c-lines", type=int, default=900)
    parser.add_argument("--max-test-lines", type=int, default=900)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--strict", action="store_true", help="Exit non-zero when warnings exist")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    infos = collect(repo, DEFAULT_ROOTS)
    warnings: list[str] = []

    by_lang = Counter(info.language for info in infos)
    by_module_lines: dict[str, int] = defaultdict(int)
    by_module_files: dict[str, int] = defaultdict(int)
    by_module_langs: dict[str, Counter[str]] = defaultdict(Counter)

    for info in infos:
        mod = module_name(info.path)
        by_module_lines[mod] += info.lines
        by_module_files[mod] += 1
        by_module_langs[mod][info.language] += 1

    print("# CapyOS Source Layout Audit")
    print(f"Repo: {repo}")
    print(f"Files scanned: {len(infos)}")

    print_section("Languages")
    for lang, count in by_lang.most_common():
        print(f"- {lang}: {count}")

    print_section("Largest Modules")
    for mod, lines in sorted(by_module_lines.items(), key=lambda item: item[1], reverse=True)[: args.top]:
        langs = ", ".join(f"{lang}:{count}" for lang, count in by_module_langs[mod].most_common())
        print(f"- {mod}: {lines} lines, {by_module_files[mod]} files ({langs})")

    print_section("Largest Files")
    largest = sorted(infos, key=lambda info: info.lines, reverse=True)[: args.top]
    for info in largest:
        print(f"- {info.path}: {info.lines} lines [{info.language}]")

    print_section("Warnings")
    for info in infos:
        if (info.language in {"c", "c-header", "c-fragment"} and
                not is_static_data_exception(info.path) and
                not is_monolith_baseline_exception(info.path)):
            limit = args.max_test_lines if info.path.parts and info.path.parts[0] == "tests" else args.max_c_lines
            if info.lines > limit:
                warnings.append(f"monolith: {info.path} has {info.lines} lines (limit {limit})")
        if (info.path.parts and info.path.parts[0] == "src" and
                info.path.suffix == ".h" and "internal" not in info.path.parts):
            warnings.append(f"internal header in src: {info.path} (prefer include/<module>/ or src/<module>/internal/)")

    for mod, langs in sorted(by_module_langs.items()):
        code_langs = {
            language_family(lang)
            for lang in langs
            if lang in {"c", "c-header", "c-fragment", "asm", "python", "shell"}
        }
        if len(code_langs) > 2 and not mod.startswith("tools"):
            warnings.append(f"mixed implementation languages in {mod}: {', '.join(sorted(code_langs))}")

    warnings.extend(check_internal_boundary(repo, infos))

    if warnings:
        for warning in warnings:
            print(f"- {warning}")
    else:
        print("- none")

    print_section("Recommended Next Splits")
    for info in largest:
        if (info.language in {"c", "c-header", "c-fragment"} and
                info.lines > args.max_c_lines and
                not is_static_data_exception(info.path)):
            print(f"- split {info.path} by responsibility before adding new features")

    return 1 if args.strict and warnings else 0


if __name__ == "__main__":
    raise SystemExit(main())
