# CapyOS 0.8.0-alpha.62+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de manifesto oficial de handoff público para CI/release. O projeto passa a ter um artefato determinístico que amarra tag, versão, checksums, assinatura, chave pública oficial, manifesto da chave, manifesto de publicação e contrato VMware antes da etapa externa real.

A versão alinhada é `0.8.0-alpha.62+20260510`.

## Principais entregas

- Novo `tools/scripts/release_official_handoff_manifest.py`.
- Novo alvo `release-official-handoff-manifest` no `Makefile`.
- Novo alvo `verify-release-official-handoff-manifest` no `Makefile`.
- Novo material público `build/release-official-handoff.manifest`.
- Reuso do contrato oficial de provisionamento para rejeitar chave privada e validar tag/chave/smoke.
- Conferência cruzada entre checksums, assinatura, manifesto da chave e manifesto público de publicação.

## Segurança e compatibilidade

- Nenhuma chave privada é lida, criada ou versionada.
- O script não executa `make`, `git`, OpenSSL ou VMware.
- O manifesto registra `private_key_included=no`, `vm_powered_on=no`, `make_executed=no` e `git_executed=no`.
- O modo `--verify` compara o manifesto existente contra os materiais públicos esperados.
- O formato é chave/valor UTF-8 estável para diffs e auditoria.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, scripts de build, scripts de teste ou automação Python de validação.

Pontos revisados estaticamente:

- rejeição de chave privada no ambiente;
- validação de tag/versionamento via contrato oficial existente;
- validação PEM/SPKI Ed25519 via contrato oficial existente;
- validação do manifesto público da chave;
- conferência de assinatura raw Ed25519 por tamanho;
- conferência SHA-256 dos materiais públicos;
- conferência do manifesto de publicação contra checksums;
- geração/verificação determinística do manifesto de handoff;
- target `release-official-handoff-manifest` no `Makefile`;
- target `verify-release-official-handoff-manifest` no `Makefile`;
- ausência de scripts temporários após higienização.

## Próximos passos

- Operador gerar/exportar a chave Ed25519 oficial fora do repositório.
- Publicar chave pública oficial, fingerprint e manifestos públicos.
- Provisionar VM/serial/credenciais VMware reais na CI.
- Avançar `0.8.0-alpha.63` com chave offline oficial/CI do smoke VMware quando a infraestrutura externa estiver pronta.
