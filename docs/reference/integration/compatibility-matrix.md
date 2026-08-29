# Cross-repo compatibility matrix

**Status:** autoritativo; **revisao atual:** 2026-08-29, CapyOS
`0.9.2+20260826` (stable, Latest e imutavel), com CapyAI `0.2.1`, CapyUI `2.24.2` e os contratos
ABI inalterados. A ultima auditoria cross-repo completa permanece no snapshot
datado da `alpha.320` abaixo.
**Sincronização:** acompanha a versão do CapyOS core em `VERSION.yaml`.
**Atualizacao 0.9.2 (2026-08-26):** CapyOS
`0.9.2+20260826` preserva `capyos-base` v3, `capyos-package-apply` v1,
handoff interno v10 e todos os pins de repositorios irmaos. O patch adiciona o
gate A/B de producao sobre a ISO `0.9.1` publicada e assets Latest publicos,
sem chave privada/flags lab, com ordem rollback-then-confirm compativel com
anti-downgrade. Tambem endurece E1000/DHCP e o transporte do updater para o
runtime VMware real, sem alterar ABI nem contrato cross-repo. Os gates lab
A/B VMware e QEMU/OVMF passaram em quatro boots, o `release-check` oficial
passou, a release foi promovida como Latest imutavel e o gate VMware de
producao passou no run `d87affe8b077`. A Etapa 8 esta concluida; o KAT externo
do signer segue como condicao de integracao da Etapa 9.
**Atualizacao 0.9.1 (2026-08-25):** CapyOS
`0.9.1+20260825` preserva `capyos-base` v3, `capyos-package-apply` v1,
handoff interno v10 e todos os pins de repositorios irmaos. O patch automatiza
o gate VMware do instalador sem UART/COM1, endurece o wizard multi-disco e a
promocao assinada/imutavel; nao altera ABI nem contrato cross-repo. O estado e
de candidato ate os gates do mesmo artefato e a publicacao aceitarem.
**Atualizacao 0.9.0 (2026-08-21):** CapyOS
`0.9.0+20260821` promove o canal para stable mantendo `capyos-base` v3,
`capyos-package-apply` v1, handoff interno v10 e todos os pins de repos irmaos.
A mudanca consolida a trilha UEFI/GPT x86_64 com instalador seguro em VMs sem
COM1, deteccao serial fail-closed e publicacao com integridade SHA-256. Os
materiais Ed25519 e o `latest.ini` de produção permanecem pendentes; a Etapa 8
continua ativa.
Nao ha mudanca de ABI nem de contrato cross-repo.
**Atualizacao alpha.320 (2026-07-30):** CapyOS
`0.8.0-alpha.320+20260730` preserva `capyos-base` v3,
`capyos-package-apply` v1 e handoff interno v10. O pin CapyUI avança de `2.24.1`
para `2.24.2` como patch exclusivo de supply chain, sem alteração de
`capy-ui-widget` v2.22, desktop-session v1 ou schema 7; os demais pins externos
permanecem inalterados. A
regressao de boot da `alpha.319` foi causada pela red zone no loader UEFI; a
correcao usa uma pilha real sem red zone, valida/copia/compara o ELF e recarrega
a GDT. Dois ciclos KVM focados passaram (quatro boots com persistencia) e o smoke
oficial ISO KVM passou com instalacao, boot do disco e persistencia
(`marker:persist-ok`). O smoke CLI TCG tambem passou em 86,2 s, com dois boots e
persistencia. O smoke ISO TCG oficial, o update A/B e os gates VMware ainda nao
foram executados; a Etapa 8 continua aberta em 7/16. O indice imutavel da release
declara exatamente nove modulos; a verificacao remota de URL, tamanho e SHA-256
fica pendente ate a publicacao dos assets. Ver
[`compatibility-audit-2026-07-30-alpha320.md`](compatibility-audit-2026-07-30-alpha320.md).
**Atualizacao alpha.319 (2026-07-28):** CapyOS
`0.8.0-alpha.319+20260728` foi mantida como candidato **nao promovido**:
entregou o gate do ciclo A/B assinado, mas a primeira execucao real expos uma regressao de boot do
kernel (`#UD` antes do primeiro marker de `kernel_main64`), reproduzida tambem por
`make smoke-x64-cli` na build oficial. A Etapa 8 continua aberta; ver
[`compatibility-audit-2026-07-28-alpha319.md`](compatibility-audit-2026-07-28-alpha319.md).
Do ponto de vista de contrato, `0.8.0-alpha.319+20260728` preserva `capyos-base` v3, o handoff
interno v10 e todos os pins externos. O ciclo A/B assinado passa a ser provado
por gate automatizado no track oficial (`smoke-x64-vmware-update-ab`), com
preflight QEMU e contrato host dentro de `release-check`. A âncora de confiança
do manifesto ganha um override exclusivo de laboratório
(`CAPYOS_UPDATE_LAB_TRUST_KEY_HEX`), fail-closed e recusado por `iso-uefi` e
`release-check`; a chave de produção pinada em `update_agent_parse.c` permanece
inalterada e continua sendo a única aceita por qualquer artefato publicável.
Aditivo interno: `x64_storage_runtime_current_boot_attempt` e
`x64_storage_boot_provider_reason_label`. Nenhum contrato externo mudou.
**Atualizacao alpha.318 (2026-07-27):** CapyOS
`0.8.0-alpha.318+20260727` preserva `capyos-base` v3 e o handoff interno v10 sem
acrescentar campos. Habilita o apply A/B persistente: `boot_slot_store_arm`
publica exatamente uma tentativa durável vinculada a lease e à geração do
staging, o updater grava o payload já verificado no slot inativo, confirma saúde
pela geração do token de handoff e reporta o rollback aplicado pelo loader.
`-60` deixa de ser recusa incondicional e passa a significar transição
persistente não comprovável. A auditoria também corrige duas afirmações obsoletas
do snapshot `alpha.317`: o flush durável nativo existe (NVMe `FLUSH`, AHCI/ATA
`FLUSH CACHE`/`EXT`) e o loader já consome o slot-control. Nenhum contrato
externo ou pin de sibling mudou.
**Atualizacao alpha.317 (2026-07-20):** CapyOS
`0.8.0-alpha.317+20260720` preserva `capyos-base` v3 e introduz apenas o handoff
UEFI-kernel interno v9 append-only, com prefixo v8 congelado, identidade GPT
estrita/legada e fundação A/B ainda fail-closed. Nenhum contrato externo ou pin
de sibling mudou.
**Atualizacao alpha.316 (2026-07-20):** CapyOS
`0.8.0-alpha.316+20260720` integra CapyAI `0.2.1`; o `capy-ai-core` artifact v0
permanece ABI-compativel e passa a ser publicado com split sem leakage e gate
massivo de comando/risco. CapyUI `2.24.1`, CapyBrowser `0.6.7` e demais pins
permanecem inalterados.
**Atualizacao alpha.315 (2026-07-15):** CapyOS
`0.8.0-alpha.315+20260715`, CapyBrowser `0.6.7`, CapyUI `2.24.1` e
CapyAI `0.2.0` formam o conjunto pinado. O instalador exige alvo/ERASE e
preflight antes do wipe; first boot e desktop foram endurecidos; workers nao
herdam principal; o updater autenticado continua fail-closed para apply; as
ABIs `capy-ui-widget` 2.22, desktop-session v1 e `capy-ai-core` artifact v0
permanecem inalteradas.
**Última auditoria datada (`alpha.320`):** [`compatibility-audit-2026-07-30-alpha320.md`](compatibility-audit-2026-07-30-alpha320.md).
**Auditoria anterior (`alpha.319`):** [`compatibility-audit-2026-07-28-alpha319.md`](compatibility-audit-2026-07-28-alpha319.md).
**Auditoria anterior (`alpha.318`):** [`compatibility-audit-2026-07-27-alpha318.md`](compatibility-audit-2026-07-27-alpha318.md).
**Auditoria anterior (`alpha.317`):** [`compatibility-audit-2026-07-20-alpha317.md`](compatibility-audit-2026-07-20-alpha317.md). Corrigida pela auditoria `alpha.318` nos pontos de flush durável e consumo UEFI.
**Auditoria anterior (`alpha.316`):** [`compatibility-audit-2026-07-20.md`](compatibility-audit-2026-07-20.md).
**Auditoria anterior (`alpha.315`):** [`compatibility-audit-2026-07-15.md`](compatibility-audit-2026-07-15.md).
**Auditoria histórica `alpha.265`:** [`compatibility-audit-2026-06-11.md`](compatibility-audit-2026-06-11.md). Preservada apenas como snapshot historico.
**Auditoria anterior (snapshot histórico):** [`compatibility-audit-2026-05-23.md`](compatibility-audit-2026-05-23.md).

