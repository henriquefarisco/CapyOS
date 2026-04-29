# Checklist de PR e release - CapyOS

Esta lista executa o gate humano que complementa os gates automaticos
(`make test`, `make layout-audit`, `make release-check`). Ela garante que cada
mudanca relevante atualize a fonte de verdade da robustez do sistema.

## Como usar

1. Abra um PR ou prepare uma release.
2. Marque cada caixa abaixo apenas depois de validar a evidencia.
3. Cole as evidencias (saida de comando, link de release note, arquivo
   modificado) no corpo do PR ou no checklist do release.
4. Bloqueie merge/release se algum item estiver incompleto sem justificativa
   explicita.

## 1. Antes de abrir o PR

- [ ] Branch baseado em `develop` (ou em uma feature branch que sera mergeada
      em `develop`).
- [ ] Mudanca isolada por dominio: rede, browser, fs, seguranca, build,
      documentacao etc. PRs cruzando dominios distintos devem ser quebrados.
- [ ] Cada arquivo novo abaixo de 900 linhas; arquivos com excecao por dados
      estaticos foram comentados em `tools/scripts/audit_source_layout.py`.
- [ ] Includes cruzados de pastas `internal/` foram revisados manualmente; o
      auditor estatico deve continuar sem violacoes.
- [ ] Comentarios e docstrings em ingles ou portugues claro, sem texto gerado
      automaticamente sem revisao.
- [ ] Nenhum segredo, chave privada, token ou credencial em arquivos
      versionados.

## 2. Gates automaticos obrigatorios

- [ ] `make test` passou local.
- [ ] `make layout-audit` passou local em modo estrito.
- [ ] `make all64 TOOLCHAIN64=elf` passou local; build host e usado apenas
      para iteracao rapida.
- [ ] `make release-check` passou local quando o PR afeta release, build,
      seguranca, FS, rede, browser ou performance.
- [ ] Quando aplicavel, `make smoke-x64-cli SMOKE_X64_CLI_ARGS='--require-shell'`
      ou outro smoke especifico foi executado e o log anexado ao PR.

## 3. Robustez e plano vivo

- [ ] Se a mudanca tocou em algum item de `docs/plans/capyos-robustness-master-plan.md`,
      o status do item foi atualizado seguindo a regra:
      `Ainda nao iniciado` -> `Parcial` -> `Implementado` -> `Substituido`/`Bloqueado`.
- [ ] A coluna `Evidencia` cita arquivo/teste/log/release e a coluna
      `Proximo passo` reflete o que ainda falta.
- [ ] O bloco "Observacao inicial" recebeu uma linha datada quando a mudanca
      foi um marco (release, gate novo, baseline novo, smoke novo etc.).

## 4. Performance

- [ ] Se a mudanca afeta boot, FS, rede, UI ou GUI, foi rodado o comando
      relevante (`perf-boot`, `perf-fs`, `perf-net`, `perf-mem`) ou um smoke
      especifico, e o resultado anexado ao PR.
- [ ] Se afeta `total_boot_to_login_us`, o JSON em
      `docs/performance/boot-baseline.json` foi atualizado seguindo o
      processo de baseline e a mudanca esta justificada.

## 5. Seguranca

- [ ] Mudancas em autenticacao, sessao, privilegio ou login disparam revisao
      cruzada e atualizam `tests/test_auth_policy.c`,
      `tests/test_login_runtime.c` ou outro teste correspondente.
- [ ] Mudancas em `update_agent`, `boot_slot`, `recovery` ou journal
      atualizam `tests/test_update_agent.c`, `tests/test_update_transact.c`,
      `tests/test_journal.c`, `tests/test_audit_events.c` conforme
      necessario.
- [ ] Eventos sensiveis novos sao emitidos via `klog(KLOG_*, "[<dominio>] ...")`
      para que a auditoria persistente em `/var/log/capyos_klog.txt` os
      capture.

## 6. Browser e internet

- [ ] Mudancas em `src/apps/html_viewer/*` ou em `src/net/*` mantem os
      budgets ativos (`navigation_budget.c`) e o `safe_mode` previsivel.
- [ ] `tests/html_viewer/resource_cases.inc` cobre o novo comportamento se
      ele afeta render, parse, cache ou rede.

## 7. Filesystem

- [ ] Mudancas em CAPYFS, journal ou recovery atualizam a integracao entre
      `mount_capyfs`, `capyfs_journal_integration.c` e a causa de recovery
      reportada por `recovery-status`.
- [ ] Caso o formato do journal mude, foi escrito teste de migracao ou
      compatibilidade em `tests/test_journal.c`.

## 8. Documentacao

- [ ] `docs/README.md` foi atualizado se um novo plano, guia ou pasta foi
      criado.
- [ ] `docs/plans/README.md` continua coerente com a classificacao de
      `Ativo`, `Historico`, `Experimental` ou `Substituido`.
- [ ] Release notes em `docs/releases/` foram criadas/atualizadas para
      mudancas que afetam usuarios externos.

## 9. Antes de promover para `main`

- [ ] Smoke oficial VMware+E1000 ou seu substituto atual foi executado e
      anexado.
- [ ] Tag de versao em `VERSION.yaml` esta consistente com `README.md`,
      `include/core/version.h` e a release note correspondente; `make
      version-audit` passou.
- [ ] Checksums de release (`build/release-artifacts.sha256`) foram
      regenerados e validados via `make verify-release-checksums TOOLCHAIN64=elf`.

## Notas

- Itens podem ser dispensados com justificativa explicita no PR (ex.: PR
  apenas de documentacao). Se voce dispensar, registre o motivo.
- Mudancas urgentes de seguranca podem usar caminho expresso, mas precisam
  abrir issue de follow-up para reabrir os gates restantes em ate 7 dias.
