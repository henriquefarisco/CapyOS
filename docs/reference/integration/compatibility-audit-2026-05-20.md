# Cross-repo compatibility audit — 2026-05-20

**Status:** snapshot técnico vigente após `alpha.244`.
**Snapshot anterior:** [`compatibility-audit-2026-05-19.md`](compatibility-audit-2026-05-19.md) (estado em `alpha.239`).
**Matriz autoritativa:** [`compatibility-matrix.md`](compatibility-matrix.md).

**Escopo:** Validação estática completa do adapter in-tree
`services/capypkg` e do `kernel/module_gate.c` versus os 6
repositórios externos visíveis em `/Volumes/CapyOS/`: `CapyAgent`,
`CapyBrowser`, `CapyCodecs`, `CapyUI`, `CapyLang`, `CapyBenchmark`.

**Pergunta dirigente:** após `alpha.244` (que entregou instalação
remota completa de módulos via GitHub Release), os 6 repositórios
apartados estão preparados para operar em paralelo ao CapyOS core
sem incompatibilidades ou quebras de integração?

**Resposta executiva:** **sim para `CapyUI` (com signer pendente);
sim para `CapyCodecs` em modo host-only; sim para `CapyLang` em modo
host-only roadmap-blocked; pendência crítica para `CapyAgent`
(signer Ed25519 ainda não publicado); base mínima ainda válida para
`CapyBrowser` e `CapyBenchmark`.**

Esta máquina é review/edit apenas. Nenhum comando foi executado.
Gates externos recomendados ao final.

---

## 1. Inventário atual

| Repositório | VERSION em `/Volumes/<repo>/VERSION` | Pinagem no `docs/compatibility.md` | Pinagem na matriz | Estado de integração runtime |
|---|---|---|---|---|
| `CapyOS` | `0.8.0-alpha.244+20260520` (VERSION.yaml) | — | — (autoritativo) | Core ativo na Etapa 3; sessão gráfica entregue como `org.capyos.ui.desktop-session` (CapyUI owner) |
| `CapyAgent` | `0.0.4` | `0.8.0-alpha.244+20260520` (alinhado por este audit) | `0.0.4` | Modelo host-testável; Ed25519 signer ainda não publicado |
| `CapyBrowser` | `0.0.4` | `0.8.0-alpha.244+20260520` (alinhado por este audit) | `0.0.4` (sem runtime) | Sem implementação ativa de browser-core; aguarda Etapas 6-7 |
| `CapyCodecs` | `0.0.4` | `0.8.0-alpha.244+20260520` (alinhado por este audit) | `0.0.4` (host-only) | BMP/PNG/JPEG host-testáveis; aguarda adapter `gui/codecs/` |
| `CapyUI` | `0.7.3` | `0.8.0-alpha.244+20260520` (alinhado por este audit) | `0.7.3` | Owner autoritativo de desktop session (`org.capyos.ui.desktop-session` v1) + widget core (`capy-ui-widget` v0.6); módulo capypkg publicado |
| `CapyLang` | `0.1.3` | `0.8.0-alpha.244+20260520` (alinhado por este audit) | `0.1.3` (S1) | Lexer S1 entregue; parser/bytecode/VM aguardam Etapa 15 |
| `CapyBenchmark` | `0.0.4` | `0.8.0-alpha.244+20260520` (alinhado por este audit) | `0.0.4` (sem runtime) | Base mínima de relatório; aguarda Etapas 15-16 |

## 2. Mudanças desde o audit de 2026-05-19 (`alpha.239`)

