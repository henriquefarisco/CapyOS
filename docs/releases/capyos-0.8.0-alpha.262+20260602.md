# CapyOS 0.8.0-alpha.262+20260602

**Data:** 2026-06-02
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.262+20260602`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** fecho da Etapa 4 (Fase F validada externamente) + batch de hardening regressivo (5 fixes) + compliance de versoes cross-repo (lote coordenado de 7 repos)

> **Tag a ser criada manualmente pelo operador.** Esta nota descreve o
> estado preparado no tree; o git HEAD anterior era a tag
> `v0.8.0-alpha.261+20260529`. A versao foi pre-bumpada (VERSION.yaml,
> include/core/version.h, README.md) para que `make version-audit`
> passe; ver `DEPLOY-WORKSPACE.txt` na raiz do workspace para a ordem de
> deploy do lote completo de 7 repositorios. **Se o deploy ocorrer em
> outro dia, ajuste o sufixo `+YYYYMMDD` de forma consistente em
> VERSION.yaml (current/extended/history), include/core/version.h
> (CAPYOS_VERSION_FULL), README.md, no nome deste arquivo e na tag.**

## Resumo executivo

Fecha a **Etapa 4 (CapyDisplay 2D + scheduler/multithread runtime)**: as
Fases A-E estavam fechadas em codigo + host tests desde `alpha.260`; esta
release marca a **Fase F** — validacao externa em VMware + UEFI + E1000
via o gate agregado `make smoke-x64-vmware-etapa-4` — como o gate de
fechamento. Acompanha um **batch de hardening regressivo de 5 fixes**
(seguranca + performance) e a **compliance cross-repo** que registra os
releases coordenados dos 6 repositorios irmaos.

## Mudancas entregues

### A) Hardening regressivo — 5 fixes (cada um com host test onde aplicavel)

1. **ATA-PIO DF/ERR fatal-status** (integridade de dados). `ata_wait_ready`
   passa a checar Device Fault/ERR apos limpar BSY e falha fechado com log.
   Novos: `include/drivers/storage/ata_status.h`,
   `src/drivers/storage/ata_status.c`, `tests/drivers/test_ata_status.c`
   (11 casos). `src/drivers/storage/ata_pio.c` usa os predicados puros.
2. **fsck superblock geometry overflow** (memory safety). Validacao
   fail-closed em `uint64` da geometria do superblock **antes** de
   qualquer alocacao, em `fsck_check` e `fsck_repair`. Novos:
   `include/fs/fsck_geometry.h`, `src/fs/fsck/fsck_geometry.c`,
   `tests/fs/test_fsck_geometry.c` (11 casos).
3. **Compositor surface-dim overflow** (integer-overflow -> OOB). Cap
   fail-closed `COMPOSITOR_MAX_SURFACE_DIM` em `alloc_surface`.
   `include/gui/compositor.h`, `src/gui/core/compositor.c`.
4. **TLS free-wipe de segredos** (higiene de segredos). `tls_memzero`
   volatile-safe + `tls_free` zera iobuf e ctx (chaves de sessao) antes
   do `kfree`. `src/security/tls.c`.
5. **memcpy/memset word-at-a-time** (performance kernel-wide). 8 bytes por
   iteracao, alinhado, comportamento identico provado por teste exaustivo
   de equivalencia. Novos: `include/util/string_ops.h`,
   `tests/util/test_string_ops.c`; `src/arch/x86_64/stubs.c` delega.

Wiring de testes: `Makefile` (TEST_SRCS) e `tests/test_runner.c`
atualizados para `ata_status`, `fsck_geometry` e `string_ops`.

### B) Fonte de verdade / docs (Etapa 4 e preparacao da Etapa 5)

- `docs/plans/active/capyos-master-plan.md` (§7 e §20) + `STATUS.md`
  reconciliados: Etapa 4 Fases A-E fechadas em codigo + host tests; unico
  bloqueador = Fase F externa.
- Novo `docs/plans/active/etapa-4-closure-tracker.md` (estado por fase).
- Atualizado `docs/operations/etapa-4-external-validation-playbook.md`.
- Novo `docs/architecture/etapa-5-tls-userland-readiness.md` (auditoria
  **read-only** do TLS; **sem** implementacao da Etapa 5 nesta release).

## Compliance de versoes (cross-repo) — lote coordenado de 7 repos

Esta release e o **pivot da matriz** para um lote coordenado em que os 6
repositorios irmaos tambem geram release. Registrado em
`compatibility-matrix.md`, `external-core-repositories.md`, `STATUS.md`,
nos contratos de integracao e no novo `compatibility-audit-2026-06-02.md`:

| Repo | De | Para | ABI |
|---|---|---|---|
| **CapyOS** | `alpha.261+20260529` | `alpha.262+20260602` | `capyos-base` v3 + `capyos-package-apply` v1 |
| **CapyUI** | `2.19.0` | `2.22.0` | `capy-ui-widget` v2.19 -> **v2.22** (display-list schema 7 inalterado) + `capy-ui-desktop-session` v1 |
| **CapyCodecs** | `0.0.6` | `0.0.7` | `capy-codec-image` v1 -> **v2** (aditiva) |
| **CapyLang** | `0.1.7` | `0.1.8` | `capy-lang-host` v0 (+opcodes 0x64-0x66 + trap V0018; 36 -> 39 opcodes) |
| **CapyBrowser** | `0.0.6` | `0.3.0` | `capy-browser-core` v1 planejada (URL/HTML-to-text/image-adapter/DOM host-testaveis) |
| **CapyAgent** | `0.0.6` | `0.0.7` | `capy-agent-component-index` v1 (**Ed25519 signer publicado host-side**) |
| **CapyBenchmark** | `0.0.6` | `0.0.7` | `capy-benchmark-report` v1 planejada (serializacao report/eval/replay) |

- **Pins do CapyOS core** nos 6 sisters (`docs/compatibility.md`):
  `alpha.261 -> alpha.262`.
- Todos os bumps de ABI sao **aditivos**; nenhum contrato breaking.

## P0 do signer Ed25519 (CapyAgent) — atualizado

O **Ed25519 signer do CapyAgent** — bloqueador P0 historico da Etapa 9 —
foi **publicado host-side** em `CapyAgent/src/signer/` (SHA-512 FIPS 180-4
+ Ed25519 RFC 8032 + serializador de manifest canonico + callback
`capyagent_ed25519_verifier` com assinatura compativel com o
`capypkg_verify_signature_fn` do CapyOS). **Restam dois gates** antes de
liberar `signed` em release:

1. Validacao externa dos known-answer tests (RFC 8032 + FIPS 180-4) via
   `make validate` no CapyAgent (fora desta maquina).
2. Registro do verifier no CapyOS via `capypkg_set_signature_verifier`
   (so quando a Etapa 9 abrir).

Ate la, o slot do verifier no
`src/arch/x86_64/kernel_services.c::kernel_capypkg_bind_runtime_adapters`
permanece **NULL by design** e o adapter rejeita repositorios `signed`
com `CAPYPKG_ERR_SIGNATURE` (fail-closed). O escopo canonico do descritor
Ed25519 (`name=N|version=V|payload_sha256=H|payload_url=U\n`) permanece
**inalterado**.

## Mudancas de contrato

Do lado do **CapyOS core**: **nenhuma**. Adapter `services/capypkg`,
activation gate `kernel/module_gate.c`, install profile schema,
line-oriented manifest format, escopo do descritor Ed25519, quotas
`CAPYPKG_*` e `install_root` scope **intactos**. As ABIs novas dos fixes
(`drivers/storage/ata_status.h`, `fs/fsck_geometry.h`, `util/string_ops.h`)
sao internas e aditivas; `struct compositor_stats`/`compositor.h` ganham
apenas campo/limite aditivo.

Do lado dos **sisters**: todos os bumps de ABI sao **aditivos** dentro do
major (CapyUI v2.x, CapyCodecs image v2 com assinaturas antigas
preservadas como wrappers, CapyLang v0 com opcodes append-only, CapyAgent
v1 com signer aditivo, CapyBenchmark v1 com serializacao aditiva).

## Evidencias / validacao

Validacao **nao executada** nesta maquina (politica review/edit-only do
workspace). Gates recomendados para o operador/CI antes de promover a tag
(detalhe e ordem completa do lote em `DEPLOY-WORKSPACE.txt` na raiz):

- `make test` — host tests (inclui `ata_status`, `fsck_geometry`, `string_ops`).
- `make layout-audit` e `make version-audit`.
- `make all64 PROFILE=full` (com `../CapyUI` 2.22.0 como sibling).
- `make iso-uefi`.
- `make smoke-x64-vmware-usb-hid-keyboard` e
  `make smoke-x64-vmware-storage-resilience` (regressoes da Etapa 3).
- `make smoke-x64-vmware-etapa-4` (Fase F; 5 markers em ordem:
  `[net] DHCP: lease acquired.` -> `[smoke] gui-session ready` ->
  `[smoke] scheduler-fairness ready` -> `[smoke] compositor-damage-track ready`
  -> `[smoke] thread-crash-survives ready`).
- `make release-check`.

## Proximos passos

1. Rodar os gates externos acima fora desta maquina (lote de 7 repos).
2. Criar manualmente a tag `v0.8.0-alpha.262+20260602` (develop + main).
3. Etapa 4 fica formalmente fechada quando a Fase F passar; Etapa 5 (TLS
   userland real) e a proxima da sequencia.
4. P0 do signer Ed25519: validar KAT externamente; registrar o verifier
   quando a Etapa 9 abrir.
