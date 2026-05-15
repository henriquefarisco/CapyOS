# CapyOS 0.8.0-alpha.61+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de contrato oficial de provisionamento público para CI/release. O projeto passa a validar, sem chave privada e sem executar VMware, que a tag, a chave pública oficial, o fingerprint pinado, o manifesto da chave e os argumentos do smoke VMware estão coerentes antes da esteira real de release.

A versão alinhada é `0.8.0-alpha.61+20260510`.

## Principais entregas

- Novo `tools/scripts/release_ci_official_provisioning_contract.py`.
- Novo alvo `release-ci-official-provisioning-contract` no `Makefile`.
- Validação PEM/SPKI Ed25519 da chave pública oficial usando apenas biblioteca padrão Python.
- Rejeição de marcadores de chave privada e variáveis de ambiente de chave privada.
- Validação de fingerprint SHA-256 pinado contra a chave pública oficial.
- Validação do manifesto público da chave oficial.
- Validação de tag pública contra `VERSION.yaml`, `include/core/version.h`, `README.md` e release note.
- Validação endurecida de `SMOKE_X64_VMWARE_ARGS` para smoke oficial.

## Segurança e compatibilidade

- Nenhuma chave privada é lida, criada ou versionada.
- O contrato não executa `make`, `git`, OpenSSL ou VMware.
- O contrato rejeita `--dry-run`, `--no-artifact-check`, `--no-poweroff`, `--no-tool-check` e `--gui` no smoke oficial.
- O smoke oficial exige `--serial-log` relativo em `build/ci/*.log`.
- Provider `govc` exige `--vm-name`, `--govc-serial-log` e ambiente GOVC mínimo.
- Provider `vmrun` exige `--vmx`.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, scripts de build, scripts de teste ou automação Python de validação.

Pontos revisados estaticamente:

- parsing PEM/SPKI Ed25519 sem OpenSSL;
- rejeição de chave privada no arquivo e no ambiente;
- normalização e comparação SHA-256;
- validação de manifesto público da chave;
- validação de tag/versionamento;
- validação estrutural de argumentos VMware oficiais;
- target `release-ci-official-provisioning-contract` no `Makefile`;
- documentação operacional e versionamento;
- ausência de scripts temporários após higienização.

## Próximos passos

- Operador gerar/exportar a chave Ed25519 oficial fora do repositório.
- Publicar chave pública oficial, fingerprint e manifesto público.
- Provisionar VM/serial/credenciais VMware reais na CI.
- Rodar `release-ci-official-provisioning-contract`, `release-ci-tag-gate` e `smoke-x64-vmware-dhcp` na esteira real.