| Área | Estado anterior (`alpha.239`) | Estado atual (`alpha.244`) |
|---|---|---|
| Install profile em `/system/install/profile.ini` | inexistente | `alpha.240` introduziu schema `BASIC|FULL|CUSTOM` com `bootstrap_repo_*` (`install_profile.{c,h}`) |
| `pkg-bootstrap` shell command | inexistente | `alpha.240` registrou comando tri-língua em `capysh` (slot 38) |
| Auto-bootstrap em kernel poll | inexistente | `alpha.240` reduziu poll interval de `SYSTEM_SERVICE_CAPYPKG` para 60s e adicionou `kernel_capypkg_maybe_bootstrap()` |
| `make package` em cada repo apartado | apenas adapter publishing manual | `alpha.240` adicionou target `make package` em CapyAgent, CapyBenchmark, CapyBrowser, CapyCodecs, CapyLang, CapyUI |
| `tools/scripts/build_modules_index.py` | inexistente | `alpha.240` adicionou aggregator host que escaneia repos irmãos e gera `modules-index.txt` agregado |
| First-boot wizard | TUI básica | `alpha.241` adicionou step interativo `BASIC|FULL|CUSTOM` que escreve profile.ini + chama `capypkg_bootstrap_run_with_progress` com callback fbcon |
| Comando `capy` unificado | inexistente | `alpha.241` adicionou frontend único em `capysh` |
| Migração desktop session | in-tree sources | `alpha.241` migrou `gui/desktop/`, `gui/window/`, `apps/` para `CapyUI` como `org.capyos.ui.desktop-session` v1; CapyOS Makefile detecta `../CapyUI` sibling e cai para in-tree fallback quando ausente |
| `kernel/module_gate.c` | inexistente | `alpha.241` adicionou `kernel_module_desktop_session_available()` + `kernel_module_widget_core_available()` que consultam `/var/capypkg/<name>/installed`; `extended.c` wrappa `ensure_desktop` e `cmd_desktop_start` com gate + `#ifndef CAPYOS_PROFILE_CORE_ONLY` |
| HTTP redirect handling | URL+path 1024 bytes; payload rejeitado descartado | `alpha.242` subiu `HTTP_MAX_URL`/`HTTP_MAX_PATH` para 2048, moveu `req/next_url/resolved` para `kmalloc` e passou a manter payloads rejeitados em `/var/capypkg/updates` |
| HTTP redirect bodyless no bootstrap remoto | falhava em casos `/releases/latest/download/` | `alpha.243` corrigiu o fluxo; smokes warnings de all64/iso-uefi limpos; ISO valida instalação com persistência |
| Instalação remota end-to-end | quebrava em payload grande | `alpha.244` entregou download HTTPS de payload grande, staging dividido no CAPYFS, marker de ativação e smoke ISO com desktop ativado no reboot |

## 3. Contrato runtime do adapter (vigente em `alpha.244`)

Fonte da verdade: `CapyOS/include/services/capypkg.h` e
`CapyOS/docs/architecture/capypkg-adapter.md`. Sem mudanças
contratuais quebrantes desde o audit anterior; formato do manifest
permanece line-oriented `key=value` com separador `---` e descritor
canônico Ed25519 `name=N|version=V|payload_sha256=H|payload_url=U\n`.

Mudanças incrementais (todas backward-compatible):

- Buffers HTTP elevados de 1024 → 2048 bytes (`alpha.242`).
- Payload máximo confiável continua 1 MiB no buffer estático alpha;
  `CAPYPKG_PAYLOAD_MAX = 8 MiB` continua sendo o teto definido,
  alcançável quando o streaming writer entrar.
- Payloads rejeitados durante install são preservados em
  `/var/capypkg/updates/` para diagnóstico forense (`alpha.242`).

Política de segurança vigente continua exatamente a mesma:
HTTPS-only, SHA-256 obrigatório, signature gate fail-closed (verifier
Ed25519 ainda NULL no kernel binder), `install_root` restrito a
`/var/capypkg` ou `/opt/`, alfabeto restrito `[a-zA-Z0-9._-]`,
rejeição de bytes não-printable, zero execução de payload.

## 4. Estado dos repositórios externos

### 4.1 CapyAgent — bloqueador único: Ed25519 signer

| Aspecto | Status |
|---|---|
| Versão | `0.0.4` (alinhada com a matriz) |
| ABI declarada | `capy-agent-component-index` v1 |
| Contratos documentais | `compatibility.md`, `capypkg-publisher-guide.md`, `tag-release-index.md`, `component-index-example.md`, `package-format-migration.md` — completos |
| Mapeamento JSON → manifest line-oriented | documentado em `capypkg-publisher-manifest-format.md §10` |
| `make package` target | presente desde `alpha.240` |
| Signer Ed25519 publicado | **NÃO** (verifier slot continua NULL no kernel binder em `src/arch/x86_64/kernel_services.c::kernel_capypkg_bind_runtime_adapters`) |
| Impacto runtime | repos `signed` (default `stable`) rejeitam toda instalação com `CAPYPKG_ERR_SIGNATURE`; apenas `--unsigned` em laboratório funciona |