Esta matriz pina as versões mínimas que o CapyOS core suporta para cada
projeto desacoplado. Atualize esta tabela junto com qualquer release de
core que mude ABIs, contratos de manifest, política de assinatura ou
política de instalação modular.

## 1. Versões coordenadas

| Repositório | Versão atual local | ABI declarada | Versão mínima compatível com CapyOS core | Versão máxima testada |
|---|---|---|---|---|
| `CapyOS` | `0.9.2+20260826` | `capyos-base` v3 + `capyos-package-apply` v1 | — (autoritativo) | — |
| `CapyAgent` | `0.0.10` | `capy-agent-component-index` v1 (Ed25519 signer publicado host-side; verifier CapyOS-side registrado + KAT host-validado (alpha.276); trust anchor de producao publicado na CapyOS `0.9.1`; KAT externo do signer pendente; emit rejeita dependencia duplicada) | `0.0.10` | `0.0.10` |
| `CapyBrowser` | `0.6.7` | `capy-browser-core` v1 textual/grafico + `capy_page_render`; HTML/CSS/layout/display-list estaticos, limites e release gate reproduzivel | `0.6.7` | `0.6.7` |
| `CapyCodecs` | `0.0.12` | `capy-codec-image` v2 (`CAPY_IMAGE_ABI_VERSION=2`, aditiva sobre v1; +`capy_image_format_name`) | `0.0.12` (consumo build-time integrado; pacote remoto ainda gated) | `0.0.12` |
| `CapyUI` | `2.24.2` | `capy-ui-widget` v2.22 + `capy-ui-desktop-session` v1; logout/session/worker hardening, launcher/browser, chat CapyAI assincrono e release supply-chain fail-closed | `2.24.2` | `2.24.2` |
| `CapyAI` | `0.2.1` | `capy-ai-core` artifact v0; orquestrador governado, modelo fixed-point reproduzivel, split sem leakage, gate massivo de risco e pacote `org.capyos.ai.assistant` | `0.2.1` | `0.2.1` |
| `CapyLang` | `0.1.12` | `capy-lang-host` v0 (parcial: S1-S7 + S6.3 structs/enums; +opcodes de array 0x60-0x6A incl. push/pop/insert/remove, traps V0017-V0019, 43 opcodes congelados; metodos de array no frontend S10 (a.push/pop/insert/remove/get/set/len, E0022); host-only no sister) | n/a (roadmap-blocked) | n/a |
| `CapyBenchmark` | `0.0.11` | `capy-benchmark-report` v1 (planejada; serialização report/eval/replay + thresholds derivadas de baseline) | n/a (roadmap-blocked) | n/a |

