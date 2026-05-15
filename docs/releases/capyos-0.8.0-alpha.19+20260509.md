# CapyOS 0.8.0-alpha.19+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha o próximo patch **Update/F5**: o payload declarado por
`payload_url` agora pode ser baixado por comando explícito, verificado com
SHA-256 real calculado no kernel e persistido como cache binário auditável.

A versão alinhada é `0.8.0-alpha.19+20260509`.

## Principais entregas

### SHA-256 real no kernel

- Novo módulo `include/security/sha256.h` e `src/security/sha256.c`.
- `Makefile` inclui `security/sha256.o` no kernel x86_64.
- `update-agent` usa `sha256_hash()` e `sha256_hex()` para verificar payload
  baixado contra `payload_sha256` do manifesto assinado.

### Download/cache de payload

- Nova API `update_agent_download_payload()`.
- Novo comando shell `update-download-payload`.
- Payload aceito é gravado como `/system/update/payload.bin`.
- Digest aceito é exposto como `payload_cache_sha256` no status e persistido em
  `/system/update/state.ini`.
- `update-stage` agora exige `payload_cache_sha256` compatível com
  `payload_sha256` antes de promover o manifesto.
- `update-status` imprime `payload-cache=... sha256=...`.
- `update-history` registra `payload_sha=` além de `payload=`.

### Rejeições seguras

- Sem update disponível: `no update payload available to download`.
- Writer binário ausente: `payload cache writer unavailable`.
- Manifesto de payload inconsistente: `payload manifest unavailable`.
- Buffer temporário indisponível: `payload download buffer unavailable`.
- Transporte/leitura falhou ou corpo inválido: `payload download failed`.
- SHA-256 real diferente do manifesto: `payload sha256 mismatch; cache refused`.
- Cache binário não persistido: `failed to persist payload cache`.
- Estado do cache não persistido: `failed to persist payload cache state`.
- Staging sem cache verificado: `payload cache missing or unverified for staging`.

## Regressões planejadas

`tests/test_update_agent.c` foi reforçado para revisar estaticamente:

- download válido do payload `abc` com SHA-256 conhecido;
- uso da URL declarada em `payload_url`;
- persistência de `/system/update/payload.bin`;
- persistência de `payload_cache_sha256` em `state.ini`;
- preservação do digest verificado após novo `update_agent_poll()`;
- recusa de mismatch SHA-256 antes de cache persistente;
- propagação de falha de transporte como `payload download failed`.

## Compatibilidade

- `payload_url` continua obrigatório e validado.
- `signature_ed25519` continua validando o manifesto antes do download.
- `update-apply <payload_sha256>` ainda exige digest explícito nesta etapa.
- Staging/apply totalmente automático a partir de `/system/update/payload.bin`
  fica para incremento posterior.
- HTTPS real ainda depende da conclusão F4/TLS no runtime; caminhos locais sob
  `/system/update/` seguem úteis para testes/smokes controlados.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- API pública e injeções `UNIT_TEST` do update-agent;
- SHA-256 real e comparação hex case-insensitive;
- limite `UPDATE_AGENT_PAYLOAD_MAX_BYTES` de 16 MiB e alocação dinâmica no runtime;
- writer binário do shell em sessão de sistema;
- persistência/limpeza de estado e cache;
- saídas `update-status`, `update-download-payload` e `update-history`;
- regressões planejadas de download válido, mismatch e transporte falho;
- documentação operacional, CLI, release-signing, STATUS e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Consumir `/system/update/payload.bin` no staging/apply automático.
- Adicionar smoke `smoke-x64-update-fetch` com payload local/HTTP controlado.
- Concluir TLS F4 para `payload_url` HTTPS real.
