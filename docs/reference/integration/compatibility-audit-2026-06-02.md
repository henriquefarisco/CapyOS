# Cross-repo compatibility audit — 2026-06-02

**Status:** snapshot do **lote coordenado de release de 7 repositórios**
pinado para CapyOS core `0.8.0-alpha.262+20260602`.
**Snapshot anterior:** [`compatibility-audit-2026-05-23.md`](compatibility-audit-2026-05-23.md).
**Matriz autoritativa:** [`compatibility-matrix.md`](compatibility-matrix.md).
**Deploy do lote:** `DEPLOY-WORKSPACE.txt` na raiz do workspace.

## Resumo

Este audit registra um lote coordenado em que **os 7 repositórios geram
release juntos**. Todas as mudanças de ABI são **aditivas** (nenhuma
breaking); o CapyOS core é o pivot que pina as versões irmãs. O
destaque é a **publicação host-side do Ed25519 signer do CapyAgent** —
bloqueador P0 histórico da Etapa 9 — ainda pendente de dois gates
(KAT externo + registro do verifier no CapyOS).

As versões foram **pré-bumpadas no working tree** (VERSION/headers/docs)
para que os gates de versão (`make version-audit` no CapyOS; `version-check`
nos sisters) passem; commit, tag e push são tarefa do deploy externo
(este workspace é review/edit-only).

## Estado coordenado (de → para)

| Repo | De | Para | ABI | Natureza |
|---|---|---|---|---|
| `CapyOS` | `0.8.0-alpha.261+20260529` | `0.8.0-alpha.262+20260602` | `capyos-base` v3 + `capyos-package-apply` v1 | Etapa 4 Fase F + 5 fixes de hardening |
| `CapyUI` | `2.19.0` | `2.22.0` | `capy-ui-widget` v2.19 → **v2.22** + `capy-ui-desktop-session` v1 | aditiva (rich-text ranges, canvas draw callback, multi-touch pinch/rotate); display-list schema **7 inalterado** |
| `CapyCodecs` | `0.0.6` | `0.0.7` | `capy-codec-image` v1 → **v2** | aditiva (per-call limits, detect/generic decode, metadata query, QOI) |
| `CapyLang` | `0.1.7` | `0.1.8` | `capy-lang-host` v0 | aditiva (S6.3 structs/enums; +opcodes 0x64-0x66 + trap V0018; 36 → 39 opcodes; header de 32 bytes intacto) |
| `CapyBrowser` | `0.0.6` | `0.3.0` | `capy-browser-core` v1 (planejada) | superfícies host-testáveis (URL, HTML-to-text, image adapter, DOM); runtime ainda Etapas 6-7 |
| `CapyAgent` | `0.0.6` | `0.0.7` | `capy-agent-component-index` v1 | aditiva (**Ed25519 signer publicado host-side**; verifier pendente) |
| `CapyBenchmark` | `0.0.6` | `0.0.7` | `capy-benchmark-report` v1 (planejada) | aditiva (serialização report/evaluation/replay + baseline fixtures) |

Pins do CapyOS core nos 6 `docs/compatibility.md` irmãos movidos de
`0.8.0-alpha.261+20260529` (+ audit `2026-05-23`) para
`0.8.0-alpha.262+20260602` (+ audit `2026-06-02`).

## CapyOS core — conteúdo da release `alpha.262`

**Etapa 4 (CapyDisplay 2D + scheduler/multithread):** Fases A-E fechadas
em código + host tests desde `alpha.260`; esta release marca a **Fase F**
(gate externo `make smoke-x64-vmware-etapa-4`, 5 markers em ordem) como o
gate de fechamento. **A Etapa 4 só fecha formalmente quando a Fase F
passar externamente** — os 6 critérios de aceite no master-plan §7
permanecem `[ ]` até a confirmação em VMware oficial.

**Batch de hardening regressivo (5 fixes):**

1. ATA-PIO DF/ERR fatal-status — `include/drivers/storage/ata_status.{h,c}`, `tests/drivers/test_ata_status.c` (11 casos).
2. fsck superblock geometry overflow — `include/fs/fsck_geometry.h`, `src/fs/fsck/fsck_geometry.c`, `tests/fs/test_fsck_geometry.c` (11 casos).
3. compositor surface-dim overflow cap `COMPOSITOR_MAX_SURFACE_DIM` — `include/gui/compositor.h`, `src/gui/core/compositor.c`.
4. TLS free-wipe de segredos volatile-safe — `src/security/tls.c`.
5. memcpy/memset word-at-a-time — `include/util/string_ops.h`, `src/arch/x86_64/stubs.c`, `tests/util/test_string_ops.c`.

**Sem mudança de contrato cross-repo no core:** `services/capypkg`,
`kernel/module_gate.c`, install profile schema, line-oriented manifest
format, escopo do descritor Ed25519, quotas `CAPYPKG_*` e `install_root`
intactos.

## P0 — Ed25519 signer do CapyAgent (mudança de status)

**Publicado host-side** em `CapyAgent/src/signer/` (`0.0.7`):

