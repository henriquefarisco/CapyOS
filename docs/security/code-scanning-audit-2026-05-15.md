# Code scanning audit — 2026-05-15

**Status:** concluído (categorias atacáveis em self-audit). Aplicado em
2026-05-15 após sinalização do usuário de ~40 alertas no painel de
*Security & Quality → Code scanning* do GitHub para o repositório
`henriquefarisco/CapyOS`.

**Escopo:** este documento captura o **self-audit local** executado por
um agente de pair programming sem acesso à API do GitHub. O cruzamento
fino com a lista oficial de 40 alertas exigirá um próximo passo onde
o usuário ou CI exporte o SARIF.

## 1. Configuração do scanner

O repositório executa CodeQL Advanced em
`.github/workflows/codeql.yml` com:

- Linguagens: `c-cpp` (autobuild) + `python`.
- Suite: **default queries** (a suite `security-and-quality` está
  comentada — `# queries: security-extended,security-and-quality`).
- Trigger: `push` em `main`/`develop`, `pull_request` para essas
  branches, e cron semanal (cron `22 4 * * 4`).

Também há `security-hardening.yml` rodando OSSF Scorecard, mas seus
alertas tipicamente aparecem em outra aba (Supply chain).

## 2. Metodologia do self-audit

Aplicada porque a policy de execução local impede acesso à API do
GitHub. O usuário escolheu **self-audit estático** em vez de aguardar
exportação SARIF, com a ressalva de que o resultado pode não bater
1:1 com os 40 alertas oficiais.

Padrões varridos:

### 2.1 C / C++ (CodeQL `c-cpp` default)

- `cpp/unbounded-write` — `strcpy`, `strcat`, `sprintf`, `gets`, `vsprintf` sem bound: **0 ocorrências** em produção.
- `cpp/missing-check-scanf` — `scanf`, `sscanf` sem cap: **0 ocorrências**.
- `cpp/alloca` — uso de alloca/__builtin_alloca: **0 ocorrências**.
- `cpp/format-string` — printf com format string variável: **0 ocorrências**.
- `cpp/missing-null-test` — uso de malloc sem null check: **0 ocorrências críticas** (todos os pontos verificados em `src/security/tls.c`, `src/memory/vmm_regions.c` etc. já têm `if (!ptr) return -1;`).
- `cpp/return-stack-allocated-memory` — retorno de ponteiro local: **0 ocorrências** (os falsos positivos do grep são todos `&local_struct` passado como argumento por referência).
- `cpp/comparison-with-wider-type` — mistura int/size_t em loops: **1 ocorrência** em `src/apps/text_editor.c:245` (não acionável: `cur_len` é signed int).
- `cpp/double-free`, `cpp/use-after-free` — verificado em `src/boot/boot_writer.c` e `src/drivers/usb/xhci.c`: padrão `kfree(p); return -1;` está correto, sem reuso.

**Resultado C/C++:** o código CapyOS está **excepcionalmente clean** em
relação aos padrões mais comuns do CodeQL. A disciplina de
segurança/auditoria do projeto é visível.

### 2.2 Python (CodeQL `python` default)

- `py/code-injection` — `eval`, `exec`: **0 ocorrências**.
- `py/command-line-injection` — `os.system`, `shell=True`: **0 ocorrências**.
- `py/unsafe-deserialization` — `pickle.loads`, `yaml.load`: **0 ocorrências**.
- `py/clear-text-logging-sensitive-data` — print de senhas/secrets: **0 ocorrências**.
- `py/regex-injection` — regex com input de usuário: **0 ocorrências** (todos os regex são literais).
- `py/insecure-temporary-file` — `tempfile.mktemp`: **1 ocorrência** ✗ FIXED.
- `py/assert-stmt` — `assert` em código de produção: **18 ocorrências** ✗ FIXED.
- `py/catch-too-general-exception` — `except Exception` / `except BaseException`: **10 ocorrências**, ✗ FIXED 4, deferred 6 (decisão do usuário por escopo de normalização CRLF).
- `py/empty-except` — except sem ação: **0 ocorrências** críticas (os `pass` em cleanup são intencionais e narrowed).
- `py/test-equals-none`, `py/test-equals-bool` — comparações com `==`: **0 ocorrências**.
- `py/missing-encoding` — `open(...)` sem encoding em modo texto: **0 ocorrências** com caminhos críticos.