> **Regra de pinagem:** "versão mínima compatível" só conta quando o
> repositório externo entrega contrato versionado, runner host, testes
> golden, limites declarados, modelo de erro e adaptador CapyOS na etapa
> correspondente. Repositórios marcados `n/a` não têm ABI ativa ainda
> e não devem ser instalados como módulo remoto em produção até a etapa
> abrir; instalações `--unsigned` em laboratório são permitidas para
> desenvolvimento mas nunca promovidas a release.

## 2. ABI naming (canonical)

Mantém alinhamento com [`modular-installation-architecture.md`](modular-installation-architecture.md)
e usa nomes de ABI em vez de nomes de repositório. Componentes
instaláveis devem declarar `required_abis` por nome.

| ABI name | Dono | Versão atual | Aceitação no adapter |
|---|---|---|---|
| `capyos-base` | CapyOS | v3 | implícito; sempre presente no runtime |
| `capyos-package-apply` | CapyOS | v1 | implícito; aplicação de pacote |
| `capy-agent-component-index` | CapyAgent | v1 | descritor de pacote; Ed25519 signer publicado host-side; verifier CapyOS-side registrado via `capypkg_set_signature_verifier` (alpha.276), fail-closed ate o trust anchor de producao |
| `capy-codec-image` | CapyCodecs | v2 | decodificação de imagem (aditiva sobre v1: per-call limits, detect/generic decode, metadata query, QOI) |
| `capy-browser-core` | CapyBrowser | v1 text subset publicado em CapyBrowser `0.6.0`; core grafico (display-list) consumido na Etapa 7 -- `0.6.6` faz o no IMAGE carregar o `src` resolvido (aditivo em v1, `CAPY_DL_VERSION` inalterado) | adapter CapyOS-side: app ring-3 `capybrowse` (texto) + pipeline/rasterizador/decode graficos (`browser_pipeline`/`browser_render_pixel`/`browser_image`) consumindo o display-list; build-validado (`make test`, `test-browser-pipeline` 19/19, `make all64` clean); runtime grafico via `smoke-x64-qemu-capygfx` (QEMU), gate VMware `smoke-x64-vmware-browser-graphical` mapeado |
| `capy-ui-widget` | CapyUI | v2.22 (display-list schema v7; v1.x LTS preservado no sister) | Etapa 4 consome `CapyUI/src/widget/capy_display_list.h` via adapter CapyOS-side; ops básicos 2D renderizam no core, ops sem provider (`IMAGE_REF`, transforms, plugins) ficam fail-safe/skip até providers dedicados |
| `capy-ui-desktop-session` | CapyUI | v1 (publicado em `alpha.241`) | aceita via capypkg `org.capyos.ui.desktop-session`; consultado pelo `kernel/module_gate.c` no boot |
| `capy-ai-core` | CapyAI | artifact v0 | core/modelo reproduzível consumido pelo seam `CAPYAI_DIR`; v0.2.1 preserva a ABI, elimina leakage do split e torna a campanha massiva um gate de release; pacote `org.capyos.ai.assistant`; adapters kernel/UI pertencem ao CapyOS/CapyUI |
| `capy-lang-host` | CapyLang + CapyOS | v0 (parcial: S1-S7 + S6.3 structs/enums; opcodes de array 0x60-0x6A, traps V0017-V0019 e 43 opcodes congelados; métodos de array no frontend S10; host-only no sister; host ABI de integração ainda planejada) | aceita só após Etapa 15 |
| `capy-benchmark-report` | CapyBenchmark | v1 (planejada) | aceita só após Etapa 15-16 |