**Bloqueador:** sem o signer Ed25519, nenhuma instalação remota
chega à produção. O caminho de install via `org.capyos.ui.desktop-session`
no first-boot wizard só funciona hoje porque o repo bootstrap pode
ser declarado como `--unsigned` no `profile.ini`. Para promover a
release oficial, o signer precisa entrar.

### 4.2 CapyBrowser — base mínima coerente, sem runtime ativo

| Aspecto | Status |
|---|---|
| Versão | `0.0.4` (alinhada com a matriz) |
| ABI declarada | `capy-browser-core` v1 (planejada) |
| Contratos documentais | `compatibility.md`, `capyos-migration.md` — coerentes; sem display-list spec ainda |
| `make package` target | presente desde `alpha.240` |
| Runtime ativo | nenhum — codecs deprecated migrados para CapyCodecs |
| Próximo passo | URL parser + HTML-to-text + display-list fixtures (Etapas 6-7) |

Sem incompatibilidade detectada. Aguarda Etapa 6 abrir.

### 4.3 CapyCodecs — readiness alta para o adapter futuro

| Aspecto | Status |
|---|---|
| Versão | `0.0.4` (alinhada com a matriz) |
| ABI declarada | `capy-codec-image` v1 (`CAPY_IMAGE_ABI_VERSION`) |
| Contratos documentais | `docs/compatibility.md` (autoritativo a partir de 2026-05-20), `docs/10-contracts/compatibility.md` (mirror histórico), `docs/10-contracts/image-abi.md`, `docs/20-validation/validation.md`, `docs/00-overview/project-boundary.md`, `docs/30-roadmap/`, `docs/40-implementation/` — organização hierárquica completa |
| `make package` target | presente desde `alpha.240` |
| Runtime ativo | BMP/PNG/JPEG host-testáveis; sem adapter `gui/codecs/` em CapyOS ainda |
| Bloqueio | aguarda Etapa 6-7 abrir adapter `gui/codecs/` em CapyOS |

Sem incompatibilidade detectada. Pronto para integração quando a etapa abrir.

### 4.4 CapyUI — owner autoritativo da desktop session

| Aspecto | Status |
|---|---|
| Versão | `0.7.3` (alinhada com a matriz) |
| ABIs declaradas | `capy-ui-widget` v0.6 + `capy-ui-desktop-session` v1 |
| Contratos documentais | `compatibility.md`, `roadmap/contracts/abi-versions.md` + 18 contratos versionados por área, `roadmap/STATUS.md`, `roadmap/README.md` — comprehensivos |
| `make package` target | presente; publica `org.capyos.ui.widget-core.bin/.manifest` e `org.capyos.ui.desktop-session.bin/.manifest` + `modules-index.txt` |
| Runtime ativo | desktop session migrada em `alpha.241`; CapyOS Makefile detecta `../CapyUI` sibling e compila de lá; in-tree fallback continua funcional |
| Workflow CI | `.github/workflows/release-artifacts.yml` re-publica rolling `latest` tag com `.bin/.manifest/modules-index.txt` em cada push para `main` |
| URL no first-boot wizard | `CAPYOS_DEFAULT_MODULES_INDEX_URL` aponta para `github.com/<owner>/CapyUI/releases/download/v0.7.3/modules-index.txt` |

Sem incompatibilidade detectada. **CapyUI é hoje o único repositório
externo com integração runtime ativa** via `org.capyos.ui.desktop-session`.

### 4.5 CapyLang — implementação Rust progredindo no roadmap

