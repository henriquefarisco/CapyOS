# CapyOS 0.8.0-alpha.12+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch incremental de **Update/F5** focado na verificação de
assinatura de manifestos antes do fetch remoto operacional. O `update-agent`
agora trata `signature_ed25519=` como requisito para manifestos novos, staged e
importados, mantendo a proteção anti-downgrade, o `payload_sha256` obrigatório e
o apply verificado introduzidos em `alpha.11`.

A versão alinhada é `0.8.0-alpha.12+20260509`.

## Principais entregas

### Gate Ed25519 para manifestos

- Manifestos novos no catálogo local só viram `update_available` se declararem
  `signature_ed25519` hex128 válida.
- Manifestos staged só viram `stage_ready` se declararem assinatura válida.
- `update-import-manifest` recusa manifestos sem assinatura válida antes de
  persistir `/system/update/latest.ini`.

### Texto canônico assinado

- O texto assinado é reconstruído a partir do manifesto original excluindo apenas
  a linha `signature_ed25519=`.
- O parser exige exatamente uma linha de assinatura.
- Assinaturas malformadas, ausentes ou divergentes entram em estado degradado
  específico.

### Integração de código

- `src/services/update_agent.c` passa a incluir `security/ed25519.h`.
- O runtime normal usa chave pública embarcada e `ed25519_verify()`.
- Sob `UNIT_TEST`, o agente aceita verifier injetável para regressões host.
- `Makefile` host passa a linkar `src/security/ed25519.c`.

## Códigos de erro relevantes

- `-23` — import recusado por assinatura Ed25519 ausente ou inválida.
- `-28` — catálogo local novo com assinatura Ed25519 ausente ou inválida.
- `-29` — staged manifest com assinatura Ed25519 ausente ou inválida.

## Compatibilidade

- O manifesto mantém os campos existentes:
  - `available_version=`
  - `channel=`
  - `branch=`
  - `source=`
  - `published_at=`
  - `payload_sha256=`
- A partir desta release, manifestos novos/staged/importados também devem incluir:
  - `signature_ed25519=` com 128 caracteres hexadecimais.
- Manifestos na mesma versão do sistema continuam legíveis para estado local, mas
  não representam update aplicável.

## Validacao

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- `update_agent_poll()` só marca `update_available` quando versão, hash e
  assinatura são válidos.
- `update_agent_poll()` só marca `stage_ready` quando staged não é downgrade,
  possui `payload_sha256` e assinatura Ed25519 válida.
- `update_agent_import_manifest_path()` valida assinatura antes de persistir o
  cache local.
- Fixtures planejadas em `tests/test_update_agent.c`, `tests/test_update_transact.c`
  e `tests/test_audit_events.c` foram ajustadas para o novo requisito.
- O host `Makefile` passou a incluir `src/security/ed25519.c` para satisfazer a
  referência de `update_agent.c`.

## Status do pacote

- ✅ Gate anti-downgrade preservado.
- ✅ `payload_sha256` obrigatório preservado.
- ✅ `signature_ed25519` obrigatória para updates novos/staged/importados.
- ✅ Apply direto de staged hashado continua bloqueado.
- ✅ Documentação e manifesto de versão alinhados para fechamento manual.

## Próximos passos

- Implementar `update-fetch` com download real via GitHub Releases/TLS.
- Conectar a assinatura operacional de release ao pipeline F2/F5.
- Operador pode executar comandos `git` manualmente para registrar o patch.
- Validações executáveis ficam para operador/CI, conforme as restrições desta
  sessão.
