# CapyOS 0.8.0-alpha.87+20260510

## Resumo executivo

Este patch avanca F2 adicionando um manifesto/verificador publico de evidencia pos-smoke VMware oficial. O novo gate consome o handoff oficial, os argumentos `SMOKE_X64_VMWARE_ARGS`, o serial log e o summary log ja produzidos pelo smoke real, sem acessar chave privada, sem ligar VM, sem chamar `make` e sem chamar `git`.

## Principais entregas

- Adiciona `tools/scripts/release_ci_smoke_evidence.py`.
- Adiciona os alvos `release-ci-smoke-evidence` e `verify-release-ci-smoke-evidence` no `Makefile`.
- Adiciona `RELEASE_SMOKE_EVIDENCE_MANIFEST`, `RELEASE_CI_SMOKE_EVIDENCE_ARGS` e `RELEASE_CI_SMOKE_EVIDENCE_VERIFY_ARGS`.
- Valida versao/tag, handoff oficial e contrato de `SMOKE_X64_VMWARE_ARGS`.
- Confere que `serial.log` e `summary.log` existem, nao estao vazios, ficam em `build/ci/*.log` e contem os markers obrigatorios.
- Mantem o marker padrao `[net] DHCP: lease acquired.` e respeita markers customizados via `--marker`.
- Rejeita evidencias com marcadores privados ou falhas graves como panic/triple fault.
- Gera `build/release-smoke-evidence.manifest` em formato deterministico `capyos-release-smoke-evidence-manifest-v1`.

## Seguranca e compatibilidade

- A chave privada oficial permanece offline e fora da CI publica.
- O verificador nao aciona VMware, nao assina, nao cria chaves e nao publica artefatos.
- O manifesto registra hashes SHA-256 e tamanhos das evidencias para auditoria posterior.
- O alvo e complementar ao smoke real; nao altera ABI, kernel, userland ou formato de artefatos de boot.

## Validacao estatica

- Revisao estatica confirmou que o novo verificador nao chama `subprocess`, VMware, OpenSSL, `make` ou `git`.
- Revisao estatica confirmou que o verificador rejeita ambiente com `RELEASE_PRIVATE_KEY`/`CAPYOS_RELEASE_PRIVATE_KEY`.
- Revisao estatica confirmou que temporarios do patch foram removidos.
- Nao foram executados `make`, `git`, build, suite de testes ou smoke VMware real.