- `src/signer/sha512.{c,h}` — SHA-512 FIPS 180-4 (host-testável, sem alocação/IO).
- `src/signer/ed25519.{c,h}` — Ed25519 RFC 8032 (referência TweetNaCl; verify constant-time; wipe volatile-safe).
- `src/signer/capyagent_signer.{c,h}` — hex codec, derivação de keypair por seed, assinar/verificar descritor canônico, gestão de chave confiável, e o callback `capyagent_ed25519_verifier(signed_text, signed_len, signature_hex)` **com assinatura compatível** com o `capypkg_verify_signature_fn` do CapyOS.
- `src/component_index/component_manifest.{c,h}` — serializador do descritor canônico + emissor de manifest line-oriented.
- `tests/test_signer.c` + `tests/test_manifest.c` — known-answer tests (RFC 8032 + FIPS 180-4) + serialização/negativos.

**Escopo canônico do descritor (inalterado):**
`name=N|version=V|payload_sha256=H|payload_url=U\n` (separadores `|`
literais, único `\n` terminador).

**Dois gates restantes antes de promover `signed`:**

1. Validação externa dos KAT (RFC 8032 + FIPS 180-4) via `make validate` no CapyAgent.
2. CapyOS registrar o verifier via `capypkg_set_signature_verifier` quando a Etapa 9 abrir.

Até ambos passarem, o slot do verifier em
`src/arch/x86_64/kernel_services.c::kernel_capypkg_bind_runtime_adapters`
permanece **NULL by design** e o adapter rejeita repositórios `signed`
com `CAPYPKG_ERR_SIGNATURE` (fail-closed). Trabalho `--unsigned` de
laboratório permanece permitido, nunca promovido.

## Notas por ABI

- **`capy-ui-widget` v2.19 → v2.22:** três fatias aditivas no tail das
  structs (`capy_widget`, `capy_gesture_recognizer`); `CAPYUI_API_VERSION_TAG`
  `0x00021300 → 0x00021600`; testes de contrato 321 → 345. Display-list
  schema permanece 7 — o adapter CapyOS-side (`capyui_display_adapter`)
  não é afetado. Quem compilava contra 2.19 recompila contra 2.22 sem
  mudança de fonte.
- **`capy-codec-image` v1 → v2:** assinaturas antigas preservadas como
  wrappers finos; `CAPY_IMAGE_ABI_VERSION = 2`; novos flags de feature,
  novos entry points `_limited`, detect/generic decode, metadata query e
  codec QOI. Pixel format ARGB32 inalterado. Fail-closed e determinístico.
- **`capy-lang-host` v0:** opcodes append-only `0x64` MakeAggregate /
  `0x65` GetField / `0x66` GetTag + trap `V0018 FIELD_OUT_OF_BOUNDS`;
  header de 32 bytes e tags de seção intactos. ABI de integração com o
  CapyOS permanece roadmap-blocked até a Etapa 15.
- **`capy-browser-core` (planejada):** URL parse/normalize/origin,
  HTML-to-text, image adapter (consome `capy-codec-image` por ABI) e
  parser DOM-like — todos host-testáveis e determinísticos. Sem runtime
  ativo até as Etapas 6-7.
- **`capy-benchmark-report` (planejada):** serialização line-oriented
  `key=value` de report/evaluation/replay + fixtures de baseline
  (snake/asteroids). Roadmap-blocked até as Etapas 15-16.

## Validação

Validação local **não executada** (workspace review/edit-only). Gates
externos recomendados, por repo, em `DEPLOY-WORKSPACE.txt`. Resumo:

- CapyOS: `make test`, `make layout-audit`, `make version-audit`,
  `make all64 PROFILE=full` (sibling `../CapyUI` 2.22.0),
  `make iso-uefi`, regressões `usb-hid-keyboard`/`storage-resilience`,
  `make smoke-x64-vmware-etapa-4` (Fase F), `make release-check`.
- CapyUI: `make validate` (345 testes) + `make package`.
- CapyCodecs: `make validate` (compila estrito + contract tests + version-check).
- CapyLang: `make rust-validate` + `make validate`.
- CapyBrowser: `make validate` (código novo ainda não compilado — esperar
  correções de warning/golden).
- CapyAgent: `make validate` (inclui KAT do signer).
- CapyBenchmark: `make validate`.

## Pendências NÃO endereçadas nesta janela (fora de política/escopo)

- **Commit + tag + push** de cada repo (deploy externo; ver `DEPLOY-WORKSPACE.txt`).
- **Etapa 4 Fase F**: execução externa do gate `make smoke-x64-vmware-etapa-4`.
- **Registro do verifier Ed25519** via `capypkg_set_signature_verifier`
  (só na Etapa 9). O signer já está publicado host-side mas os `signed`
  repos seguem fail-closed até lá.
- **Data do release:** as versões usam o sufixo `+20260602`. Se o deploy
  ocorrer em outro dia, ajustar `+YYYYMMDD` de forma consistente em
  VERSION.yaml, `include/core/version.h`, README, nome da release note e
  tags.
