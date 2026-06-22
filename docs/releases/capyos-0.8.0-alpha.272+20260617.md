# CapyOS 0.8.0-alpha.272+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.272+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Robustez do resolver DNS (retransmissao) + correcao de doc

## Resumo executivo

Robustez do resolver DNS **ativo** (que ja existia e funciona desde a
Etapa 5 / F4 secao c parte 4/4): `net_stack_dns_resolve` passa a
retransmitir a query UDP em caso de perda, em vez de queimar a janela de
timeout inteira num unico envio. Tambem corrige um comentario obsoleto que
ainda descrevia o resolver como "follow-up". **A Etapa 6 NAO esta
fechada**; o fetch **remoto** de modulos/browsing depende de assets
publicados alcancaveis + verificador Ed25519 (P0), nao de codigo de kernel.

## Mudancas

- **Retransmissao DNS (`src/net/core/stack.c`):** DNS roda sobre UDP, entao
  a query ou a resposta pode ser descartada silenciosamente.
  `net_stack_dns_resolve` enviava a query **uma unica vez** e poll-ava ate
  o timeout (3 s); uma perda gastava a janela inteira sem retry. Agora
  reenvia a **mesma** query (mesmo `query_id`, entao uma resposta tardia a
  um envio anterior ainda casa) a cada `NET_DNS_RETRANSMIT_INTERVAL_MS`
  (1 s), ate `NET_DNS_MAX_RETRANSMITS` (2) vezes dentro do timeout do
  chamador -- espelhando o retransmit de SYN do TCP em
  `net/services/socket.c`. `net_dns_send_query` e re-chamavel e nao toca no
  estado da resposta, entao reenviar e seguro e o **caminho feliz**
  (resposta < 1 s) permanece byte-identico ao anterior.
- **Correcao de doc (`userland/lib/capylibc-net/capy_net_resolve.c`):** o
  comentario de cabecalho ainda afirmava que o kernel nao promovia um cache
  miss em query ativa ("the active resolver is a follow-up"). Isso e falso
  desde 2026-05-08: `SYS_DNS_RESOLVE` -> `syscall_dns_resolve_with_active`
  faz cache -> cache negativo -> query ativa (`net_stack_dns_resolve`) ->
  seed. Comentarios atualizados; sem mudanca de comportamento.

## Validacao

- `src/net/core/stack.c` compila limpo (objeto isolado).
- `make test` -- verde (sem regressao; mudanca aditiva, caminho feliz
  inalterado).
- Resolver ativo confirmado em runtime: `net-resolve example.com ->
  104.20.23.154` no smoke da ISO (pre-existente).
- `make version-audit` -- verde.

## Escopo pendente

- Fetch **remoto** de modulos/browsing: assets de release publicados e
  alcancaveis + verificador Ed25519 da CapyAgent (P0). Caminho offline ja
  funciona (alpha.271).
- Gates externos VMware da Etapa 6 (`smoke-x64-vmware-capybrowse-text` +
  `smoke-x64-vmware-apps-basic-roundtrip`).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.271+20260617` | `alpha.272+20260617` | Retransmissao DNS (robustez) + correcao de doc do resolver |

Sem mudanca de ABI base do kernel nem de contrato cross-repo; os 6 repos irmaos
permanecem nas versoes de `alpha.266` (sem bump).

_Build: `0.8.0-alpha.272+20260617`_
