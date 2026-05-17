# Monolith refactor roadmap

**Status:** ativo. Refactor em curso desde 2026-05-15 para retirar arquivos
C de runtime e testes da lista de exceções de monolito definida em
`tools/scripts/audit_source_layout.py`. **Onda 1 (2026-05-15) entregou
5 monolitos divididos em 17 arquivos novos + 5 internal headers. Onda 2
(2026-05-15) entregou +1 monolito (update_agent.c) dividido em 3 novos
arquivos, reusando o internal header pré-existente. Onda 3 (2026-05-15)
entregou +3 monolitos de teste (test_gui_event.c, test_gui_window_
dispatcher.c, test_crypt_vectors.c) divididos em 6 novos arquivos +
3 internal headers. Onda 4 (2026-05-15) entregou +1 monolito
(test_capylibc_net.c) dividido em 2 novos arquivos + 1 internal
header, e **reorganizou a árvore inteira de `tests/`** em subdiretórios
por domínio (189 arquivos movidos). Onda 5 (2026-05-15) entregou +1
monolito (test_capylibc_tls.c) dividido em 2 novos arquivos + 1
internal header, e corrigiu 2 includes relativos quebrados pela
reorganização (test_capylibc_tls.c, test_hello_program.c). Onda 7
(2026-05-15, Estágio C dedicado) entregou o maior monolito do
projeto — `src/auth/login_runtime.c` — dividido em 65 arquivos novos
+ 1 internal header (66 PRs C.0-C.65), reduzindo o original de
22776 LOC para um facade fino de 282 LOC.**

**Objetivo:** todos os arquivos C de runtime e de teste-host abaixo de
**900 linhas**, segregados por responsabilidade conforme
`docs/architecture/source-layout.md`.

## 1. Estado inicial (2026-05-15)

Auditoria estática contra os arquivos de exceção atuais e contra todo o
diretório `src/` + `tests/`. Linha-base de exceção do audit antes do refactor:

| Arquivo | LOC | Tipo | Status |
|---|---:|---|---|
| `src/auth/user.c` | 974 | runtime | **Refatorado 2026-05-15** (onda 1) |
| `src/security/crypt.c` | 973 | runtime | **Refatorado 2026-05-15** (onda 1) |
| `src/apps/file_manager.c` | 1132 | runtime | **Refatorado 2026-05-15** (onda 1) |
| `src/apps/settings.c` | 1081 | runtime | **Refatorado 2026-05-15** (onda 1) |
| `src/gui/desktop/taskbar.c` | 1436 | runtime | **Refatorado 2026-05-15** (onda 1) |
| `src/arch/x86_64/kernel_main.c` | 989 | runtime | Deferred — já passou por múltiplos splits anteriores |
| `src/services/update_agent.c` | 1990 | runtime | **Refatorado 2026-05-15** (onda 2) |
| `src/security/ed25519.c` | 1465 → **286** | runtime | **Refatorado 2026-05-15** (estágio A do plano dedicado: PR A.1 + A.2 + A.3) — group/scalar mult em `ed25519_group.c` (306 LOC) + codec em `ed25519_encode.c` (208 LOC) + scalar arithmetic mod L em `ed25519_scalar.c` (836 LOC) + internal header (139 LOC). Saiu da baseline. |
| `src/auth/login_runtime.c` | 22776 → **282** | runtime | **Refatorado 2026-05-15** (estágio C do plano dedicado: PRs C.0-C.65) — facade fino + 65 TUs sob 900 LOC em `src/auth/login_runtime/` + 1 internal header em `src/auth/internal/`. Saiu da baseline. |
| `include/auth/login_runtime.h` | 10881 → **142** | header | **PRs 1-12 + 11a-e done 2026-05-15** (Estágio B+C+D completo + per-struct split) — 47 partial headers em `include/auth/login_runtime/` (23 originais + 24 sub-partials por struct após PR 11), todos sob 900 LOC. 5 aggregators finos (~28 LOC cada: purge_reclaim, window_display, seal_ledger, journal_retention, window_output) delegam para per-struct sub-partials. 90 structs + 1 typedef + 60+ function prototypes + 90+ `#define` constants movidos. Facade reduzido em **-98,7%** e **saiu da baseline** (`MONOLITH_BASELINE_EXCEPTIONS`). Maior partial restante: `deadline_cleanup.h` (807 LOC, dentro do limite). |
| `tests/userland/test_capylibc_net.c` | 2025 | test | **Refatorado 2026-05-15** (onda 4) |
| `tests/userland/test_capylibc_tls.c` | 1324 | test | **Refatorado 2026-05-15** (onda 5) |
| `tests/security/test_crypt_vectors.c` | 1910 | test | **Refatorado 2026-05-15** (onda 3) |
| `tests/gui/test_gui_event.c` | 1085 | test | **Refatorado 2026-05-15** (onda 3) |
| `tests/gui/test_gui_window_dispatcher.c` | 985 | test | **Refatorado 2026-05-15** (onda 3) |
| `tests/auth/test_login_runtime.c` | 25663 → **529** | test | **Refatorado 2026-05-16** (estágio D do plano dedicado: PRs D.7-D.47) — facade fino + 46 companion TUs sob 900 LOC em `tests/auth/test_login_runtime_credential_*.c` + 1 internal test header em `tests/auth/test_login_runtime_internal.h`. Saiu da baseline. |