Bumps de ABI devem ser aditivos até que a etapa permita uma migração
breaking explícita.

## 3. Política de release por repositório

| Repositório | Política de versionamento | Política de tag | Política de assinatura |
|---|---|---|---|
| `CapyOS` | `0.9.2+20260826` (stable extended) | `v<major>.<minor>.<patch>+<YYYYMMDD>` | release assinada, imutavel e aceita pelo gate completo |
| `CapyAgent` | semver `MAJOR.MINOR.PATCH` | `v<x>.<y>.<z>` | assinatura Ed25519 obrigatória no payload do adapter; **signer publicado host-side em `0.0.7` (`src/signer/`); verifier CapyOS-side registrado via `capypkg_set_signature_verifier` (alpha.276), KAT host-validado e trust anchor de producao publicado na CapyOS `0.9.1`; KAT externo do signer pendente** |
| `CapyBrowser` | semver `MAJOR.MINOR.PATCH` | `v<x>.<y>.<z>`; `v0.6.0` publica `org.capyos.browser.text` para Etapa 6 | assinatura obrigatoria quando o fluxo signed for promovido; laboratorio segue `--unsigned` |
| `CapyCodecs` | semver `MAJOR.MINOR.PATCH` | `v<x>.<y>.<z>` | será obrigatória quando entrar como pacote |
| `CapyUI` | semver `MAJOR.MINOR.PATCH` (versão 2.x ativa; absorveu desktop+window+apps em alpha.241; modules `widget-core` + `desktop-session`) | `v<x>.<y>.<z>` | será obrigatória quando entrar como pacote signed |
| `CapyAI` | semver `MAJOR.MINOR.PATCH`; modelo/pacote reproduzíveis | `v<x>.<y>.<z>` via workflow tag-triggered | assinatura obrigatória quando o pacote for promovido ao catálogo signed; laboratório não substitui trust anchor/KAT |
| `CapyLang` | semver `MAJOR.MINOR.PATCH` | `v<x>.<y>.<z>` | será obrigatória quando entrar como pacote |
| `CapyBenchmark` | semver `MAJOR.MINOR.PATCH` | `v<x>.<y>.<z>` | será obrigatória quando entrar como pacote |

