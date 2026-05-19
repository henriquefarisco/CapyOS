# Cross-repo compatibility audit — 2026-05-19

> **Atualização 2026-05-19 (alpha.240):** parte dos gaps P0-P2 listados
> neste documento foi endereçada na mesma data pelo slice
> `alpha.240` (install profile + auto-bootstrap + `make package` em
> cada repo externo + aggregator `make modules-index`). Os deltas
> estão registrados em `VERSION.yaml` (`history: 0.8.0-alpha.240+20260519`)
> e a sequência operacional ficou em
> [`../../operations/manual-module-deploy-runbook.md`](../../operations/manual-module-deploy-runbook.md).
> Esta auditoria continua válida como **snapshot histórico do estado
> em alpha.239** antes da extensão.

**Escopo:** Validação estática completa do adapter in-tree
`services/capypkg` (entrega antecipatória da Etapa 9) versus os 6
repositórios externos visíveis em `/Volumes/CapyOS/`: `CapyAgent`,
`CapyBrowser`, `CapyCodecs`, `CapyUI`, `CapyLang`, `CapyBenchmark`.

**Pergunta dirigente:** No deploy manual feito durante a instalação do
CapyOS core, os módulos remotos serão instalados corretamente via os
outros repositórios e a compatibilidade está coerente?

**Resposta executiva:** **Parcialmente sim, com gaps documentais
significativos.** O adapter in-tree é robusto, fail-closed e está
pronto para receber pacotes Capy remotos. Os repositórios externos
têm contratos versionados coerentes em alto nível, mas:

1. Nenhum repositório externo publica hoje um pacote no formato
   line-oriented `key=value` que o adapter consome.
2. Nenhum repositório externo tem build target para produzir o
   artefato `.bin` + manifest + assinatura Ed25519.
3. CapyAgent ainda não pluga o verificador Ed25519 via
   `capypkg_set_signature_verifier`, então repos `signed` permanecem
   fail-closed.
4. Há drift de versão entre `README.md` do CapyOS (`0.8.1`) e
   `VERSION.yaml` real (`0.8.0-alpha.239+20260519`).
5. Os exemplos de descriptor JSON em CapyAgent
   (`docs/component-index-example.md`) usam um esquema diferente do
   que o adapter consome, sem mapeamento documentado.

**Esta máquina é review/edit apenas.** Nenhum comando foi executado.
Gates externos recomendados ao final.

---

## 1. Inventário dos repositórios

| Repositório | VERSION | Tamanho do src | Tests | Estado de integração runtime |
|---|---|---|---|---|
| `CapyOS` | `0.8.0-alpha.239+20260519` (VERSION.yaml) | 516 itens | 251 itens | Core ativo na Etapa 3 |
| `CapyAgent` | `0.0.2` | 8 arquivos | 1 host test | Modelo host-testável; sem adapter para CapyOS |
| `CapyBrowser` | `0.0.2` | 3 arquivos (codec deprecated) | 1 host test | Sem implementação ativa de browser-core |
| `CapyCodecs` | `0.0.2` | 5 arquivos (image) | 16 arquivos test + goldens | Codecs BMP/PNG/JPEG host-testáveis |
| `CapyUI` | `0.6.0` | 6 arquivos (widget) | 1 host test ~43 KB | Modelo de widget mais maduro |
| `CapyLang` | `0.1.1` | 74 itens (Rust crates) | golden tests + 23 fixtures | Lexer slice S1 implementado |
| `CapyBenchmark` | `0.0.2` | 2 arquivos (harness) | 1 host test | Base mínima de relatório |

## 2. Drift de versão e doc

| Local | Valor declarado | Esperado | Severidade |
|---|---|---|---|
| `CapyOS/README.md:10` | `Versao de referencia: 0.8.1` | `0.8.0-alpha.239+20260519` | **alta** — usuário recebe versão falsa |
| `CapyOS/VERSION.yaml` | `0.8.0-alpha.239+20260519` | autoritativo | ok |
| `CapyAgent/docs/component-index-example.md` | placeholders `sha256: 0000…` | esperado ser placeholder | ok (declarado como exemplo) |
| `CapyAgent/docs/component-index-example.md` | `kind: codec`, `kind: browser-core` | match com `capy_component_kind` em `component_index.h` | ok |

## 3. Contrato runtime do adapter in-tree

Fonte da verdade: `CapyOS/include/services/capypkg.h` e
`CapyOS/docs/architecture/capypkg-adapter.md`.

