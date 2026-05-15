# CapyOS 0.8.0-alpha.51+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de preparação de CI: o projeto passa a ter um
preflight versionado que valida chave pública/fingerprint de release e argumentos
VMware antes de iniciar gates pesados.

A versão alinhada é `0.8.0-alpha.51+20260510`.

## Principais entregas

### Preflight CI F2

- Novo `tools/scripts/release_ci_preflight.py`.
- Novo alvo `release-ci-preflight` no `Makefile`.
- Validação de chave pública Ed25519 PEM e fingerprint SHA-256 esperado.
- Validação de `SMOKE_X64_VMWARE_ARGS` com providers `vmrun` e `govc`.
- Rejeição de flags inadequados para CI de release: `--dry-run`,
  `--no-artifact-check` e `--no-poweroff`.
- Novo guia `docs/operations/release-ci-preflight.md`.

## Compatibilidade

- O preflight é um alvo separado; `release-check` local não passa a depender de
  infraestrutura VMware ou chave oficial.
- Nenhuma chave privada ou chave pública oficial é criada/versionada.
- O smoke VMware real continua dependendo da infraestrutura externa.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- parsing de variáveis e argumentos do preflight;
- fail-closed para chave pública/fingerprint ausentes ou divergentes;
- checagem de provider `vmrun`/`govc` e variáveis `GOVC_*` obrigatórias;
- rejeição de flags de diagnóstico em CI de release;
- target `release-ci-preflight` no `Makefile`;
- documentação em `docs/operations/release-ci-preflight.md`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Publicar a chave Ed25519 oficial esperada e seu fingerprint.
- Provisionar VM/serial/credenciais VMware na CI.
- Rodar `release-ci-preflight`, `verify-release-signature` com pinagem e
  `smoke-x64-vmware-dhcp` na esteira de tag.
