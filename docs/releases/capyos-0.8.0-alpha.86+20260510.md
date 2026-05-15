# CapyOS 0.8.0-alpha.86+20260510

## Resumo executivo

Este patch avanca F2 adicionando um gate publico de prontidao do smoke VMware oficial. O novo gate valida o manifesto oficial de handoff CI/release e os argumentos `SMOKE_X64_VMWARE_ARGS` antes da execucao real do smoke, sem acessar chave privada, sem ligar VM, sem chamar `make` e sem chamar `git`.

## Principais entregas

- Adiciona `tools/scripts/release_ci_smoke_readiness.py`.
- Adiciona alvo `release-ci-smoke-readiness` no `Makefile`.
- Valida `VERSION.yaml`, `include/core/version.h`, `README.md`, release note e tag publica.
- Valida `release-official-handoff.manifest` em formato `capyos-release-official-handoff-manifest-v1`.
- Confere gates obrigatorios do handoff: `release-ci-official-provisioning-contract`, `release-ci-tag-gate`, `release-publication-gate` e `smoke-x64-vmware-dhcp`.
- Confere que o handoff declara `private_key_included=no`, `vm_powered_on=no`, `make_executed=no` e `git_executed=no`.
- Confere logs do smoke como paths relativos seguros em `build/ci/*.log`.
- Reusa o contrato oficial de `SMOKE_X64_VMWARE_ARGS` para rejeitar `--dry-run`, `--no-artifact-check`, `--no-poweroff`, `--no-tool-check` e `--gui`.

## Seguranca e compatibilidade

- A chave privada oficial permanece offline e fora da CI.
- O gate nao assina, nao cria chaves, nao liga VM e nao publica artefatos.
- O fluxo falha fechado se qualquer campo do handoff divergir da versao/tag ou dos argumentos oficiais do smoke.
- O alvo e complementar aos gates existentes; nao altera o ABI do sistema.

## Validacao estatica

- Revisao estatica confirmou que o novo gate nao chama `make`, `git`, OpenSSL ou VMware.
- Revisao estatica confirmou que o script rejeita ambiente com `RELEASE_PRIVATE_KEY`/`CAPYOS_RELEASE_PRIVATE_KEY`.
- Revisao estatica confirmou que temporarios do patch foram removidos.
- Nao foram executados `make`, `git`, build, suite de testes ou smoke VMware real.
