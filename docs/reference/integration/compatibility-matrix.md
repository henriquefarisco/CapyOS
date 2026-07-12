# Cross-repo compatibility matrix

**Status:** autoritativo desde 2026-05-19; **atualizacao tecnica em 2026-07-07 (CapyOS core `alpha.309+20260702`; releases `alpha.308`/`alpha.309` = fixes de campo do desktop grafico, e o trabalho in-tree `alpha.310`/`alpha.311` de hardening do navegador grafico — NXE em BSP+APs, page tables proprias do kernel, fix do poll de eventos, window-gone->CLOSE, task_sleep degrade, scroll — sao TODOS ABI-neutros: nenhum contrato de sibling muda, nenhum bump de sibling; bump de VERSION.yaml para alpha.310/311 pendente):** pins dos siblings alinhados ao estado local atual — CapyUI `2.23.1` (2.23.1: preempt guard no frame do desktop, fix da tela azul VMware; 2.23.0: launcher com registro de apps + Navegador + Lista completa, terminal com exit fechando janela + scroll; 2.22.7: input bridge Slice 7.5), CapyAgent `0.0.10`, CapyBrowser `0.6.6`, CapyCodecs `0.0.12`, CapyLang `0.1.12`, CapyBenchmark `0.0.11`; Etapa 7 ativa (browser web estatica). Release `alpha.307` sem mudanca de ABI (fix do pager do shell grafico + enriquecimento do plano mestre). Historico tecnico anterior: 2026-06-17 (CapyOS core `alpha.266` — Etapa 6 ativa, Slice 6.4 adapter CapyOS implementado + build-validado (gate externo `smoke-x64-vmware-capybrowse-text` pendente); CapyBrowser `0.6.1`; demais pacotes consolidados em alpha.266: CapyUI `2.22.1` / `capy-ui-widget` v2.22, CapyCodecs `0.0.8` / `capy-codec-image` v2, CapyLang `0.1.9`, CapyAgent `0.0.8`, CapyBenchmark `0.0.9`). **Atualizacao alpha.293 (2026-06-17):** Etapa 7 / Slice 7.4 (decode de imagem inline, nucleo host-provado); CapyBrowser `0.6.1` -> `0.6.6` (o no IMAGE do display-list passa a carregar o `src` resolvido; aditivo em `capy-browser-core` v1, `CAPY_DL_VERSION` inalterado); `capy-codec-image` v2 consumido pelo adapter de decode CapyOS-side (`browser_image`); demais pins inalterados. Ver addendum em [`compatibility-audit-2026-06-11.md`](compatibility-audit-2026-06-11.md).
**Sincronização:** acompanha a versão do CapyOS core em `VERSION.yaml`.
**Atualizacao alpha.312 (2026-07-12):** CapyOS
`0.8.0-alpha.312+20260712`, CapyBrowser `0.6.7`, CapyUI `2.24.0` e
CapyAI `0.1.0` formam o conjunto pinado desta release. O navegador fecha o
escopo estatico (sem JavaScript); o desktop integra o chat CapyAI assincrono e
o indice agregado passa a ser publicado pelo CapyOS.
**Auditoria atual:** [`compatibility-audit-2026-06-11.md`](compatibility-audit-2026-06-11.md).
**Auditoria anterior (snapshot histórico):** [`compatibility-audit-2026-05-23.md`](compatibility-audit-2026-05-23.md).

Esta matriz pina as versões mínimas que o CapyOS core suporta para cada
projeto desacoplado. Atualize esta tabela junto com qualquer release de
core que mude ABIs, contratos de manifest, política de assinatura ou
política de instalação modular.

## 1. Versões coordenadas