Total **16 arquivos** em violação. Após onda 1 (2026-05-15): **11**
restantes. Após onda 2 (2026-05-15): **10**. Após onda 3 (2026-05-15):
**7**. Após onda 4 (2026-05-15): **6**. Após onda 5 (2026-05-15):
**5 arquivos** restantes em violação (3 runtime + 1 teste + 1 header).
Após onda 6 (Estágio B+C+D PRs 1-12, 2026-05-15): **4 arquivos**
restantes (3 runtime + 1 teste — `login_runtime.h` saiu da baseline
em 142 LOC). Após onda 7 (Estágio C dedicado PRs C.0-C.65,
2026-05-15): **3 arquivos** restantes (`kernel_main.c` deferred,
`test_login_runtime.c` casado e ainda pendente — `login_runtime.c`
saiu da baseline em 282 LOC). Após onda 8 (Estágio D dedicado PRs
D.7-D.47, 2026-05-16): **1 arquivo** restante (`kernel_main.c`
deferred — `test_login_runtime.c` saiu da baseline em 529 LOC,
dividido em 46 companion TUs).

## 2. Padrão de refactor estabelecido

Cada split segue cinco passos auditáveis:

1. **Mapear funções por família semântica.** `grep` por prefixos de
   nome (`auth_*`, `crypt_*`, etc.) e clusterizar.
2. **Definir o split por responsabilidade.** Cada arquivo novo deve ter
   um papel claro, descritível em uma linha.
3. **Criar header interno** em `src/<domain>/internal/<base>.h` apenas
   quando símbolos compartilhados entre as TUs derivadas precisarem
   atravessar a fronteira de tradução. Helpers `static` que
   permaneciam em escopo de arquivo viram `extern` com prefixo de
   domínio (ex.: `cstring_copy` → `auth_cstring_copy`).
4. **Atualizar Makefile.** Adicionar o `.o` correspondente na lista de
   objetos do kernel e os `.c` correspondentes no `TEST_SRCS` quando o
   código for exercitado por testes host-side.
5. **Atualizar `tools/scripts/audit_source_layout.py`.** Remover o
   arquivo da `MONOLITH_BASELINE_EXCEPTIONS` com um comentário
   apontando para os novos arquivos.

Refactor de **comportamento preservado**: nenhum split do roadmap pode
alterar ABI público, semântica observável de função ou ordem de wipe
de credenciais. Validação externa recomendada quando o módulo é
exercitado em testes:

- `make test` para módulos cobertos por unit tests host-side.
- `make all64` para módulos que tocam boot/runtime ou linker.
- `make layout-audit` para confirmar que o arquivo saiu da baseline.

## 3. Splits concluídos na onda 1 (2026-05-15)

### 3.0 `src/apps/file_manager.c` (1132 → 3 arquivos)

Quebra por responsabilidade UI.

| Arquivo | LOC | Responsabilidade |
|---|---:|---|
| `src/apps/file_manager.c` | 611 | lifecycle + ops (path/move/delete/unique) + window event dispatch + click router |
| `src/apps/file_manager_view.c` | 214 | drawing primitives + `file_manager_paint` |
| `src/apps/file_manager_dnd.c` | 375 | drag-and-drop handlers + right-click context menu |
| `src/apps/internal/file_manager_internal.h` | — | declarações compartilhadas (status helpers, drawing prim., ops, lifecycle) |

ABI público preservado (todos os símbolos em `include/apps/file_manager.h`).

### 3.0a `src/apps/settings.c` (1081 → 3 arquivos)

Quebra por camada de aplicação.

| Arquivo | LOC | Responsabilidade |
|---|---:|---|
| `src/apps/settings.c` | 290 | globals + tab labels + click row infrastructure + lifecycle + window events + click router |
| `src/apps/settings_view.c` | 549 | layout + drawing primitives + per-tab rendering + `settings_paint` |
| `src/apps/settings_actions.c` | 324 | inline-prompt callbacks (theme/keyboard/language apply, user creation, homepage edit) |
| `src/apps/internal/settings_internal.h` | — | click row defs, layout helpers, action gates |

ABI público preservado (todos os símbolos em `include/apps/settings.h`).

### 3.0b `src/gui/desktop/taskbar.c` (1436 → 3 arquivos)

Quebra por subsistema da taskbar.

| Arquivo | LOC | Responsabilidade |
|---|---:|---|
| `src/gui/desktop/taskbar.c` | 476 | `tb_*` utilities, main bar (window list + clock + tray + paint + click router) |
| `src/gui/desktop/taskbar_menu.c` | 774 | start menu data model + popup rendering + recent/session helpers + `menu_popup_paint` |
| `src/gui/desktop/taskbar_menu_input.c` | 266 | event handlers (toggle, click, hover, scroll, keyboard) |
| `src/gui/desktop/internal/taskbar_internal.h` | — | `tb_*` utilities + menu data-model helpers + popup primitives |

ABI público preservado (todos os símbolos em `include/gui/taskbar.h`).

### 3.1 `src/auth/user.c` (974 → 4 arquivos)

Quebra por camada do user database.

| Arquivo | LOC | Responsabilidade |
|---|---:|---|
| `src/auth/user.c` | 97 | `user_record_init`, `user_record_clear` |
| `src/auth/user_helpers.c` | 173 | helpers `auth_cstring_*`, `auth_memory_zero`, hex, parse, salt |
| `src/auth/userdb_io.c` | 437 | ensure/read/parse/iterate/serialize/write + `userdb_find/next_ids/has_any/add` |
| `src/auth/userdb_auth.c` | 350 | `userdb_authenticate*`, `userdb_set_password`, replace-hash + dummy salt |
| `src/auth/internal/user_helpers.h` | — | declarações dos helpers compartilhados |
| `src/auth/internal/userdb_io.h` | — | primitivas de armazenamento compartilhadas entre io e auth |