### 3.1 Formato do manifest

Linha-oriented `key=value`. Múltiplas entradas separadas por `---\n`.
Mesma sintaxe é usada para:

- índice remoto (`https://<repo>/index.txt` ou equivalente);
- cache local persistido (`/system/capypkg/cache/index.txt`);
- DB de instalados (`/system/capypkg/db.idx`).

**Campos obrigatórios:**

- `name` — alfabeto `[a-zA-Z0-9._-]`, recusa nomes só-pontos;
- `version` — texto opaco;
- `payload_url` — HTTPS obrigatório;
- `payload_sha256` — exatamente 64 hex.

**Campos opcionais:**

- `summary` — printable ASCII;
- `payload_size` — decimal `uint32_t`, ≤ 8 MiB (`CAPYPKG_PAYLOAD_MAX`);
- `signature_ed25519` — exatamente 128 hex (obrigatório se o repo
  for `signed`);
- `install_root` — absoluto, sob `/var/capypkg` ou `/opt/`, sem `..`;
- `depends` — lista separada por vírgula com alfabeto do `name`;
- `repo` — preenchido pelo adapter; manifests devem deixar vazio.

Chaves desconhecidas são toleradas forward-compat. Bytes não-printable
ASCII (0x20-0x7E) em qualquer valor causam rejeição
`CAPYPKG_ERR_DENIED` (anti-injeção ANSI).

### 3.2 Descriptor canônico para assinatura

A assinatura Ed25519 cobre exatamente este byte string (sem espaços
adicionais, com `|` literal e `\n` final):

```text
name=<N>|version=<V>|payload_sha256=<H>|payload_url=<U>\n
```

Onde os quatro valores vêm verbatim do manifest. CapyAgent é dono do
lado de assinatura.

### 3.3 Lifecycle de install

1. `capypkg_fetch_index` baixa índice do(s) repositório(s)
   configurado(s) via HTTPS.
2. Cada entrada é parseada e validada (alfabeto, HTTPS, hex,
   `install_root`, sem `..`, sem ANSI escapes).
3. `capypkg_install(name)` resolve dependências recursivamente
   (budget 8), baixa payload, recomputa SHA-256, verifica assinatura
   se o repo exigir, grava em `<install_root>/<name>.bin`, atualiza
   `installed[]` e persiste `db.idx`.
4. Kernel **nunca** executa os bytes instalados; ativação é diferida
   para etapa futura com loader sandboxed.

### 3.4 Quotas

- `CAPYPKG_PAYLOAD_MAX` = 8 MiB (alpha runtime usa buffer estático
  de 1 MiB; payloads maiores aguardam streaming writer);
- `CAPYPKG_MAX_INSTALLED` = 64;
- `CAPYPKG_MAX_AVAILABLE` = 128;
- `CAPYPKG_MAX_REPOS` = 4;
- `CAPYPKG_MAX_DEPS` = 8 por pacote.

## 4. Gaps por repositório externo

### 4.1 CapyAgent — gaps críticos

| Gap | Evidência | Impacto |
|---|---|---|
| Descriptor JSON em `docs/component-index-example.md` usa esquema diferente do consumido pelo adapter | `id`, `tag`, `artifact`, `sha256` vs adapter espera `name`, `version`, `payload_url`, `payload_sha256` | Publisher que seguir só o exemplo CapyAgent produz índice ilegível |
| Sem serializer do formato line-oriented `key=value` em `src/` | Diretórios `package_format`, `component_index`, `update_core` só têm modelos C in-memory | Não há ferramenta executável que gere o manifest correto |
| Sem implementação Ed25519 nem ferramenta de assinatura | `grep -r ed25519` em `CapyAgent/src` retorna vazio | Repos `signed` permanecem inacessíveis até o verificador ser plugado e o signer publicado |
| Adapter para `capypkg_set_signature_verifier` não publicado | `docs/architecture/capypkg-adapter.md` § Future work item 1 ainda aberto | Repo `stable` default (`require_signature=1`) recusa toda instalação |
| Sem `make package` target | `Makefile` cobre só `test`/`lint`/`security`/`version-check` | Publisher precisa scriptar à mão |

### 4.2 CapyBrowser — gaps estruturais