| Repositório | Versão atual local | ABI declarada | Versão mínima compatível com CapyOS core | Versão máxima testada |
|---|---|---|---|---|
| `CapyOS` | `0.8.0-alpha.312+20260712` | `capyos-base` v3 + `capyos-package-apply` v1 | — (autoritativo) | — |
| `CapyAgent` | `0.0.10` | `capy-agent-component-index` v1 (Ed25519 signer publicado host-side; verifier CapyOS-side registrado + KAT host-validado (alpha.276), fail-closed ate o trust anchor de producao; KAT externo do signer pendente; emit rejeita dependencia duplicada) | `0.0.10` | `0.0.10` |
| `CapyBrowser` | `0.6.7` | `capy-browser-core` v1 textual/grafico + `capy_page_render`; HTML/CSS/layout/display-list estaticos, limites e release gate reproduzivel | `0.6.7` | `0.6.7` |
| `CapyCodecs` | `0.0.12` | `capy-codec-image` v2 (`CAPY_IMAGE_ABI_VERSION=2`, aditiva sobre v1; +`capy_image_format_name`) | `0.0.12` (host-only) | `0.0.12` |
| `CapyUI` | `2.24.0` | `capy-ui-widget` v2.22 + `capy-ui-desktop-session` v1; launcher/browser e chat CapyAI com submit/poll assincrono | `2.24.0` | `2.24.0` |
| `CapyAI` | `0.1.0` | `capy-ai-core` v0; modelo fixed-point reproduzivel e pacote `org.capyos.ai.assistant` | `0.1.0` | `0.1.0` |
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
| `capy-lang-host` | CapyLang + CapyOS | v0 (parcial: S1-S7 + S6.3 structs/enums; +opcodes 0x64-0x66 MakeAggregate/GetField/GetTag + trap V0018, 36->39 opcodes; host-only no sister; host ABI de integração ainda planejada) | aceita só após Etapa 15 |
| `capy-benchmark-report` | CapyBenchmark | v1 (planejada) | aceita só após Etapa 15-16 |

Bumps de ABI devem ser aditivos até que a etapa permita uma migração
breaking explícita.

## 3. Política de release por repositório

| Repositório | Política de versionamento | Política de tag | Política de assinatura |
|---|---|---|---|
| `CapyOS` | `0.8.0-alpha.309+20260702` (alpha extended) | `v<x>.<y>.<z>+<build>` | autoritativo; assina release quando o gate `release-check` aceitar |
| `CapyAgent` | semver `MAJOR.MINOR.PATCH` | `v<x>.<y>.<z>` | assinatura Ed25519 obrigatória no payload do adapter; **signer publicado host-side em `0.0.7` (`src/signer/`); verifier CapyOS-side registrado via `capypkg_set_signature_verifier` (alpha.276) e KAT host-validado, fail-closed ate o trust anchor de producao; KAT externo do signer pendente** |
| `CapyBrowser` | semver `MAJOR.MINOR.PATCH` | `v<x>.<y>.<z>`; `v0.6.0` publica `org.capyos.browser.text` para Etapa 6 | assinatura obrigatoria quando o fluxo signed for promovido; laboratorio segue `--unsigned` |
| `CapyCodecs` | semver `MAJOR.MINOR.PATCH` | `v<x>.<y>.<z>` | será obrigatória quando entrar como pacote |
| `CapyUI` | semver `MAJOR.MINOR.PATCH` (versão 2.x ativa; absorveu desktop+window+apps em alpha.241; modules `widget-core` + `desktop-session`) | `v<x>.<y>.<z>` | será obrigatória quando entrar como pacote signed |
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
| Etapas 5-7 | CapyUI widget v2.22 oficial + CapyCodecs image v2 (quando adapter GUI image abrir) + CapyBrowser text (`org.capyos.browser.text`, CapyBrowser `0.6.0`, adapter CapyOS implementado + build-validado, gate externo pendente) | CapyLang, CapyBenchmark |
| Etapas 8-9 | installer/update UX + package manager + SDK + ABI estável; CapyAgent vira producer oficial e pluga verifier Ed25519 | CapyLang, CapyBenchmark |
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

- [`compatibility-audit-2026-06-11.md`](compatibility-audit-2026-06-11.md) (atual; alpha.265 + CapyBrowser 0.6.0 handoff da Etapa 6 / Slice 6.4)
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