Símbolos públicos preservados: todos os declarados em `include/auth/user.h`.
Wipe seguro de credenciais preservado byte-a-byte em cada caminho.

### 3.2 `src/security/crypt.c` (973 → 4 arquivos)

Quebra por responsabilidade criptográfica.

| Arquivo | LOC | Responsabilidade |
|---|---:|---|
| `src/security/crypt.c` | 133 | `crypt_secure_clear`, `crypt_hmac_sha256_internal`, `crypt_constant_time_compare`, `crypt_hmac_sha256` (público) |
| `src/security/crypt_kdf.c` | 193 | PBKDF2 + `crypt_derive_xts_keys*` (PBKDF2 e Argon2id) |
| `src/security/crypt_aes_xts.c` | 534 | AES-256 cipher core (tables, key expand, encrypt/decrypt), XTS mode, adapter `block_device` (`crypt_init/free`, `crypt_read_block/write_block`) |
| `src/security/crypt_hkdf.c` | 233 | HKDF-SHA256 (extract, expand, oneshot) |
| `src/security/internal/crypt_internal.h` | — | declara `crypt_secure_clear` e `crypt_hmac_sha256_internal` |

ABI público inalterado (todos os símbolos em `include/security/crypt.h`).

## 3.5 Splits concluídos na onda 2 (2026-05-15)

### 3.5.1 `src/services/update_agent.c` (1990 → 4 arquivos)

Quebra por fase do state machine do agente de atualizações. O internal
header `src/services/internal/update_agent_internal.h` (pré-existente,
já compartilhado com `update_agent_transact.c`) foi ampliado para expor
helpers, view types e validators.

| Arquivo | LOC | Responsabilidade |
|---|---:|---|
| `src/services/update_agent.c` | 601 | globals, fetcher hooks UNIT_TEST, string/hex helpers, VFS read/write/remove + `active_*`, fetch_remote_manifest_text/fetch_payload_bytes (com hooks de teste), seed/init/reset/setters, status_get |
| `src/services/update_agent_parse.c` | 584 | version key parser, manifest validators (sha256, payload URL, ed25519), branch/URL builders, .ini line parsers, manifest_capture_signed_text, read_manifest_view, read_state_view, prepare_repository_status |
| `src/services/update_agent_apply.c` | 433 | `update_agent_poll` (state machine evaluator) + `update_agent_import_manifest_path` (offline import gate) |
| `src/services/update_agent_prepare.c` | 590 | write_state_file (local), update_agent_fetch_remote_manifest, update_agent_download_payload, prepare_dry_run/explain/staged_update, stage_latest, clear_stage, set_pending_activation |
| `src/services/internal/update_agent_internal.h` | — | path defines, view types, singleton + fetcher hooks, string/hex helpers, IO accessors, fetchers, view resets, validators, parsers, signed-text capture, buffered readers, prepare_repository_status |

`src/services/update_agent_transact.c` (boot-slot integration) ficou
inalterado e continua usando apenas `update_agent_g_status` +
`update_agent_local_copy` do internal header.

ABI público inalterado (todos os símbolos em `include/services/update_agent.h`).

## 3.6 Splits concluídos na onda 3 (2026-05-15)

Onda focada nos monolitos de **teste**. Cada split mantém uma entrada
principal (`run_*` ou `test_*_run`) no arquivo original e dispara casos
adicionais em arquivos companheiros via funções `*_cases()`. Helpers e
counters compartilhados ficam em internal headers.

### 3.6.1 `tests/test_gui_event.c` (1085 → 2 arquivos + header)

| Arquivo | LOC | Responsabilidade |
|---|---:|---|
| `tests/test_gui_event.c` | 361 | entry `test_gui_event_run`, contadores globais, `make_event` helper, e cobertura init/FIFO/poll/peek/dispatch/ready/backpressure |
| `tests/test_gui_event_helpers.c` | 757 | push helpers + coalescing (mouse move/scroll, paint, timer) + discard window + snapshot + reset + overflow |
| `tests/test_gui_event_internal.h` | — | TEST/PASS/FAIL macros, counter externs, `make_event` declaração, `test_gui_event_helper_cases` declaração |

### 3.6.2 `tests/test_gui_window_dispatcher.c` (985 → 2 arquivos + header)

| Arquivo | LOC | Responsabilidade |
|---|---:|---|
| `tests/test_gui_window_dispatcher.c` | 591 | fixture (counter globals, callback set, reset/shutdown), entry `test_gui_window_dispatcher_run`, testes: noop+key, key-up, scroll+paint, mouse, mouse capture, mouse capture reset |
| `tests/test_gui_window_dispatcher_lifecycle.c` | 424 | snapshot/health, focus+blur lifecycle, compositor-owned lifecycle, context menu, timer, miss/ignore |
| `tests/test_gui_window_dispatcher_internal.h` | — | TEST/PASS/FAIL macros, counter externs (run/pass + callback counters), callback declarações, fixture helper declarações, lifecycle entry declaração, aliases macro para preservar test bodies verbatim |

### 3.6.3 `tests/test_crypt_vectors.c` (1910 → 3 arquivos + header)