| Gap | Evidência | Impacto |
|---|---|---|
| Sem implementação ativa de browser-core | `src/codecs/` contém só BMP snapshot deprecated marcado em README | Nada para empacotar como browser-core até Etapa 6 |
| `docs/compatibility.md` declara ABI `capy-browser-core` mas sem implementação | `compatibility.md:13` | Promessa futura, não verificável hoje |

### 4.3 CapyCodecs — readiness alta, gaps de publishing

| Gap | Evidência | Impacto |
|---|---|---|
| Codecs implementados e testados, mas sem build target para Capy package | `Makefile` produz só `test_image_contracts` binário | Publisher precisa empacotar manualmente |
| ABI version `CAPY_IMAGE_ABI_VERSION` declarada em header mas sem mapeamento documentado para `required_abis.minimum_version` | `docs/10-contracts/image-abi.md` | Risco de divergência futura |

### 4.4 CapyUI — readiness alta, gaps de publishing

| Gap | Evidência | Impacto |
|---|---|---|
| ABI mais madura (`0.6` since `display-list v2`) | `docs/compatibility.md:23-36` | OK |
| Sem build target para `.capypkg` artifact | `Makefile` produz só test binário | Publisher manual |
| Modelo retained-mode pronto para integração Etapa 4/6 | `tests/test_widget_contracts.c` 42 KB | OK |

### 4.5 CapyLang — gap de empacotamento

| Gap | Evidência | Impacto |
|---|---|---|
| Implementação em Rust (lexer S1) | `crates/capy-lexer/` com 23 goldens | Roadmap-blocked até Etapa 15 |
| Build target via cargo, sem `.capypkg` | `Makefile` chama `cargo test` | Publisher manual quando Etapa 15 ativar |

### 4.6 CapyBenchmark — base mínima

| Gap | Evidência | Impacto |
|---|---|---|
| Apenas modelo de relatório host-testável | `src/harness/` 2 arquivos | Coerente com roadmap (Etapas 15-16) |

## 5. Riscos de segurança identificados

| Risco | Mitigação atual | Mitigação faltante |
|---|---|---|
| Repo `unsigned` adicionado por usuário e enviando payload malicioso | SHA-256 obrigatório + alfabeto restrito + scope de filesystem | Documentar para o usuário que `--unsigned` desabilita a única defesa contra repo-side swap |
| `CapyAgent` ainda sem signer Ed25519; usuário pode ser tentado a usar `--unsigned` | Default `stable` é `signed=1` | Documentar caminho seguro para alpha |
| Mismatch entre descriptor JSON exemplo de `CapyAgent` e formato real consumido pelo adapter | Inexistente | Adicionar nota cruzada em ambos os lados + criar doc canônico de publisher |
| Versão errada no README do CapyOS (`0.8.1` vs real `0.8.0-alpha.239`) | Inexistente | Corrigir README |
| Ausência de matriz autoritativa de versão/ABI cross-repo | Contratos individuais cada um por seu lado | Criar `compatibility-matrix.md` |

## 6. Conformidade com contratos de integração

| Contrato em `docs/reference/integration/` | Status |
|---|---|
| `modular-installation-architecture.md` — Basic install boota sem componentes externos | **Conforme** — base bootstrap não depende de `capypkg`; `SYSTEM_SERVICE_CAPYPKG` é `STARTUP_MANUAL`, só no target `FULL` |
| `package-format-integration-contract.md` — adapter recebe Capy packages | **Conforme** — adapter ativo e fail-closed |
| `tag-release-component-index.md` — descritor com `id`, `tag`, `sha256` etc. | **Divergência** — o adapter usa formato diferente; contrato fala de "future installer/package adapter" mas o adapter já existe |
| `browser-core-integration-contract.md` | Roadmap-blocked (Etapa 6-7) |
| `media-codec-integration-contract.md` | Roadmap-blocked (Etapa 6-7) |
| `capyui-widget-integration-contract.md` | Roadmap-blocked (Etapa 4 e 6) |
| `capylang-integration-contract.md` | Roadmap-blocked (Etapa 15) |
| `benchmark-harness-integration-contract.md` | Roadmap-blocked (Etapa 15-16) |
| `core-migration-quarantine.md` | **Conforme** — flag `CAPYOS_ENABLE_LEGACY_MIGRATED` aposentada |
| `external-core-repositories.md` | **Conforme** — registro coerente |

## 7. Plano de validação externo recomendado

Esta máquina não executa. Quando o usuário fizer o deploy manual em
VMware + UEFI + E1000:

### 7.1 Antes do deploy (na máquina de build)

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

