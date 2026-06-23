# CapyOS — processo de release (alpha)

Mecânica de cortar uma release alpha do CapyOS, com a automação do version-bump
e a política de agrupamento (batching). Para a coordenação cross-repo (releases
pareadas com repos irmãos) ver também
`.windsurf/workflows/cross-repo-release-coordination.md`.

## 1. Política de batching

Um bump alpha é um **evento de release** (tag + GitHub Release + 4 workflows).
Não desperdice esse custo em micro-incrementos:

- **Agrupe** slices/fixes relacionados num único alpha. Ex.: adicionar 5 apps ao
  mesmo smoke-roundtrip deve ser **uma** release, não cinco.
- **1 alpha = 1 unidade de valor coerente** (uma feature, um fix com sua
  regressão, um lote de hardening/docs), não um passo mecânico isolado.
- Exceções legítimas a release isolada: um **fix de segurança** ou um **fix de
  correção** que você quer rastrear sozinho; uma release de **coordenação
  cross-repo** que pina um irmão.

## 2. Bump automatizado (`make bump-alpha`)

`tools/scripts/bump_alpha.py` é a fonte única para os toques de versão repetitivos
e propensos a drift que o `make version-audit` cobre, **mais** o `STATUS.md`. Ele
detecta a versão atual a partir do `VERSION.yaml` e aplica edições byte-safe com
asserção de contagem por edição (aborta alto se um âncora estiver obsoleto, em vez
de aplicar pela metade).

```sh
# resumo de uma linha (vai para current_summary + entry do history):
make bump-alpha TO=283 SUMMARY="apps-roundtrip cobre os 5 apps basicos"
# ou de arquivo:
make bump-alpha TO=283 SUMMARY_FILE=/tmp/sum.txt
# preview sem escrever:
make bump-alpha TO=283 SUMMARY="..." BUMP_ALPHA_ARGS=--dry-run
```

Atualiza automaticamente:

- `VERSION.yaml` — `channels.alpha.current`, `.extended`, `.current_summary` e
  um novo entry (mais recente primeiro) sob `history:`.
- `include/core/version.h` — `CAPYOS_VERSION_PRERELEASE/_EXTENDED/_FULL/_ALPHA`.
- `README.md` — a linha "Versao de referencia:".
- `docs/plans/STATUS.md` — a linha de versão executiva + o cabeçalho da seção de
  repos irmãos ("estado em alpha.NNN").
- `docs/releases/capyos-<extended>.md` — **scaffold** a partir de
  `docs/releases/_template.md` (só se ausente), satisfazendo o `version-audit`;
  o autor então preenche o corpo.

Depois roda `make version-audit` para auto-verificar.

### Ainda manual (por design)

Estes são específicos de release pareada/contrato, não toil de toda release; o
script imprime um lembrete:

- `VERSION` do repo irmão + linha da `compatibility-matrix.md` + tabela de irmãos
  no `STATUS.md` (só quando um irmão muda de versão).
- Prosa do `master-plan` / `readiness` / `architecture` e o bullet cronológico
  "Atualizacao alpha.NNN" do `STATUS.md`.
- O índice `docs/releases/README.md` (narrativo; opcional).

## 3. Sequência completa de envio

Validação roda no host remoto (`Automation/remote-exec.sh`); este computador é
review/edit. WSL para `make`, cmd para `git`/`gh`.

1. `make bump-alpha TO=...` + preencher a release note.
2. Validar o que a mudança exige: `make test`, `make layout-audit`,
   `make version-audit`, `make all64` (+ gates/QEMU aplicáveis — ver
   `.windsurf/rules/30-validation-gates.md`).
3. Commit em `develop` (mensagem fora do repo, `git commit -F`), push.
4. CI verde em `develop` (CI + CodeQL).
5. Fast-forward `main` ← `develop`, push `main`.
6. Tag `v<extended>` (ex.: `v0.8.0-alpha.283+20260617`), push da tag.
7. O workflow **Release Artifacts** cria o GitHub Release (ISO + sha256);
   `gh release edit <tag> --title "CapyOS <extended>" --notes-file <release-note>
   --latest` para o título + corpo + marcação Latest.
8. Confirmar os 4 workflows do tag/main verdes (Release Artifacts, CI,
   CodeQL, Security Hardening).
9. Restaurar host em `develop`, limpar temporários.
