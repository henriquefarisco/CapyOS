# Cross-repo compatibility matrix

**Status:** autoritativo desde 2026-05-19.
**Atualização:** acompanha a versão do CapyOS core em `VERSION.yaml`.
**Auditoria de origem:** [`compatibility-audit-2026-05-19.md`](compatibility-audit-2026-05-19.md).

Esta matriz pina as versões mínimas que o CapyOS core suporta para cada
projeto desacoplado. Atualize esta tabela junto com qualquer release de
core que mude ABIs, contratos de manifest, política de assinatura ou
política de instalação modular.

## 1. Versões coordenadas

| Repositório | Versão atual local | ABI declarada | Versão mínima compatível com CapyOS core | Versão máxima testada |
|---|---|---|---|---|
| `CapyOS` | `0.8.0-alpha.241+20260519` | `capyos-base` v3 + `capyos-package-apply` v1 | — (autoritativo) | — |
| `CapyAgent` | `0.0.3` | `capy-agent-component-index` v1 | `0.0.3` | `0.0.3` |
| `CapyBrowser` | `0.0.3` | `capy-browser-core` v1 (planejada) | n/a (sem runtime ativo) | n/a |
| `CapyCodecs` | `0.0.3` | `capy-codec-image` v1 (`CAPY_IMAGE_ABI_VERSION`) | `0.0.2` (host-only) | `0.0.2` |
| `CapyUI` | `0.7.0` | `capy-ui-widget` v0.6 + `capy-ui-desktop-session` v1 | `0.7.0` (cross-repo build) | `0.7.0` |
| `CapyLang` | `0.1.2` | `capy-lang-host` v0 (parcial, S1 lexer) | n/a (roadmap-blocked) | n/a |
| `CapyBenchmark` | `0.0.3` | `capy-benchmark-report` v1 (planejada) | n/a (roadmap-blocked) | n/a |

> **Regra de pinagem:** "versão mínima compatível" só conta quando o
> repositório externo entrega contrato versionado, runner host, testes
> golden, limites declarados, modelo de erro e adaptador CapyOS na etapa
> correspondente. Repositórios marcados `n/a` não têm ABI ativa ainda
> e não devem ser instalados como módulo remoto até a etapa abrir.

## 2. ABI naming (canonical)

Mantém alinhamento com `modular-installation-architecture.md` e usa
nomes de ABI em vez de nomes de repositório. Componentes instaláveis
devem declarar `required_abis` por nome.

| ABI name | Dono | Versão atual | Aceitação no adapter |
|---|---|---|---|
| `capyos-base` | CapyOS | v3 | implícito; sempre presente no runtime |
| `capyos-package-apply` | CapyOS | v1 | implícito; aplicação de pacote |
| `capy-agent-component-index` | CapyAgent | v1 | descritor de pacote |
| `capy-codec-image` | CapyCodecs | v1 | decodificação de imagem |
| `capy-browser-core` | CapyBrowser | v1 (planejada) | aceita só após Etapa 6 |
| `capy-ui-widget` | CapyUI | v0.6 | aceita só após Etapa 4/6 |
| `capy-ui-desktop-session` | CapyUI | v1 (alpha.241) | aceita via capypkg `org.capyos.ui.desktop-session` |
| `capy-lang-host` | CapyLang + CapyOS | v0 (planejada) | aceita só após Etapa 15 |
| `capy-benchmark-report` | CapyBenchmark | v1 (planejada) | aceita só após Etapa 15-16 |

Bumps de ABI devem ser aditivos até que a etapa permita uma migração
breaking explícita.

## 3. Política de release por repositório

| Repositório | Política de versionamento | Política de tag | Política de assinatura |
|---|---|---|---|
| `CapyOS` | semver alpha (`0.MAJOR.MINOR-alpha.NUM+YYYYMMDD`) | `v<x>.<y>.<z>` | manifest assinado por Ed25519 (autoridade base) |
| `CapyAgent` | semver `MAJOR.MINOR.PATCH` | `v<x>.<y>.<z>` | assinatura Ed25519 obrigatória no payload do adapter; **signer ainda não publicado** |
| `CapyBrowser` | semver `MAJOR.MINOR.PATCH` | `v<x>.<y>.<z>` | será obrigatória quando entrar como pacote |
| `CapyCodecs` | semver `MAJOR.MINOR.PATCH` | `v<x>.<y>.<z>` | será obrigatória quando entrar como pacote |
| `CapyUI` | semver `MAJOR.MINOR.PATCH` (versão 0.7 ativa; absorveu desktop+window+apps em alpha.241) | `v<x>.<y>.<z>` | será obrigatória quando entrar como pacote |
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
| Etapa 3 (atual) | nenhum oficial; apenas adapter `capypkg` em modo recebedor para testes | todos |
| Etapas 4-7 | CapyUI widget model + CapyCodecs image (quando adapter GUI image abrir) + CapyBrowser text | CapyLang, CapyBenchmark |
| Etapas 8-9 | installer/update UX + package manager + SDK + ABI estável; CapyAgent vira producer oficial | CapyLang, CapyBenchmark |
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
- fail-closed em `signature_required` sem verifier plugado;
- staging sem execução de bytes do payload;
- audit trail completo via `[audit] [capypkg]` no klog;
- quotas de pacote, instalado, disponível e repositório aplicadas.

## 7. Garantias dos repositórios externos

Cada repositório externo declara seu próprio contrato em
`docs/compatibility.md`. Os requisitos comuns:

- runner host ou biblioteca testável fora do CapyOS;
- golden tests sob `tests/`;
- limites de memória, tempo e tamanho de entrada documentados;
- modelo de erro determinístico;
- nenhuma chamada direta a syscalls ou estruturas internas do CapyOS;
- ABI declarada com nome canônico (`capy-*`).

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

- [`compatibility-audit-2026-05-19.md`](compatibility-audit-2026-05-19.md)
- [`modular-installation-architecture.md`](modular-installation-architecture.md)
- [`capypkg-publisher-manifest-format.md`](capypkg-publisher-manifest-format.md)
- [`../../operations/manual-module-deploy-runbook.md`](../../operations/manual-module-deploy-runbook.md)
- [`tag-release-component-index.md`](tag-release-component-index.md)
- [`package-format-integration-contract.md`](package-format-integration-contract.md)
- [`external-core-repositories.md`](external-core-repositories.md)
- [`../../architecture/capypkg-adapter.md`](../../architecture/capypkg-adapter.md)