| Arquivo | LOC | Responsabilidade |
|---|---:|---|
| `tests/test_crypt_vectors.c` | 440 | mem-block-device harness, helpers `hex_nibble`/`decode_hex`/`expect_hex` (definições non-static), entry `run_crypt_vector_tests`, testes: SHA-256, PBKDF2, AES-XTS, block0 com wrappers, constant-time compare, sha256_clear |
| `tests/test_crypt_vectors_aead.c` | 659 | ed25519 fail-closed contract (RFC 8032 §7.1), HKDF-SHA256 (RFC 5869), ChaCha20 block (RFC 8439), ChaCha20 encrypt round-trip, Poly1305 (RFC 8439), ChaCha20-Poly1305 AEAD |
| `tests/test_crypt_vectors_kdf.c` | 669 | X25519 (RFC 7748 §5.2 scalar-mult + §6.1 ECDH + small-order rejection + NULL + high-bit + clamping), BLAKE2b (RFC 7693 + empty/multiblock/streaming/variable-output/keyed/fail-closed), Argon2id (RFC 9106 smoke), `crypt_derive_xts_keys_argon2id` |
| `tests/test_crypt_vectors_internal.h` | — | declarações de helpers + entries companheiros + aliases macro |

ABI/contrato de testes preservados: `run_crypt_vector_tests`,
`test_gui_event_run` e `test_gui_window_dispatcher_run` continuam
sendo as únicas entries que o aggregator de testes invoca.

## 3.7 Reorganização da árvore `tests/` (2026-05-15)

Concluída logo após onda 3. Os 189 arquivos `.c`/`.h` em `tests/`
estavam jogados na pasta raiz, dificultando localização e tornando
óbvio o crescimento sem padrão.

A reorganização introduz subdiretórios por domínio espelhando o
`src/` para tornar a navegação simétrica:

| Pasta nova | Arquivos | Conteúdo |
|---|---:|---|
| `tests/stubs/` | 10 | `stub_*.c`/`*.h` (substitutos de runtime para host tests) |
| `tests/auth/` | 11 | autenticação, login window, audit, user lifecycle, privilege |
| `tests/security/` | 13 | crypto vectors (3), csprng, TLS hostname, volume header/provider/rekey (6+1) |
| `tests/boot/` | 6 | boot manifest/writer/slot, EFI block, grub_cfg builder, gen_boot_config |
| `tests/fs/` | 7 | block wrappers, partition, capyfs check/journal, journal, buffer cache |
| `tests/gui/` | 8 | gui_event (3), gui_window_dispatcher (3), compositor, desktop smoke |
| `tests/net/` | 6 | DNS (resolver + cache), HTTP encoding, net probe, syscall net |
| `tests/userland/` | 3 | `capylibc_*` (ABI + net + TLS) |
| `tests/drivers/` | 18 | Hyper-V runtime (3) + gates, netvsc/netvsp/rndis, storvsc/storvsp, USB HID, keyboard layouts |
| `tests/kernel/` | 21 | scheduler, pmm, tasks, processes, vmm, context switch, syscall, pipe, op budget, fault classify, TSS, CPU local, enter user mode, stdin buf |
| `tests/kernel/linux_compat/` | 76 | `test_linux_*.c` (todos os syscall compat tests) |
| `tests/services/` | 6 | service_manager, service_boot_policy, service_runner, work_queue, update_agent, update_transact |
| `tests/apps/` | 2 | hello program + embedded_progs |
| `tests/lang/` | 1 | localization |
| `tests/` (raiz) | 1 | `test_runner.c` (entry point + forward decls) |

Total: 189 arquivos movidos. `Makefile/TEST_SRCS` reorganizado em
blocos por domínio para refletir a nova árvore. Os 3 paths na
baseline do `audit_source_layout.py` foram atualizados:
`tests/test_capylibc_net.c` → `tests/userland/test_capylibc_net.c`,
`tests/test_capylibc_tls.c` → `tests/userland/test_capylibc_tls.c`,
`tests/test_login_runtime.c` → `tests/auth/test_login_runtime.c`.

Nenhuma mudança de conteúdo de arquivo. Todos os `#include`
relativos continuam válidos (cada `.c` está no mesmo diretório que
seu `*_internal.h` companheiro).

## 3.8 Split concluído na onda 4 (2026-05-15)

### 3.8.1 `tests/userland/test_capylibc_net.c` (2025 → 3 arquivos + header)

Quebra por camada testada — sockets/DNS vs URL/HTTP-parser vs HTTP
end-to-end. O internal header expõe a fake-state estrutural
(`fake_state`, `g_fake`, `fake_reset`) usada pelos três
translation units. Os linker symbols dos fake stubs
(`capy_socket`, `capy_connect`, `capy_send`, `capy_recv`,
`capy_close`, `capy_dns_resolve`) ficam num único arquivo
(`test_capylibc_net.c`) para evitar redefinição.

| Arquivo | LOC | Responsabilidade |
|---|---:|---|
| `tests/userland/test_capylibc_net.c` | 613 | fake stubs (linker definitions), counter globals, sockets/DNS coverage (endian, inet_pton/ntoa, tcp_connect_ip4/str, send_all, recv_all, recv_until, resolve_host_ip4, tcp_connect_host, last_error_resets), entry `test_capylibc_net_run` |
| `tests/userland/test_capylibc_net_url.c` | 635 | URL parser (23 testes — schemes, ports, hostnames, paths, queries, fragments, fail-closed em CVE-class footguns) + HTTP request builder (12 testes) + HTTP status line parser (8 testes) + HTTP header parser (11 testes) |
| `tests/userland/test_capylibc_net_http.c` | 729 | `capy_http_get` end-to-end (32 testes — happy path, LF-only, chunked recv, truncation, Content-Length variations, Content-Encoding, Transfer-Encoding, headers beyond cap, request format) |
| `tests/userland/test_capylibc_net_internal.h` | 101 | declarações de `struct fake_state` + `g_fake` extern + `fake_reset` proto + counter externs + TEST/PASS/FAIL macros + companion entries |