## 4. Política de canal (`channel`)

| Canal | Uso | Quando usar |
|---|---|---|
| `stable` | release validado externamente | apenas quando o gate `release-check` aceita |
| `testing` | candidato a release | smokes oficiais aprovados |
| `experimental` | snapshot de feature | exclusivamente em VM de laboratório |
| `custom` | fonte do usuário | exige confirmação explícita; nunca default |

O repositório `stable` semeado por default no adapter
(`CAPYPKG_REPOS_FILE`) tem `require_signature=1`. Mudar isso é um
incidente; documente no `STATUS.md`.

## 5. Compatibilidade entre etapas e instalação modular

| Etapa | Componentes que podem ser instalados como módulo remoto | Componentes bloqueados |
|---|---|---|
| Etapa 3 (concluída em alpha.253) | apenas `org.capyos.ui.widget-core` e `org.capyos.ui.desktop-session` em `--unsigned` para validar o pipeline; nenhum em `signed` (verifier do CapyAgent ainda NULL) | demais |
| Etapa 4 (concluída em alpha.262) | mesmo escopo da Etapa 3 + adapter CapyOS-side para consumir `capy-ui-widget` v2.22 / display-list schema v7 do sister `CapyUI`; módulos remotos continuam em `--unsigned` durante o scaffolding | CapyCodecs (audio + image como módulo), CapyBrowser, CapyAgent assinado, CapyLang, CapyBenchmark |
| Etapas 5-7 (concluídas) | CapyUI widget/desktop, CapyCodecs image v2, CapyBrowser estático textual/gráfico e CapyAI `capy-ai-core` v0 / `org.capyos.ai.assistant` integrados por seams/adapters versionados | CapyLang, CapyBenchmark |
| Etapa 8 (concluída) | installer/update/release gate; `0.9.2` promovida como Latest imutável e ciclo A/B VMware público aprovado | CapyLang, CapyBenchmark |
| Etapa 9 (desbloqueada, próxima) | package manager + SDK + ABI estável; KAT externo do signer é condição desta integração | CapyLang, CapyBenchmark |
| Etapa 10 | CapyCodecs audio | CapyLang, CapyBenchmark |
| Etapa 15 | CapyLang VM e benchmarks | — |
| Etapa 16 | baseline CapyBenchmark | — |

Antes de qualquer etapa abrir, módulos só podem ser instalados em
laboratório com `--unsigned` e sem expectativa de continuidade.

## 6. Garantias do core para instalação modular

Independentemente da etapa, o adapter `capypkg` garante:

- HTTPS obrigatório no transporte;
- SHA-256 obrigatório por payload, validado pelo adapter;
- `install_root` restrito a `/var/capypkg` ou `/opt/`;
- alfabeto restrito `[a-zA-Z0-9._-]` em `name` e `depends`;
- rejeição de bytes não-printable em todos os campos do manifest;
- fail-closed em `signature_required` sem trust anchor de publisher pinado (verifier Ed25519 registrado em alpha.276);
- staging sem execução de bytes do payload;
- audit trail completo via `[audit] [capypkg]` no klog;
- quotas de pacote, instalado, disponível e repositório aplicadas;
- ativação consultada por `kernel/module_gate.c` via marker
  `/var/capypkg/<canonical-name>/installed`.

## 7. Garantias dos repositórios externos

Cada repositório externo declara seu próprio contrato em
`docs/compatibility.md` (autoritativo em todos os repos). Em
`CapyCodecs` o arquivo raiz `docs/compatibility.md` consolida o
contrato; `docs/10-contracts/`, `docs/20-validation/`,
`docs/30-roadmap/` e `docs/40-implementation/` mantêm a referência
técnica detalhada (image ABI, validation strategy, roadmap). Os
requisitos comuns:

