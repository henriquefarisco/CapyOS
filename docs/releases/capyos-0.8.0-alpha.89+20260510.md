# CapyOS 0.8.0-alpha.89+20260510

## Resumo executivo

Este patch avanca F2 adicionando um gate/manifesto publico de promocao pos-smoke VMware oficial. O novo gate confere que publicacao, handoff oficial e aceitacao da evidencia estao alinhados antes de declarar a release publicamente promovivel, sem ligar VM, sem acessar chave privada, sem chamar `make`, sem chamar `git` e sem executar OpenSSL.

## Principais entregas

- Adiciona `tools/scripts/release_ci_smoke_promotion.py`.
- Adiciona os alvos `release-ci-smoke-promotion` e `verify-release-ci-smoke-promotion` no `Makefile`.
- Adiciona `RELEASE_SMOKE_PROMOTION_MANIFEST`, `RELEASE_CI_SMOKE_PROMOTION_ARGS` e `RELEASE_CI_SMOKE_PROMOTION_VERIFY_ARGS`.
- Reusa o gate de aceitacao do `alpha.88` para recalcular o manifesto esperado antes da promocao.
- Amarra `release-publication.manifest`, `release-official-handoff.manifest` e `release-smoke-acceptance.manifest` em `release-smoke-promotion.manifest`.

## Seguranca e compatibilidade

- A chave privada oficial permanece offline e fora da CI publica.
- O gate nao aciona VMware, nao assina, nao cria chaves, nao chama OpenSSL e nao publica artefatos.
- O manifesto registra hashes SHA-256 dos manifestos de publicacao, handoff e aceitacao.
- O alvo e complementar aos gates anteriores; nao altera ABI, kernel, userland ou formato dos artefatos de boot.

## Validacao estatica

- Revisao estatica confirmou que o novo gate nao chama `subprocess`, VMware, OpenSSL, `make` ou `git`.
- Revisao estatica confirmou que o gate rejeita ambiente com `RELEASE_PRIVATE_KEY`/`CAPYOS_RELEASE_PRIVATE_KEY`.
- Revisao estatica confirmou que temporarios do patch foram removidos.
- Nao foram executados `make`, `git`, build, suite de testes ou smoke VMware real.