ABI/contrato de testes preservado: `test_capylibc_net_run` continua
sendo a entry invocada pelo aggregator, e os fake stubs continuam
linkando com o código de `userland/lib/capylibc-net/`.

## 3.9 Split concluído na onda 5 (2026-05-15)

### 3.9.1 `tests/userland/test_capylibc_tls.c` (1324 → 3 arquivos + header)

Quebra por camada testada — lifecycle/connect/IO vs trust-store
metadata vs backend stub state machine. O internal header expõe
TEST/PASS/FAIL macros + counter externs + helper `fake_ctx()` usados
pelos três translation units. A ordem original de execução é
preservada (lifecycle → trust → backend → connect/IO/names).

| Arquivo | LOC | Responsabilidade |
|---|---:|---|
| `tests/userland/test_capylibc_tls.c` | 636 | lifecycle (init, config_resolve, context prepare/reset/clear, managed slot acquire/release/free), connect/IO/security_info/names, counter storage + fake_ctx + entry `test_capylibc_tls_run` (10 + 10 = 20 testes) |
| `tests/userland/test_capylibc_tls_trust.c` | 383 | default trust anchor catalog, slot table, descriptors, bundle/material summary, store manifest — entry `test_capylibc_tls_trust_cases()` (11 testes) |
| `tests/userland/test_capylibc_tls_backend.c` | 424 | default backend plan, BearSSL reserved state, BearSSL adapter contract, `capy_tls_backend_connect` state machine — entry `test_capylibc_tls_backend_cases()` (7 testes) |
| `tests/userland/test_capylibc_tls_internal.h` | 65 | TEST/PASS/FAIL macros + `tests_run`/`tests_passed` aliases + counter externs + `fake_ctx` proto + companion entry declarations + correct relative include path para `capy_tls_internal.h` |

ABI/contrato de testes preservado: `test_capylibc_tls_run` continua
sendo a entry invocada pelo aggregator, e o include relativo
`"../../userland/lib/capylibc-tls/capy_tls_internal.h"` foi corrigido
para a nova localização `tests/userland/`.

### 3.9.2 Correções de includes relativos pós-reorganização

A reorganização da onda 4 moveu 189 arquivos para subdiretórios e
quebrou 2 includes relativos que apontavam `"../userland/..."` (correto
quando o arquivo estava em `tests/`, errado quando passou a estar em
`tests/<subdir>/`):

- `tests/apps/test_hello_program.c`: `"../userland/bin/hello/main.c"`
  → `"../../userland/bin/hello/main.c"`.
- `tests/userland/test_capylibc_tls.c`:
  `"../userland/lib/capylibc-tls/capy_tls_internal.h"` →
  `"../../userland/lib/capylibc-tls/capy_tls_internal.h"` (movido
  para o internal header criado na onda 5).

Auditoria via `grep -rn '#include "\.\./' tests/` agora retorna apenas
includes corretos (paths `../../...`).

## 4. Splits pendentes — planos por arquivo

### 4.1 `src/apps/file_manager.c` (1132 LOC) — prioridade média

Aplicação gráfica de gerenciamento de arquivos. Split natural por
responsabilidade UI vs FS:

| Arquivo proposto | LOC alvo | Responsabilidade |
|---|---:|---|
| `src/apps/file_manager/file_manager.c` | ~250 | entry point + dispatch de eventos |
| `src/apps/file_manager/file_manager_view.c` | ~350 | desenho de janela, ícones, lista |
| `src/apps/file_manager/file_manager_ops.c` | ~300 | operações copy/move/delete/rename |
| `src/apps/file_manager/file_manager_navigation.c` | ~250 | cwd, histórico, breadcrumbs |
| `src/apps/file_manager/internal/file_manager_internal.h` | — | header interno |

Cuidados: a aplicação é exercitada apenas em runtime; sem testes
host-side hoje. Adicionar pelo menos um teste de view-model como gate
de regressão é recomendado mas não bloqueante.

### 4.2 `src/apps/settings.c` (1081 LOC) — prioridade média

App de configurações com várias páginas:

| Arquivo proposto | LOC alvo | Responsabilidade |
|---|---:|---|
| `src/apps/settings/settings.c` | ~150 | entry point + roteamento de páginas |
| `src/apps/settings/settings_view.c` | ~300 | rendering compartilhado |
| `src/apps/settings/settings_pages_system.c` | ~300 | páginas de sistema (idioma, teclado, tema, hostname) |
| `src/apps/settings/settings_pages_security.c` | ~250 | páginas de senha, lockout, volume |
| `src/apps/settings/internal/settings_internal.h` | — | header interno |

Risco: páginas tendem a compartilhar widgets de input. Avaliar se um
`settings_widgets.c` separado se justifica após o primeiro split.

### 4.3 `src/gui/desktop/taskbar.c` (1436 LOC) — prioridade média

Barra de tarefas com múltiplas zonas:

