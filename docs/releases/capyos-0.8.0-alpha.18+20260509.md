# CapyOS 0.8.0-alpha.18+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um novo patch **Update/F5**: manifestos assinados passam a
carregar uma origem explícita de payload (`payload_url`) antes do download
automático do payload remoto.

A versão alinhada é `0.8.0-alpha.18+20260509`.

## Principais entregas

### Origem de payload no manifesto

- `payload_url=` passa a ser parseado pelo `update-agent`.
- Manifestos novos só expõem update quando `payload_sha256`, `payload_url` e
  `signature_ed25519` estão válidos.
- `payload_url` aceita URLs `https://...` ou caminhos locais sob
  `/system/update/`.
- Espaços, quebras de linha e `..` são recusados.

### Status e auditoria

- `struct system_update_status` expõe `payload_url` e `staged_payload_url`.
- `update-status` imprime `payload=` e `staged-payload=`.
- `update-fetch` imprime a origem declarada do payload após aceitar o manifesto.
- `update-stage` imprime a origem preservada no staged update.
- `/var/log/update-history.log` passa a registrar `payload=` para eventos de
  update quando o catálogo ou staging possuem origem declarada.

### Erros estáveis

- Catálogo local mais novo sem `payload_url` válido retorna summary
  `catalog cache missing or malformed payload url`.
- Staged update sem `payload_url` válido retorna summary
  `staged update missing or malformed payload url`.
- Import manual/fetch remoto sem `payload_url` válido retorna summary
  `imported manifest missing or malformed payload url`.

## Regressões planejadas

`tests/test_update_agent.c` e `tests/test_update_transact.c` foram alinhados para
revisar estaticamente:

- propagação de `payload_url` do catálogo para o status;
- preservação de `staged_payload_url` no staging;
- import/fetch válidos com `payload_url`;
- rejeição de `payload_url` ausente, `http://` e caminhos com `..`;
- compatibilidade dos fluxos transacionais de apply verificado.

## Compatibilidade

- `payload_sha256` continua sendo o digest de integridade obrigatório.
- `payload_url` ainda não é baixado automaticamente nesta etapa.
- O operador ainda fornece o digest real ao `update-apply <payload_sha256>`.
- A assinatura Ed25519 continua cobrindo o texto canônico do manifesto sem a
  própria linha `signature_ed25519=`.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- parser e validação `payload_url` no `update-agent`;
- propagação em `system_update_status`;
- saídas `update-status`, `update-fetch` e `update-stage`;
- registro `payload=` em `/var/log/update-history.log`;
- regressões planejadas de catálogo, staging, import e fetch;
- documentação operacional, CLI, release-signing, STATUS e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Baixar payload remoto declarado por `payload_url`.
- Validar SHA-256 real do payload baixado antes de staging/apply automático.
- Adicionar smoke `smoke-x64-update-fetch` com servidor HTTP local em QEMU.
