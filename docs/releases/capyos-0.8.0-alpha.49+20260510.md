# CapyOS 0.8.0-alpha.49+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de segurança para assinatura de release:
`verify_release_signature.py` passa a ter um self-test negativo que comprova que
assinaturas Ed25519 mutiladas falham fechado.

A versão alinhada é `0.8.0-alpha.49+20260510`.

## Principais entregas

### Self-test negativo Ed25519

- Novo `--self-test` em `tools/scripts/verify_release_signature.py`.
- O self-test gera material temporário, assina um arquivo mínimo e valida que a
  assinatura correta é aceita.
- Em seguida, altera um byte da assinatura e exige que o verificador rejeite a
  assinatura mutilada.
- Novo alvo `verify-release-signature-selftest` no `Makefile`.
- `release-check` passa a incluir esse self-test como gate futuro de CI.

## Compatibilidade

- Nenhuma chave oficial é criada ou versionada por este patch.
- O self-test usa apenas material temporário e não depende da infraestrutura
  VMware nem da chave pública oficial.
- O fluxo normal `verify-release-signature` permanece compatível.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- geração temporária de chave Ed25519 no self-test;
- assinatura válida aceita por `signature_verify_ok`;
- assinatura mutilada exigida como falha;
- alvo `verify-release-signature-selftest` no `Makefile`;
- inclusão do self-test em `release-check`;
- documentação em `docs/security/release-signing.md`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Publicar a chave Ed25519 oficial esperada.
- Provisionar VM/serial/credenciais VMware na CI.
- Ligar `verify-release-signature` e `smoke-x64-vmware-dhcp` na esteira de tag.