| Arquivo proposto | LOC alvo | Responsabilidade |
|---|---:|---|
| `src/gui/desktop/taskbar.c` | ~250 | entry point + dispatch |
| `src/gui/desktop/taskbar_layout.c` | ~280 | layout de zonas (start, app list, tray, clock) |
| `src/gui/desktop/taskbar_rendering.c` | ~300 | desenho das zonas |
| `src/gui/desktop/taskbar_events.c` | ~280 | mouse hover, click, focus |
| `src/gui/desktop/taskbar_tray.c` | ~250 | indicadores de bateria, rede, áudio |
| `src/gui/desktop/internal/taskbar_internal.h` | — | header interno |

Cobertura de testes atual: `tests/test_desktop_smoke_readiness.c`.
Sem regressão funcional, todos os testes existentes devem continuar
passando.

### 4.4 `src/security/ed25519.c` (1465 LOC) — prioridade média

Primitiva criptográfica auditada. Split sequenciado pelo
[plano dedicado de monolitos residuais](../plans/active/monolith-residual-dedicated-plan.md)
estágio A. **Cada PR exige vetores RFC 8032 §7 + `make all64`
externos antes de promover**:

| Arquivo proposto | LOC alvo / atual | Responsabilidade | Status |
|---|---|---|---|
| `src/security/ed25519.c` | ~250 / **286** | public APIs (`ed25519_create_keypair`, `_sign`, `_verify`) + SHA-512 helpers + entry banner | **estágio A done 2026-05-15** |
| `src/security/ed25519_group.c` | ~280 / **306** | constantes do grupo + Edwards point arithmetic + scalar mult (constant-time) | **PR A.1 done 2026-05-15** |
| `src/security/ed25519_encode.c` | ~180 / **208** | point encode/decode (32-byte compressed form, RFC 8032 §5.1.2-§5.1.3) | **PR A.2 done 2026-05-15** |
| `src/security/ed25519_scalar.c` | ~800 / **836** | aritmética escalar mod L (`sc_reduce64`, `sc_muladd`, `sc_is_canonical`, `ED_L_BYTES`, `load_3`/`load_4`) | **PR A.3 done 2026-05-15** |
| `src/security/internal/ed25519_internal.h` | ~80 / **139** | `ge_p3`, `ED_D`/`ED_SQRTM1`, group/codec/scalar helpers cross-file | **criado/estendido 2026-05-15** |

Testes em `tests/security/test_crypt_vectors_aead.c` (659 LOC, já
refatorado na onda 3) cobrem ed25519 com vetores RFC 8032 §7.1. O
split não pode quebrar nenhum vetor — validação externa via
`make test` é mandatória **por PR**.

### 4.5 `src/services/update_agent.c` (1990 LOC) — prioridade média

Serviço de atualizações com múltiplas fases (discover, download,
verify, apply, rollback):

| Arquivo proposto | LOC alvo | Responsabilidade |
|---|---:|---|
| `src/services/update_agent.c` | ~250 | API pública + state machine principal |
| `src/services/update_agent_discover.c` | ~300 | descoberta de releases |
| `src/services/update_agent_download.c` | ~400 | download + integridade |
| `src/services/update_agent_verify.c` | ~350 | verificação de assinatura ed25519 |
| `src/services/update_agent_apply.c` | ~400 | aplicação + rollback path |
| `src/services/update_agent_state.c` | ~300 | persistência de estado em disco |
| `src/services/internal/update_agent_internal.h` | — | tipos e helpers compartilhados |

Coberto por `tests/test_update_agent.c` (extenso). Coordenar split
com testes — possível necessidade de quebrar o test file também.

### 4.6 `src/auth/login_runtime.c` (22776 → **282** LOC) — **CONCLUÍDO na onda 7 (Estágio C dedicado)**

Refatorado em 2026-05-15 via PRs C.0-C.65 (66 PRs total) do
[plano dedicado de monolitos residuais](../plans/active/monolith-residual-dedicated-plan.md)
estágio C. Resultado entregue:

- **`src/auth/login_runtime.c`** reduzido para **282 LOC** (facade com
  5 helpers `static` privados + entry point `login_runtime_run`).
- **65 TUs novos** em `src/auth/login_runtime/` (total 27616 LOC,
  todos abaixo de 900 LOC). Maiores arquivos: `compositor_damage.c`
  (656 LOC), `purge_plan.c` (655 LOC), `window_event_plan.c`
  (650 LOC), `frame_surface.c` (645 LOC), `queue_activation.c`
  (641 LOC).
- **1 internal header** em `src/auth/internal/login_runtime_internal.h`
  (74 LOC, 5 helpers `static inline`: `dbg_login_putc`,
  `dbg_login_puts`, `ops_ready`, `login_service_poll`,
  `login_maintenance_mode_active`).

Distribuição por fase do Estágio C:

| Fase | PRs | Arquivos novos | Tema |
|---:|---|---:|---|
| 1 | C.0 | 1 internal header | Helpers `static inline` compartilhados |
| 2 | C.1–C.6 | 6 | Contract/policy/credential pré-pipeline |
| 3 | C.7–C.15 | 9 | Pipeline plan builders (`render_action_ui_event` → `present_plan`) |
| 4 | C.16–C.41 | 26 | Per-plan `reset` + `is_safe` pairs (`schedule_plan` → `expiry_plan`) |
| 5 | C.42–C.46 | 5 | Purge/reclaim section |
| 6 | C.47–C.63 | 17 | GUI/Window section |
| 7 | C.64–C.65 | 2 | `pipeline_safety.c` + `view_model.c` |

ABI público preservado byte-for-byte em cada PR. Linkage estática
preservada (cada `.c` próprio TU). `Makefile` atualizado em sincronia
(65 entradas em `CAPYOS64_OBJS` + 65 entradas em `TEST_SRCS`). Saiu
da baseline `MONOLITH_BASELINE_EXCEPTIONS`.