| Aspecto | Status |
|---|---|
| Versão | `0.1.3` (alinhada com a matriz) |
| ABI declarada | `capy-lang-host` v0 parcial (apenas S1 lexer) |
| Contratos documentais | `compatibility.md`, `integration.md`, `bytecode-v0.md`, `lexer.md` — bytecode header frozen por S4 mas body opcodes ainda drafted |
| `make package` target | presente desde `alpha.240` (gera `org.capyos.lang.runtime`-style assets quando aplicável) |
| Runtime ativo | apenas lexer (crate `capy-lexer`); CLI `capyc-tokens` para debugging |
| Bloqueio | aguarda parser (S2), bytecode body (S4), VM (S6-S9), stdlib (S10), host ABI (S11), CLI (S12) |

Sem incompatibilidade detectada. Roadmap-blocked corretamente.

### 4.6 CapyBenchmark — base mínima, sem runtime

| Aspecto | Status |
|---|---|
| Versão | `0.0.4` (alinhada com a matriz) |
| ABI declarada | `capy-benchmark-report` v1 (planejada) |
| Contratos documentais | `compatibility.md` — mínimo, sem detalhes operacionais |
| `make package` target | presente desde `alpha.240` |
| Runtime ativo | apenas modelo de relatório host-testável |
| Bloqueio | aguarda CapyLang VM (Etapa 15) para workloads reais e Etapa 16 para baseline regressiva |

Sem incompatibilidade detectada. Coerente com o roadmap.

## 5. Cobertura do kernel/system contra integrações

| Subsistema do kernel | Caller-side relevante | Status |
|---|---|---|
| `services/capypkg` (4 TUs + 1 header público) | CapyAgent (publisher), capysh (`pkg-*`), wizard de first-boot, kernel auto-bootstrap | **Ativo** com 28 testes host-side passando; verifier Ed25519 NULL fail-closed |
| `services/capypkg_bootstrap` (`alpha.240`) | wizard, `pkg-bootstrap`, `kernel_capypkg_maybe_bootstrap()` | **Ativo**, eventos progressivos `REPO_REGISTER`/`INDEX_FETCH`/`PACKAGE_BEGIN/OK/FAIL/SKIP`/`SWEEP_DONE` |
| `services/install_profile` (`alpha.240`) | first-boot wizard, capypkg_bootstrap | **Ativo**, schema `BASIC|FULL|CUSTOM` line-oriented `key=value` |
| `kernel/module_gate` (`alpha.241`) | desktop activation (CapyUI consumer), `extended.c::ensure_desktop`/`cmd_desktop_start` | **Ativo**, fail-closed por `CAPYOS_PROFILE_CORE_ONLY` ou marker ausente |
| `net/services/http` | capypkg fetcher, update_agent fetcher | **Ativo**, redirect HTTP até 5 hops, `HTTP_MAX_URL=2048`, alocação heap, payload preservation |
| `security/tls` (BearSSL) | HTTP/HTTPS, trust anchors em `tls_trust_anchors.c` | **Ativo**, handshake real entregue até Etapa 2; Etapa 5 polish |
| `security/volume_provider` | install path + boot path do volume cifrado | **Ativo**, Argon2id + header `CAPYVHDR`, downgrade protection, fallback legacy preservado |
| `fs/capyfs` + `fs/vfs` | capypkg staging em `/var/capypkg/`, `/system/install/`, `/system/capypkg/` | **Ativo**, journaling, encryption por volume_provider |

## 6. Riscos vigentes

| Risco | Mitigação atual | Mitigação faltante |
|---|---|---|
| Repo `unsigned` adicionado por usuário e enviando payload malicioso | SHA-256 obrigatório + alfabeto restrito + scope de filesystem + zero execução | Documentar para o usuário que `--unsigned` desabilita a única defesa contra repo-side swap |
| `CapyAgent` ainda sem signer Ed25519 | Default `stable` é `signed=1` (rejeita install) | Publicar signer e plugar verifier antes de promover repo público para usuário final |
| Mismatch entre descriptor JSON exemplo de `CapyAgent` e formato real consumido pelo adapter | Mapeamento documentado em `capypkg-publisher-manifest-format.md §10` e nota cruzada em `CapyAgent/docs/capypkg-publisher-guide.md` | Implementar serializer canônico em CapyAgent que converte modelo high-level → manifest line-oriented |
| Payload > 1 MiB rejeitado mesmo abaixo do `CAPYPKG_PAYLOAD_MAX` | Buffer estático alpha + log warn | Implementar streaming writer para chegar ao limite real de 8 MiB |
| First-boot wizard depende de rede para baixar índice | Auto-bootstrap em poll com retry; arquivo marker `bootstrap.done` só é gravado em BASIC ou sweep completo sem falhas e com escrita de marker concluída | Manter UX de fallback claro para usuário em VM/instalação offline |