- runner host ou biblioteca testável fora do CapyOS;
- golden tests sob `tests/`;
- limites de memória, tempo e tamanho de entrada documentados;
- modelo de erro determinístico;
- nenhuma chamada direta a syscalls ou estruturas internas do CapyOS;
- ABI declarada com nome canônico (`capy-*`);
- `make package` que gera `<name>.bin` + `<name>.manifest` aceitos pelo
  parser `services/capypkg` (line-oriented `key=value`).

## 8. Processo de upgrade da matriz

Quando uma destas mudanças ocorrer, atualize esta tabela junto:

1. release de qualquer repositório externo que afete o adapter ou
   um contrato de integração;
2. bump de ABI declarada em qualquer header público;
3. mudança de política de assinatura ou de canal;
4. abertura ou fechamento de Etapa que envolva instalação modular;
5. abertura de nova ABI canônica.

Use o release do CapyOS core como pivot e ancore a nova matriz na
versão correspondente do `VERSION.yaml`.

## 9. Referência cruzada

- [`compatibility-audit-2026-07-30-alpha320.md`](compatibility-audit-2026-07-30-alpha320.md) (ultima auditoria cross-repo completa; a revisao alpha.321 preserva ABIs e pins)
- [`compatibility-audit-2026-07-28-alpha319.md`](compatibility-audit-2026-07-28-alpha319.md) (historico; alpha.319 implementou o gate A/B, mas nao foi promovida)
- [`compatibility-audit-2026-07-27-alpha318.md`](compatibility-audit-2026-07-27-alpha318.md) (histórico; alpha.318 + apply A/B persistente)
- [`compatibility-audit-2026-07-20-alpha317.md`](compatibility-audit-2026-07-20-alpha317.md) (histórico; alpha.317 + fundação A/B/handoff v9 internos, pins externos inalterados)
- [`compatibility-audit-2026-07-20.md`](compatibility-audit-2026-07-20.md) (histórico; alpha.316 + CapyAI 0.2.1)
- [`compatibility-audit-2026-07-15.md`](compatibility-audit-2026-07-15.md) (histórico; alpha.315 + CapyUI 2.24.1 + CapyAI 0.2.0)
- [`compatibility-audit-2026-06-11.md`](compatibility-audit-2026-06-11.md) (histórico; alpha.265 + CapyBrowser 0.6.0)
- [`compatibility-audit-2026-06-06.md`](compatibility-audit-2026-06-06.md) (historico; alpha.263 + CapyBrowser 0.5.0; addendum alpha.264 = fecho Etapa 5 / abertura Etapa 6, pins sister inalterados)
- [`compatibility-audit-2026-06-02.md`](compatibility-audit-2026-06-02.md) (hist?rico; lote coordenado de 7 repos ? alpha.262 + CapyUI 2.22.0 + CapyCodecs 0.0.7/image v2 + CapyLang 0.1.8 + CapyBrowser 0.3.0 + CapyAgent 0.0.7/signer + CapyBenchmark 0.0.7)
- [`compatibility-audit-2026-05-23.md`](compatibility-audit-2026-05-23.md) (snapshot histórico; inclui addenda alpha.260 de 2026-05-25 e a sincronização cross-repo de 2026-05-29)
- [`compatibility-audit-2026-05-22.md`](compatibility-audit-2026-05-22.md) (snapshot histórico)
- [`compatibility-audit-2026-05-21.md`](compatibility-audit-2026-05-21.md) (snapshot histórico)
- [`compatibility-audit-2026-05-20.md`](compatibility-audit-2026-05-20.md) (snapshot histórico)
- [`compatibility-audit-2026-05-19.md`](compatibility-audit-2026-05-19.md) (snapshot histórico)
- [`modular-installation-architecture.md`](modular-installation-architecture.md)
- [`capypkg-publisher-manifest-format.md`](capypkg-publisher-manifest-format.md)
- [`../../operations/manual-module-deploy-runbook.md`](../../operations/manual-module-deploy-runbook.md)
- [`tag-release-component-index.md`](tag-release-component-index.md)
- [`package-format-integration-contract.md`](package-format-integration-contract.md)
- [`external-core-repositories.md`](external-core-repositories.md)
- [`../../architecture/capypkg-adapter.md`](../../architecture/capypkg-adapter.md)
- [`../../../STATUS.md`](../../plans/STATUS.md)
