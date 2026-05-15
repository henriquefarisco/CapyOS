# CapyOS 0.8.0-alpha.50+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de segurança para assinatura de release:
`verify_release_signature.py` passa a validar, opcionalmente, o fingerprint
SHA-256 esperado da chave pública Ed25519 usada para verificar os checksums.

A versão alinhada é `0.8.0-alpha.50+20260510`.

## Principais entregas

### Pinagem SHA-256 da chave pública

- Novo `--expected-public-key-sha256` em `tools/scripts/verify_release_signature.py`.
- O valor também pode vir de `CAPYOS_RELEASE_PUBLIC_KEY_SHA256`.
- O formato aceita hex64 contínuo ou pares hex separados por `:`.
- `verify-release-signature` passa a propagar `RELEASE_PUBLIC_KEY_SHA256` quando informado.
- O self-test agora cobre fingerprint correto, fingerprint com `:` e fingerprint incorreto.

## Compatibilidade

- O fluxo sem `RELEASE_PUBLIC_KEY_SHA256` permanece compatível.
- Nenhuma chave oficial ou chave privada é criada/versionada.
- A pinagem permite que a CI detecte assinatura válida feita com chave pública errada.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- normalização do fingerprint SHA-256 esperado;
- comparação com SHA-256 do DER/SPKI da chave pública Ed25519;
- rejeição fail-closed para fingerprint inválido ou inesperado;
- preservação do fluxo sem pinagem;
- passagem opcional de `RELEASE_PUBLIC_KEY_SHA256` pelo `Makefile`;
- atualização do self-test para fingerprint correto/incorreto;
- documentação em `docs/security/release-signing.md`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Publicar a chave Ed25519 oficial esperada e seu fingerprint.
- Provisionar VM/serial/credenciais VMware na CI.
- Ligar `verify-release-signature` com `RELEASE_PUBLIC_KEY_SHA256` e
  `smoke-x64-vmware-dhcp` na esteira de tag.