### 2.3 Workflows GitHub Actions

- Script injection (`${{ github.event.* }}` em `run:`): **0 ocorrências**.
- Permissões: top-level e por job estão configuradas com `contents: read` minimal.
- Actions estão pinadas em tags `v4`/`v6`/`v7` (não em SHA, mas isso é OSSF Scorecard, não CodeQL).

## 3. Findings e fixes aplicados

### 3.1 Insecure temporary file (HIGH) — 1 fix

**Regra CodeQL:** `py/insecure-temporary-file`
**Severidade:** HIGH (TOCTOU race)
**Status:** ✗ FIXED

| Arquivo:linha | Antes | Depois |
|---|---|---|
| `tools/scripts/provision_gpt_workflow.py:40` | `tempfile.mktemp(prefix="manifest_", suffix=".bin")` | `tempfile.mkstemp(...)` + `os.close(fd)` + `Path(path)` |

Adicionado `import os`. `mkstemp` retorna `(fd, path)` atomicamente,
fechando a janela TOCTOU de `mktemp`.

### 3.2 Assert em código de produção (MEDIUM) — 18 fixes

**Regra CodeQL:** `py/assert-stmt`
**Severidade:** MEDIUM (`python -O` strip)
**Status:** ✗ FIXED

| Arquivo | Asserts | Fix |
|---|---:|---|
| `tools/scripts/smoke_x64_session.py` | 5 | `if X is None: raise RuntimeError(...)` agrupado |
| `tools/scripts/release_official_handoff_manifest.py` | 1 | `if X is None: raise RuntimeError(...)` |
| `tools/scripts/release_publication_gate.py` | 1 | idem |
| `tools/scripts/release_public_key_manifest.py` | 1 | idem |
| `tools/scripts/release_public_key_fingerprint.py` | 1 | idem |
| `tools/scripts/release_ci_tag_gate.py` | 2 | idem (combinado em 1 raise) |
| `tools/scripts/release_ci_official_provisioning_contract.py` | 4 | idem |
| `tools/scripts/release_publication_manifest.py` | 1 | idem |
| `tools/scripts/verify_release_signature.py` | 2 | idem |

Comentários explicativos foram adicionados em todos os fixes
referenciando `py/assert-stmt`.

### 3.3 Broad exception (LOW/QUALITY) — 4 fixes, 6 deferred

**Regra CodeQL:** `py/catch-too-general-exception`
**Severidade:** LOW (quality)
**Status:** ✗ FIXED parcial — 4/10

| Arquivo:linha | Antes | Depois | Status |
|---|---|---|---|
| `tools/scripts/smoke_x64_session.py:67` | `except Exception` | `except OSError` | FIXED |
| `tools/scripts/inspect_disk.py:34` | `except Exception as exc` | `except (OSError, ValueError, struct.error, IndexError, KeyError) as exc` | FIXED |
| `tools/scripts/inspect_disk.py:60` | idem | idem | FIXED |
| `tools/scripts/inspect_disk.py:93` | `except Exception as exc` | `except OSError as exc` | FIXED |
| `tools/scripts/smoke_x64_common.py:66` | `except Exception` | (deferred — line endings CRLF) | DEFERRED |
| `tools/scripts/smoke_x64_common.py:78` | idem | (deferred) | DEFERRED |
| `tools/scripts/smoke_x64_iso_install.py:296` | `except Exception as exc` | (deferred) | DEFERRED |
| `tools/scripts/inspect_disk_boot.py:269` | idem | (deferred) | DEFERRED |
| `tools/scripts/smoke_x64_iso_cancel.py:173` | idem | (deferred) | DEFERRED |
| `tools/scripts/smoke_x64_cli.py:291` | idem | (deferred) | DEFERRED |

Os 6 deferred ficaram fora deste audit por uma decisão de escopo
acordada com o usuário: os 24 arquivos com line endings CRLF/mixed
em `tools/scripts/` precisam de normalização Unix antes que edits
tools possam aplicar fixes confiáveis. A normalização é um change
mais amplo e foi adiada para uma onda separada (ver §5).

