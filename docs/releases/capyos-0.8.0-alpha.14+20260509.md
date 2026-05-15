# CapyOS 0.8.0-alpha.14+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch incremental de **Update/F5**: o `update-agent` agora
consegue buscar o manifesto remoto configurado e reaproveita o gate local de
importação para validar trilha, versão, `payload_sha256` e `signature_ed25519`
antes de atualizar `/system/update/latest.ini`.

A versão alinhada é `0.8.0-alpha.14+20260509`.

## Principais entregas

### Fetch remoto de manifesto

- Nova API `update_agent_fetch_remote_manifest()`.
- Runtime normal baixa o `remote_manifest=` configurado usando `http_get()`.
- O texto baixado é persistido temporariamente em `/system/update/fetched.ini`.
- A importação final reutiliza `update_agent_import_manifest_path()`.
- O arquivo temporário é removido após aceite ou rejeição.

### Reuso dos gates existentes

O manifesto remoto só substitui o catálogo local se passar pelos mesmos gates já
exigidos para import manual:

- `channel`/`branch`/`source` compatíveis com a trilha selecionada;
- versão semanticamente mais nova que o sistema atual;
- `payload_sha256` hex64;
- `signature_ed25519` hex128 válida sobre texto canônico sem a linha de
  assinatura.

### Comando shell

- Novo comando `update-fetch`.
- O comando atualiza o estado do serviço `update-agent`.
- O evento `fetch` entra em `/var/log/update-history.log`.

### Testes planejados

`tests/test_update_agent.c` ganhou fetcher injetável sob `UNIT_TEST` para cobrir
por revisão estática:

- fetch remoto válido aceito e persistido no catálogo;
- assinatura remota inválida recusada;
- falha de transporte reportada como `remote manifest fetch failed`;
- URL remota construída a partir de `source`/`branch` selecionados;
- temporário `/system/update/fetched.ini` removido após import.

## Compatibilidade

- O build host não precisa linkar a pilha HTTP: sob `UNIT_TEST`, o fetcher é
  injetado.
- `update-import-manifest` continua disponível como caminho manual.
- `update-stage`, `update-arm` e apply verificado continuam dependentes do
  catálogo local já validado.
- Payload remoto e `update-apply` operacional seguem para incremento posterior.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- API pública em `include/services/update_agent.h`;
- implementação real via `http_get()` fora de `UNIT_TEST`;
- isolamento por hook `update_agent_set_manifest_fetcher()` em testes;
- reaproveitamento do import validado;
- comando shell `update-fetch` registrado;
- documentação CLI/operacional/plano/release alinhada.

## Próximos passos

- Baixar payload remoto declarado no manifesto.
- Implementar `update-apply` chamando `update_agent_apply_boot_slot_verified()`.
- Adicionar smoke `smoke-x64-update-fetch` com servidor HTTP local.
- Integrar TLS userland completo quando F4 entregar `libcapy-tls`.