## 7. Conformidade com contratos de integração

| Contrato em `docs/reference/integration/` | Status |
|---|---|
| `modular-installation-architecture.md` — Basic install boota sem componentes externos | **Conforme** — base bootstrap não depende de `capypkg`; `SYSTEM_SERVICE_CAPYPKG` é `STARTUP_MANUAL`, só no target `FULL`; `PROFILE=core-only` produz kernel sem desktop session |
| `package-format-integration-contract.md` — adapter recebe Capy packages | **Conforme** — adapter ativo, recebe `.capypkg` via `capypkg_install`/`capypkg_fetch_index`; staging preservado |
| `capypkg-publisher-manifest-format.md` — formato canônico | **Conforme** — todos os 6 repos podem produzir manifests no formato esperado via `make package` |
| `tag-release-component-index.md` — descritor com `id`, `tag`, `sha256`, `required_abis` | **Conforme** — JSON high-level documentado; mapeamento para line-oriented em §10 do publisher manifest format |
| `browser-core-integration-contract.md` | Roadmap-blocked (Etapa 6-7) |
| `media-codec-integration-contract.md` | Roadmap-blocked (Etapa 6-7 imagem; Etapa 10 áudio) |
| `capyui-widget-integration-contract.md` | Roadmap-blocked oficialmente (Etapas 4 e 6) — mas `org.capyos.ui.desktop-session` v1 já instalável via capypkg em modo alpha |
| `capylang-integration-contract.md` | Roadmap-blocked (Etapa 15) |
| `benchmark-harness-integration-contract.md` | Roadmap-blocked (Etapas 15-16) |
| `core-migration-quarantine.md` | **Conforme** — flag `CAPYOS_ENABLE_LEGACY_MIGRATED` aposentada |
| `external-core-repositories.md` | **Conforme** — registro coerente e atualizado em 2026-05-20 |

## 8. Plano de validação externo recomendado

Esta máquina não executa. Quando o usuário fizer o deploy manual em
VMware + UEFI + E1000:

### 8.1 Antes do deploy (na máquina de build)

```bash
# Sanidade host-side do core
make test
make test-capypkg
make layout-audit
make version-audit

# Build oficial UEFI + ISO
make all64
make iso-uefi
make verify-release-checksums TOOLCHAIN64=host
```

### 8.2 Para cada repo apartado (em paralelo)

```bash
cd CapyAgent && make validate && make package
cd CapyUI && make validate && make package
cd CapyCodecs && make validate && make package
cd CapyBrowser && make validate && make package
cd CapyLang && make validate
cd CapyBenchmark && make validate && make package
```

### 8.3 Aggregator (na máquina de build do CapyOS)

```bash
make modules-index
```

### 8.4 Durante o deploy (na VM oficial)

1. Boot ISO UEFI em VMware com NIC E1000.
2. Wizard de first-boot escolhe profile `FULL` (ou `CUSTOM` para
   especificar quais módulos instalar).
3. Wizard escreve `/system/install/profile.ini` e chama
   `capypkg_bootstrap_run_with_progress` que baixa o `modules-index.txt`
   agregado, valida, instala cada módulo e marca em
   `/var/capypkg/<name>/installed`.
4. Acpi reboot.
5. No próximo boot, `kernel_module_desktop_session_available()` retorna
   1 → desktop session ativada → login GUI sobe normalmente.

### 8.5 Smokes oficiais

```bash
make smoke-x64-iso TOOLCHAIN64=host
make smoke-x64-vmware-mouse-events    # gate da Etapa 2
make release-check                    # confidence release-level
```

### 8.6 Quando Etapas futuras abrirem

