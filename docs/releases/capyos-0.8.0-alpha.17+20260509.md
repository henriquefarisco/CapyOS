# CapyOS 0.8.0-alpha.17+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha o patch **Update/F5.4**: a política remota de manifestos agora
é explícita e consistente entre bootstrap de configuração, `repository.ini` e
`update-agent`.

A versão alinhada é `0.8.0-alpha.17+20260509`.

## Principais entregas

### Política por trilha

- `develop` deriva `remote_manifest` em `refs/heads/<branch>`.
- `stable` mantém `branch=main` para compatibilidade de manifestos assinados, mas
  deriva `remote_manifest` em `refs/tags/v<major>.<minor>.<patch>`.
- A tag estável é calculada a partir do núcleo semântico da versão corrente.

### Bootstrap e runtime alinhados

- `system_prepare_update_catalog()` e `config_write_update_repository_file()`
  passam a gravar `remote_manifest` conforme a política F5.4.
- `update-agent`, quando não encontra `remote_manifest=` no repositório, deriva a
  mesma URL a partir de `source=github:<owner>/<repo>`, `channel=`, `branch=` e
  versão corrente.

### Regressões planejadas

`tests/test_update_agent.c` foi alinhado para revisar estaticamente:

- URL padrão stable por `refs/tags/v0.8.0`;
- URL develop por `refs/heads/develop`;
- `update-fetch` stable usando a tag versionada derivada.

## Compatibilidade

- Manifestos continuam validando `channel`, `branch` e `source`.
- `stable` continua usando `branch=main` dentro do manifesto.
- Apenas o ref bruto do GitHub muda para tags versionadas quando a trilha é
  `stable`.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- derivação `system_update_remote_manifest_url()`;
- derivação `build_remote_manifest_url()` no update-agent;
- regressões planejadas de URLs remotas;
- documentação operacional, CLI, release-signing, STATUS e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Baixar payload remoto declarado no manifesto.
- Adicionar smoke `smoke-x64-update-fetch` com servidor HTTP local em QEMU.
- Integrar TLS userland completo quando F4 entregar `libcapy-tls`.
