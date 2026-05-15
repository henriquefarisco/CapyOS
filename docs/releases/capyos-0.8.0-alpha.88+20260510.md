# CapyOS 0.8.0-alpha.88+20260510

## Resumo executivo

Este patch avanca F2 adicionando um gate/manifesto publico de aceitacao da evidencia pos-smoke VMware oficial. O novo gate confere o handoff oficial, recalcula a expectativa do manifesto de evidencia, valida os logs e markers ja produzidos e materializa uma decisao auditavel de aceitacao sem ligar VM, sem acessar chave privada, sem chamar `make` e sem chamar `git`.

## Principais entregas

- Adiciona `tools/scripts/release_ci_smoke_acceptance.py`.
- Adiciona os alvos `release-ci-smoke-acceptance` e `verify-release-ci-smoke-acceptance` no `Makefile`.
- Adiciona `RELEASE_SMOKE_ACCEPTANCE_MANIFEST`, `RELEASE_CI_SMOKE_ACCEPTANCE_ARGS` e `RELEASE_CI_SMOKE_ACCEPTANCE_VERIFY_ARGS`.
- Reusa o verificador de evidencia do `alpha.87` para recalcular o manifesto esperado antes da aceitacao.
- Amarra `release-official-handoff.manifest`, `release-smoke-evidence.manifest`, logs `build/ci/*.log`, markers e gates obrigatorios em `release-smoke-acceptance.manifest`.
- Registra os gates obrigatorios de promocao: provisioning, tag gate, publication gate, readiness, smoke VMware, evidence e verify evidence.

## Seguranca e compatibilidade

- A chave privada oficial permanece offline e fora da CI publica.
- O gate nao aciona VMware, nao assina, nao cria chaves, nao chama OpenSSL e nao publica artefatos.
- O manifesto registra hashes SHA-256 do handoff e da evidencia para auditoria posterior.
- O alvo e complementar ao smoke real; nao altera ABI, kernel, userland ou formato de artefatos de boot.

## Validacao estatica

- Revisao estatica confirmou que o novo gate nao chama `subprocess`, VMware, OpenSSL, `make` ou `git`.
- Revisao estatica confirmou que o gate rejeita ambiente com `RELEASE_PRIVATE_KEY`/`CAPYOS_RELEASE_PRIVATE_KEY`.
- Revisao estatica confirmou que temporarios do patch foram removidos.
- Nao foram executados `make`, `git`, build, suite de testes ou smoke VMware real.
