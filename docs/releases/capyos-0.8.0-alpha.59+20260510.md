# CapyOS 0.8.0-alpha.59+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de contrato público de CI para publicação: o
projeto passa a validar estruturalmente os artefatos públicos, manifestos,
fingerprint pinado e argumentos VMware antes dos gates de publicação.

A versão alinhada é `0.8.0-alpha.59+20260510`.

## Principais entregas

### Contrato público de CI/publicação

- Novo `tools/scripts/release_ci_publication_contract.py`.
- Novo alvo `release-ci-publication-contract` no `Makefile`.
- Nova documentação operacional em
  `docs/operations/release-ci-publication-contract.md`.
- Rejeição de ambiente com variável de chave privada definida.
- Validação estrutural de checksums, assinatura, chave pública, manifesto da
  chave e manifesto de publicação.
- Validação de `SMOKE_X64_VMWARE_ARGS` para `vmrun` ou `govc` sem executar VM.

## Compatibilidade

- Nenhuma chave privada é lida, criada ou versionada.
- O contrato não executa `make`, `git`, OpenSSL ou VMware.
- A validação criptográfica continua nos gates públicos já versionados.
- A execução real em CI ainda depende da chave pública oficial e da infraestrutura
  VMware provisionada.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- rejeição de `RELEASE_PRIVATE_KEY` e `CAPYOS_RELEASE_PRIVATE_KEY` no ambiente;
- exigência de chave pública e fingerprint SHA-256 esperado;
- validação estrutural de manifestos públicos;
- comparação de fingerprints entre manifestos;
- comparação de SHA-256 de checksums, assinatura e manifesto da chave;
- comparação entre entradas de `release-artifacts.sha256` e manifesto de
  publicação;
- rejeição de caminhos de artefato inseguros;
- validação de provider VMware `vmrun`/`govc` e rejeição de flags diagnósticos;
- target `release-ci-publication-contract` no `Makefile`;
- documentação operacional e de assinatura de release;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Operador gerar/exportar a chave Ed25519 oficial.
- Publicar a chave pública oficial, fingerprint e manifestos públicos.
- Provisionar VM/serial/credenciais VMware na CI.
- Rodar `release-ci-preflight`, `release-ci-publication-contract`,
  `release-publication-gate`, `verify-release-signature` com pinagem e
  `smoke-x64-vmware-dhcp` na esteira de tag.