```bash
# Etapa 9 (package manager + Ed25519 signer)
make smoke-x64-vmware-pkg-install

# Etapa 4 (CapyDisplay 2D + widget model integration)
make smoke-x64-vmware-compositor-damage-track

# Etapa 6 (apps básicos + CapyBrowse Text + CapyCodecs imagem)
make smoke-x64-vmware-apps-basic-roundtrip
make smoke-x64-vmware-capybrowse-text
```

## 9. Recomendações prioritárias

| Prioridade | Ação | Owner | Bloqueador para |
|---|---|---|---|
| **P0** | Publicar Ed25519 signer em CapyAgent e plugar verifier via `capypkg_set_signature_verifier` no kernel binder | CapyAgent | Etapa 9 oficial; promoção de release público para usuário final |
| **P1** | Implementar streaming writer em `services/capypkg` para chegar ao `CAPYPKG_PAYLOAD_MAX = 8 MiB` real (hoje buffer estático limita a 1 MiB) | CapyOS core | Pacotes maiores que 1 MiB |
| **P1** | Atualizar `capypkg-publisher-manifest-format.md §10` se CapyAgent introduzir serializer canônico JSON → line-oriented | CapyAgent + CapyOS | Workflow do publisher |
| **P2** | Finalizar bytecode body em `CapyLang/docs/bytecode-v0.md` antes de Etapa 15 abrir | CapyLang | Etapa 15 integration |
| **P2** | Definir display-list format em `CapyBrowser/docs/compatibility.md` antes de Etapa 6-7 abrir | CapyBrowser | Etapas 6-7 integration |
| **P3** | Expandir documentação de CapyBenchmark e CapyBrowser para nível hierárquico (CapyCodecs/CapyUI já têm) | CapyBenchmark + CapyBrowser | UX para contribuidores externos |
| **P3** | Adicionar contrato de erro determinístico padronizado em todos os contratos de integração (já existe no `capypkg-publisher-manifest-format.md`) | Cada repo | Paralelismo seguro |

## 10. Decisão sobre o deploy manual atual

**Pode prosseguir** com o deploy manual do CapyOS core na trilha
oficial `UEFI/GPT + x86_64` em `VMware + UEFI + E1000` no canal
`alpha`. O sistema:

- Boota a sessão gráfica completa quando o módulo CapyUI
  `org.capyos.ui.desktop-session` é instalado via wizard de first-boot
  (canal `--unsigned` em alpha).
- Cai para shell textual fail-closed quando o módulo não está
  instalado (gate ativado por `kernel/module_gate.c`).
- Permanece operacional para CLI/services mesmo sem nenhum módulo
  externo (Basic profile).

**Para promover a release pública para usuário final:**

- caminho `unsigned` funciona em laboratório mas **não pode** ser
  default público; mantenha `signed=1` no `stable`;
- publicar Ed25519 signer em CapyAgent é P0 absoluto antes de
  qualquer release pública oficial;
- após o signer plugado, todos os 6 repos podem republicar seus
  artefatos em `signed` mode e a chain of trust fica completa.

## 11. Referência cruzada

- Adapter: `CapyOS/include/services/capypkg.h`, `CapyOS/src/services/capypkg/`
- Module gate: `CapyOS/include/kernel/module_gate.h`, `CapyOS/src/kernel/module_gate.c`
- Bootstrap: `CapyOS/include/services/capypkg_bootstrap.h`, `CapyOS/src/services/capypkg_bootstrap.c`
- Install profile: `CapyOS/include/services/install_profile.h`, `CapyOS/src/services/install_profile.c`
- Design rationale: `CapyOS/docs/architecture/capypkg-adapter.md`
- Decoupling policy: `CapyOS/docs/architecture/decoupled-development-contracts.md`
- Matriz autoritativa: `CapyOS/docs/reference/integration/compatibility-matrix.md`
- Formato de manifest para publishers: `CapyOS/docs/reference/integration/capypkg-publisher-manifest-format.md`
- Runbook de deploy manual: `CapyOS/docs/operations/manual-module-deploy-runbook.md`
- Contratos por domínio: `CapyOS/docs/reference/integration/README.md`
- Plano mestre: `CapyOS/docs/plans/active/capyos-master-plan.md`
- Status executivo: `CapyOS/docs/plans/STATUS.md`