**Validação externa pendente** (não rodável nesta máquina por
política operacional):
- `make test` — exercita `tests/auth/test_login_runtime.c` + os 65
  novos TUs via aggregator.
- `make all64` — linker deve enxergar os 65 objetos novos sob
  `$(BUILD)/x86_64/auth/login_runtime/`.
- `make layout-audit` — deve confirmar saída de `login_runtime.c` da
  baseline.
- `make smoke-x64-vmware-*` para login flow end-to-end.

### 4.7 `tests/auth/test_login_runtime.c` (25663 LOC) — **prioridade alta, casado com 4.6**

Espelho do `src/auth/login_runtime.c`. Cada arquivo de teste novo casa
1:1 com o arquivo de source novo. Não dividir antes do split do source.

| Arquivo de teste proposto | LOC alvo | Cobre |
|---|---:|---|
| `tests/auth/test_login_contract.c` | ~150 | `login_contract.c` |
| `tests/auth/test_login_recovery.c` | ~250 | `login_recovery.c` |
| (... 1:1 com cada arquivo do source ...) | | |

### 4.8 `tests/userland/test_capylibc_net.c` (2025 LOC) — **CONCLUÍDO na onda 4**

Refatorado em 2026-05-15 (ver §3.8). Resultado: 3 arquivos +
internal header, todos abaixo de 900 LOC. Localização atual:
`tests/userland/test_capylibc_net{,_url,_http}.c`.

### 4.9 `tests/userland/test_capylibc_tls.c` (1324 LOC) — **CONCLUÍDO na onda 5**

Refatorado em 2026-05-15 (ver §3.9). Resultado: 3 arquivos +
internal header, todos abaixo de 900 LOC. Localização atual:
`tests/userland/test_capylibc_tls{,_trust,_backend}.c`. O split do
plano original (handshake/record/trust) foi substituído pela
segregação real do código (lifecycle/connect/IO vs trust-store
metadata vs backend stub state machine), já que o código testado
fica fail-closed enquanto o BearSSL backend não aterriza.

### 4.10 `tests/test_crypt_vectors.c` (1910 LOC) — **CONCLUÍDO na onda 3**

Refatorado em 2026-05-15 (ver §3.6.3). Resultado: 3 arquivos +
internal header, todos abaixo de 900 LOC. Localização atual:
`tests/security/test_crypt_vectors{,_aead,_kdf}.c`.

### 4.11 `tests/test_gui_event.c` (1085 LOC) — **CONCLUÍDO na onda 3**

Refatorado em 2026-05-15 (ver §3.6.1). Resultado: 2 arquivos +
internal header, ambos abaixo de 900 LOC. Localização atual:
`tests/gui/test_gui_event{,_helpers}.c`.

### 4.12 `tests/test_gui_window_dispatcher.c` (985 LOC) — **CONCLUÍDO na onda 3**

Refatorado em 2026-05-15 (ver §3.6.2). Resultado: 2 arquivos +
internal header, ambos abaixo de 900 LOC. Localização atual:
`tests/gui/test_gui_window_dispatcher{,_lifecycle}.c`.

### 4.13 `src/arch/x86_64/kernel_main.c` (989 LOC) — prioridade baixa

Já passou por múltiplos splits anteriores conforme cabeçalho do
arquivo. Extração possível: helpers (range_ok, dbg_hex*,
pmm_init_from_handoff, kernel_halt_forever, kernel_log_boot_warnings)
para `kernel_main_helpers.c` (~120 LOC) baixando o arquivo para
~870 LOC. Ganho marginal; recomenda-se priorizar os outros monolitos
primeiro.

## 5. Sequência de execução recomendada

1. **Apps independentes (baixo risco):** `file_manager.c`, `settings.c`.
2. **GUI (médio risco):** `taskbar.c`.
3. **Crypto secundário (médio risco):** `ed25519.c` casado com
   `test_crypt_vectors.c`.
4. **Service (médio risco):** `update_agent.c` casado com
   `test_update_agent.c`.
5. **Tests independentes:** `test_capylibc_net.c`, `test_capylibc_tls.c`,
   `test_gui_event.c`, `test_gui_window_dispatcher.c`.
6. **Login runtime (alto risco, plano dedicado):**
   `src/auth/login_runtime.c` + `include/auth/login_runtime.h` +
   `tests/test_login_runtime.c` em onda única, com sub-pastas e
   validação externa completa.
7. **Limpeza final:** `kernel_main.c` (opcional, ganho marginal).

## 6. Política de validação por etapa

Cada split deve ser concluído com:

- **Inspeção estática local** (este computador): símbolos cruzados,
  paths de include corretos, Makefile atualizado, audit script
  atualizado.
- **Validação externa obrigatória** (outra máquina ou CI):
  - `make test` quando o código está em `TEST_SRCS`.
  - `make all64` quando o código é parte do kernel.
  - `make layout-audit` para confirmar saída da baseline.
  - `make release-check` antes de aceitar uma onda inteira.

## 7. Métricas de progresso

