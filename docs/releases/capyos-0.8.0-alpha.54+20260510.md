# CapyOS 0.8.0-alpha.54+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de endurecimento do preflight de CI: o
preflight passa a validar o manifesto público da chave Ed25519 esperada contra a
chave pública e o fingerprint pinado antes dos gates pesados.

A versão alinhada é `0.8.0-alpha.54+20260510`.

## Principais entregas

### Manifesto validado no preflight

- `tools/scripts/release_ci_preflight.py` agora exige
  `RELEASE_PUBLIC_KEY_MANIFEST` ou `CAPYOS_RELEASE_PUBLIC_KEY_MANIFEST`.
- O preflight rejeita manifesto ausente, vazio, não UTF-8, malformado, com campo
  desconhecido, duplicado ou obrigatório ausente.
- O preflight valida `format`, `algorithm`, `public_key_encoding`,
  `private_key_included`, nome da chave pública e fingerprints.
- O manifesto precisa concordar com a chave pública Ed25519 e com o fingerprint
  esperado usado pela CI.

## Compatibilidade

- Nenhuma chave privada é lida, criada ou versionada.
- O target `release-ci-preflight` continua separado de `release-check` para não
  exigir infraestrutura externa em validação local.
- CI de release deve gerar/publicar o manifesto antes do preflight.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- parsing estrito do manifesto público da chave;
- rejeição de campos ausentes, duplicados, desconhecidos ou malformados;
- checagem de formato/algoritmo/encoding/private-key flag;
- comparação do nome da chave pública;
- normalização e comparação dos fingerprints do manifesto;
- revalidação Ed25519 DER/SPKI da chave pública;
- forwarding de `RELEASE_PUBLIC_KEY_MANIFEST` pelo `Makefile`;
- documentação em `docs/security/release-signing.md` e
  `docs/operations/release-ci-preflight.md`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Operador gerar/exportar a chave Ed25519 oficial.
- Publicar a chave pública oficial, fingerprint e manifesto.
- Provisionar VM/serial/credenciais VMware na CI.
- Rodar `release-ci-preflight`, `verify-release-signature` com pinagem e
  `smoke-x64-vmware-dhcp` na esteira de tag.