## 4. Cobertura estimada vs 40 alertas oficiais

| Categoria | Encontrados | Fixed | % cobertura |
|---|---:|---:|---:|
| HIGH (py/insecure-temporary-file) | 1 | 1 | 100% |
| MEDIUM (py/assert-stmt) | 18 | 18 | 100% |
| LOW (py/catch-too-general-exception) | 10 | 4 | 40% |
| Outros (não encontrados em self-audit) | ? | — | — |
| **TOTAL identificados** | **29** | **23** | **79%** |

**Diferença para os 40:** 40 - 29 = 11 alertas residuais não
identificados pelo self-audit. Causas prováveis:

- O CodeQL pode flag a mesma assert mais de uma vez se ela está em
  múltiplos paths/branches.
- Quality queries que não fazem parte do default suite (ex:
  `py/unused-import`, `py/redundant-comparison`) podem estar
  ativadas via configuração não vista no workflow.
- Algumas queries de C que não cobri exaustivamente (ex:
  `cpp/incorrectly-checked-scanf`, `cpp/cleartext-storage-of-sensitive-information`,
  `cpp/uncontrolled-allocation-size`).

**Próximo passo recomendado:** o usuário exporta o SARIF da aba
*Security & Quality → Code scanning → Filter: severity:any* via
GitHub UI ou `gh codeql database analyze`, e cola/salva localmente.
O agent então fará reconciliação 1:1.

## 5. Itens deferred para próxima onda

### 5.1 Normalização de line endings (CRLF → LF)

24 arquivos Python em `tools/scripts/` têm line endings CRLF ou
mistos. Lista:

```
tools/scripts/check_deps.py
tools/scripts/gen_manifest.py
tools/scripts/inspect_disk.py
tools/scripts/inspect_disk_boot.py
tools/scripts/inspect_disk_common.py
tools/scripts/inspect_disk_fat32.py
tools/scripts/mk_efiboot_img.py
tools/scripts/provision_boot_config.py
tools/scripts/provision_bootmedia.py
tools/scripts/provision_fat32.py
tools/scripts/provision_gpt.py
tools/scripts/provision_gpt_cli.py
tools/scripts/provision_gpt_core.py
tools/scripts/provision_gpt_disk.py
tools/scripts/provision_gpt_layout.py
tools/scripts/provision_gpt_workflow.py
tools/scripts/smoke_x64_boot.py
tools/scripts/smoke_x64_cli.py
tools/scripts/smoke_x64_common.py
tools/scripts/smoke_x64_flow.py
tools/scripts/smoke_x64_helpers.py
tools/scripts/smoke_x64_iso_cancel.py
tools/scripts/smoke_x64_iso_install.py
tools/scripts/smoke_x64_session.py
```

**Recomendação:** rodar `dos2unix tools/scripts/*.py` em uma máquina
externa (não local execution policy) ou via CI step, depois aplicar
os 6 fixes restantes de `except Exception`.

### 5.2 SARIF reconciliation

Após o usuário exportar os 40 alertas reais, fazer um passe de
reconciliação para identificar os ~11 alertas residuais não cobertos
pelo self-audit.

### 5.3 Habilitar suite extended

Considerar trocar `# queries: security-extended,security-and-quality`
de comentário para linha ativa no `codeql.yml`, o que aumentaria a
cobertura mas também o volume de alertas. Discussão sobre tradeoff
fica para outro PR.

## 6. Validação externa recomendada

Como local execution policy aplica:

1. `make test` — confirma que os fixes Python não quebraram nenhum
   teste host-side (os scripts editados são chamados indiretamente
   por `make smoke-x64-iso`, `make release-check`, etc).
2. CodeQL CI run no próximo push para `main`/`develop` — recontar
   alertas, esperado **redução de ~23 alertas** (de ~40 para ~17,
   sendo 11 residuais não cobertos pelo self-audit + 6 deferred do
   except narrowing).
3. `make release-check` — sanity completo, especialmente para
   `release_*` que tiveram refactor de asserts.

## 7. Histórico

- **2026-05-15** (criação): self-audit completo em paralelo com
  conclusão do plano dedicado de monolitos residuais. 23 fixes
  aplicados, 6 deferred, 11 residuais esperam SARIF para
  reconciliação.