| Indicador | Inicial | Onda 1 | Onda 2 | Onda 3 | Onda 4 | Onda 5 | Onda 7 (2026-05-15) | Onda 8 (2026-05-16) | Meta final |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Arquivos C runtime > 900 LOC | 9 | 4 | 3 | 3 | 3 | 3 | **2** | **1** | 0 |
| Arquivos test > 900 LOC | 6 | 6 | 6 | 3 | 2 | 1 | **1** | **0** | 0 |
| Linhas em exceção (runtime) | ~35 000 | ~30 800 | ~28 800 | ~28 800 | ~28 800 | ~28 800 | **~6 100** | **989** | 0 |
| Linhas em exceção (tests) | ~33 000 | ~33 000 | ~33 000 | ~29 000 | ~27 000 | ~25 700 | **~25 700** | **0** | 0 |
| Arquivos novos criados (onda 1) | 0 | **17** | 17 | 17 | 17 | 17 | 17 | 17 | — |
| Arquivos novos criados (onda 2) | 0 | — | **3** | 3 | 3 | 3 | 3 | 3 | — |
| Arquivos novos criados (onda 3) | 0 | — | — | **6** | 6 | 6 | 6 | 6 | — |
| Arquivos novos criados (onda 4) | 0 | — | — | — | **2 + reorg** | 2 | 2 | 2 | — |
| Arquivos novos criados (onda 5) | 0 | — | — | — | — | **2 + 2 fix** | 2 | 2 | — |
| Arquivos novos criados (onda 7) | 0 | — | — | — | — | — | **65** | 65 | — |
| Arquivos novos criados (onda 8) | 0 | — | — | — | — | — | — | **46** | — |
| Internal headers criados/expandidos | 0 | 5 | 6 | 9 | 10 | 11 | **12** | **13** | — |
| Monolitos refatorados / tratáveis | 0/11 | 5/11 | 6/11 | 9/11 | 10/11 | 11/11 | **12/12** | **13/13** | 13/13 |

**13 de 13 monolitos tratáveis refatorados = 100%** (excluindo
apenas `kernel_main.c` deferred).  `ed25519.c` saiu da baseline em
2026-05-15 via estágio A do plano dedicado (PRs A.1+A.2+A.3, 1465 →
286 LOC). `login_runtime.c` saiu da baseline em 2026-05-15 via
estágio C do plano dedicado (PRs C.0-C.65, 22 776 → 282 LOC, 65
arquivos novos + 1 internal header). `test_login_runtime.c` saiu da
baseline em 2026-05-16 via estágio D do plano dedicado (PRs D.7-
D.47, 22 113 → 529 LOC, 46 arquivos companion + 1 internal test
header).

Onda 4 também concluiu a **reorganização estrutural de `tests/`** em
14 subdiretórios por domínio (189 arquivos movidos). Onda 5 corrigiu
2 includes relativos quebrados pela reorganização e concluiu o último
test monolith diretamente atacável.

O único arquivo restante em violação (após PRs A.3 + C.65 + D.47
retirarem `ed25519.c`, `login_runtime.c` e `test_login_runtime.c`
respectivamente):

- `src/arch/x86_64/kernel_main.c` (989 LOC) — deferred (já passou por
  splits anteriores; remoção adicional implica deslocar
  responsabilidades de boot/arch que tocam ABIs sensíveis).

**Plano dedicado para os residuais:** ver
`docs/plans/active/monolith-residual-dedicated-plan.md`. Resumo:

- **Estágio A** (ed25519): 4 arquivos + 1 internal header. **Done
  2026-05-15** via PR A.1+A.2+A.3. Pendente apenas o gate externo
  combinado (vetores RFC 8032 §7 + `make all64` + `make release-check`).
- **Estágios B+C combinados** (login_runtime.h + .c): **Done
  2026-05-15**. Estágio B (header) entregou via PRs 1-12 + 11a-e
  (47 partial headers, header reduzido de 10881 para 142 LOC).
  Estágio C (source) entregou via PRs C.0-C.65 (65 TUs novos +
  1 internal header, source reduzido de 22776 para 282 LOC).
  Pendente apenas o gate externo combinado (`make test` + `make
  all64` + `make layout-audit` + `make release-check`).
- **Estágio D** (test_login_runtime.c): **Done 2026-05-16** via PRs
  D.7-D.47. 46 companion TUs em `tests/auth/test_login_runtime_credential_*.c`
  + 1 internal test header em `tests/auth/test_login_runtime_internal.h`,
  arquivo principal reduzido de 22 113 para 529 LOC (97,6% de
  redução). Cada companion espelha 1:1 um stage do pipeline de
  credential screen (mapping 1:1 com os TUs do estágio C). Pendente
  apenas o gate externo combinado (`make test` + `make layout-audit` +
  `make release-check`).
- **Estágio E** (kernel_main): permanece deferred. Após conclusão
  dos estágios A-D, é o único arquivo restante na baseline. Avaliação
  separada exigida.

Com o estágio D concluído, apenas `kernel_main.c` permanece na
baseline (1/16 = 6,25% remanescente, **93,75% de redução total**).
Quando `kernel_main.c` sair da baseline (estágio E), o
`audit_source_layout.py` deve passar a `--strict` sem warnings, e
`MONOLITH_BASELINE_EXCEPTIONS` fica reduzido a uma comment-only set
(ou removido).

## 8. Notas para validação local

Refactor de comportamento preservado é validável por inspeção
estática + diff review. Cada split deste roadmap deve ser PR-able
isoladamente com:

1. Lista de funções movidas (e renomeadas, se for o caso).
2. Lista de novos arquivos + LOC.
3. Diff do Makefile.
4. Diff do `audit_source_layout.py`.
5. Diff dos call-sites (zero, idealmente — só renames internos).

Validação externa fica responsabilidade do operador humano que
executa `make test`/`make all64`/`make layout-audit` numa máquina
autorizada.
