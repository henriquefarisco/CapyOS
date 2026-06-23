# CapyOS 0.8.0-alpha.283+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.283+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Infraestrutura de desenvolvimento (sem mudanca de runtime/ABI)

## Resumo executivo

Implementa as recomendacoes **#2 (automacao de release)** e **#4 (cadencia de
auditoria de seguranca)** da avaliacao geral do projeto. Nenhuma mudanca de
runtime, kernel ou ABI; os 6 repos irmaos permanecem inalterados. Esta propria
release foi cortada pela nova ferramenta `make bump-alpha` (dogfood).

## Mudancas

### #2 -- Automacao do version-bump + politica de batching

- **`tools/scripts/bump_alpha.py`** (novo): fonte unica dos toques de versao
  repetitivos. Detecta a versao atual no `VERSION.yaml` e aplica edicoes
  byte-safe com **assercao de contagem por edicao** (aborta alto se um ancora
  estiver obsoleto) em `VERSION.yaml` (current/extended/current_summary + entry
  no history), `include/core/version.h` (4 macros), `README.md` e
  `docs/plans/STATUS.md`; **scaffolda** a release note de
  `docs/releases/_template.md`. Substitui os `.bumpNNN.py` ad-hoc.
- **`make bump-alpha TO=NNN SUMMARY=...`** (novo alvo): roda o script + depois
  `make version-audit` para auto-verificar.
- **`docs/releases/_template.md`** (novo) + **`docs/operations/release-process.md`**
  (novo): template da release note e a politica de batching (1 alpha = 1 unidade
  de valor coerente, nao 1 micro-slice) + a sequencia completa de envio.

### #4 -- Cadencia de auditoria de seguranca

- **`docs/security/attack-surface.md`** (novo): inventario das areas de input
  nao-confiavel/segredo, com owner, cobertura de teste e status da auditoria.
- **`docs/security/audit-playbook.md`** (novo): checklist por area (bounds,
  no-overflow por subtracao, fail-closed, constant-time, wipe de segredo,
  downgrade) + quando/como auditar.
- **`docs/security/audit-log.md`** (novo): rodada datada 2026-06-17 (o fix do
  ELF loader de alpha.282 + as areas auditadas limpas).
- **`make security-selftest`** (novo alvo) + filtro em `tests/test_runner.c`:
  regressao focada de seguranca que reusa o binario de teste full-link e roda so
  o subconjunto de seguranca quando invocado com o argumento `security`
  (parsers DNS/DHCP/ICMP/ARP, crypto de volume + rekey, csprng/crypt vectors,
  user-password-hash, assinatura capypkg, elf_bounds). O caminho default
  (`make test`, sem argumento) roda a suite inteira como antes.

## Validacao

- `make test` -- verde (caminho default do runner inalterado).
- `make security-selftest` -- `[security-selftest] OK (0 falhas)`.
- `make version-audit` -- alinhado em `0.8.0-alpha.283` (rodado pelo proprio
  `make bump-alpha` que cortou esta release).
- `make all64` nao e afetado (mudancas sao tooling/docs + host-test runner).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.282+20260617` | `0.8.0-alpha.283+20260617` | infra de dev: bump automation (#2) + cadencia de auditoria (#4) |

Sem mudanca de ABI. Os 6 repos irmaos permanecem inalterados (CapyUI segue 2.22.6).

_Build: `0.8.0-alpha.283+20260617`_