### 7.2 Durante o deploy (na VM oficial)

1. Boot ISO UEFI em VMware com NIC E1000.
2. Instalar para disco GPT via fluxo oficial.
3. Login com usuário desktop.
4. No `capysh`, verificar baseline:
   - `pkg-source-list` deve mostrar `stable` (require_signature=1);
   - `pkg-stats` (via `pkg-list` se disponível) deve mostrar
     `initialized=1`.
5. Para o deploy manual de módulos remotos:
   - Adicionar repositório de teste:
     `pkg-source-add testing https://<repo>/index.txt --unsigned`
   - Sincronizar: `pkg-fetch`
   - Listar: `pkg-list --available`
   - Instalar: `pkg-install <nome>`
   - Confirmar: `pkg-info <nome>` e arquivo em
     `/var/capypkg/<nome>/<nome>.bin`.

### 7.3 Smokes oficiais

```bash
make smoke-x64-iso TOOLCHAIN64=host
make smoke-x64-vmware-mouse-events    # gate da Etapa 2
make release-check                    # confidence release-level
```

### 7.4 Quando a Etapa 9 abrir oficialmente

```bash
make smoke-x64-vmware-pkg-install
```

## 8. Recomendações prioritárias

| Prioridade | Ação | Owner |
|---|---|---|
| **P0** | Corrigir versão no `CapyOS/README.md` (`0.8.1` → `0.8.0-alpha.239+20260519`) | CapyOS |
| **P0** | Publicar doc canônico do formato de manifest para publishers externos (`capypkg-publisher-manifest-format.md`) | CapyOS |
| **P0** | Publicar matriz de compatibilidade cross-repo (`compatibility-matrix.md`) com pinagem de versão e ABI | CapyOS |
| **P0** | Publicar runbook de deploy manual de módulos (`manual-module-deploy-runbook.md`) | CapyOS |
| **P1** | Adicionar em cada repo externo nota apontando para a matriz autoritativa e formato canônico | CapyAgent + outros |
| **P1** | Em `CapyAgent/docs/component-index-example.md`, marcar que JSON é registro de alto nível (CapyAgent) e o adapter consome formato line-oriented `key=value` | CapyAgent |
| **P2** | Publicar em CapyAgent um signer Ed25519 + serializer do manifest line-oriented | CapyAgent (futuro) |
| **P2** | Plugar verificador Ed25519 via `capypkg_set_signature_verifier` antes do primeiro `pkg-install` real | CapyAgent + CapyOS adapter |
| **P3** | Adicionar `make package` em cada repo externo quando a etapa correspondente abrir | repos externos |

## 9. Decisão sobre o deploy manual atual

**Pode prosseguir** com o deploy manual do CapyOS core na trilha
oficial `UEFI/GPT + x86_64` em `VMware + UEFI + E1000`. O sistema
boota a sessão gráfica completa e CLI sem depender de nenhum
componente externo.

**Para instalar módulos remotos hoje:**

- caminho `unsigned` funciona se o usuário adicionar um repositório
  HTTPS com `pkg-source-add ... --unsigned`. Os pacotes não serão
  ativados (sem loader sandboxed), apenas staged para
  `/var/capypkg/<nome>/`;
- caminho `signed` (default `stable`) **permanece fail-closed** até
  CapyAgent publicar e plugar o verificador Ed25519;
- nenhum dos repositórios externos hoje publica um pacote no formato
  correto, então o usuário precisará produzir o `manifest.txt` e o
  payload manualmente seguindo o doc
  `capypkg-publisher-manifest-format.md`.

**Para garantir compatibilidade total:**

- seguir o runbook `manual-module-deploy-runbook.md`;
- consultar a matriz `compatibility-matrix.md` para versões pinadas;
- aplicar o gate externo VMware no ambiente oficial.

## 10. Referência cruzada

- Adapter: `CapyOS/include/services/capypkg.h`,
  `CapyOS/src/services/capypkg/`
- Design rationale: `CapyOS/docs/architecture/capypkg-adapter.md`
- Matriz autoritativa: `CapyOS/docs/reference/integration/compatibility-matrix.md`
- Formato de manifest para publishers: `CapyOS/docs/reference/integration/capypkg-publisher-manifest-format.md`
- Runbook de deploy manual: `CapyOS/docs/operations/manual-module-deploy-runbook.md`
- Contratos por domínio: `CapyOS/docs/reference/integration/README.md`
