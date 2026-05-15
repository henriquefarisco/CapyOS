# CapyOS — Implementação finalizada até 0.8.0-alpha.93

Este documento consolida entregas já finalizadas. Ele é histórico e não orienta
implementação nova sozinho. O plano ativo fica em
[`../active/capyos-master-plan.md`](../active/capyos-master-plan.md).

## Marco consolidado

- **Versão base:** `0.8.0-alpha.93+20260510`.
- **Estado:** base histórica congelada para replanejamento sequencial.
- **Regra:** itens abaixo não voltam ao plano ativo como tarefas abertas.

## Fundações do sistema

- Boot x86_64/UEFI, runtime kernel, VFS base, isolamento de processos,
  scheduler, syscalls, ELF userland, pipes e `capysh` entregues nas trilhas
  M0-M8/M4/M5 históricas.
- Segurança e robustez base entregues: stack protector na release,
  journaling/replay/fsck, logs persistentes, autenticação e rollback assistido.
- Layout e clean-code entraram como regra viva e gate de manutenção.

## Browser e UX histórica

- Browser ring-3 histórico foi entregue e consolidado em
  [`f3-browser-delivered.md`](f3-browser-delivered.md).
- UX W7-ish histórica foi entregue e consolidada em
  [`ux-w7-ish-delivered.md`](ux-w7-ish-delivered.md).
- Funcionalidades antigas de browser/UX ficam como referência técnica; a nova
  frente visual ativa passa a ser `CapyUI Shell Polish v1`.

## Release, assinatura e publicação

- Tooling Ed25519 de release, verificador, self-test negativo, fingerprint da
  chave pública, manifestos públicos e gates de publicação foram entregues.
- Handoff oficial, evidência pública pós-smoke e promoção pública pós-smoke
  foram documentados e versionados.
- Pendências operacionais de chave oficial/CI real permanecem no plano ativo,
  mas o histórico de tooling não volta como tarefa.

## Rede, HTTP e DNS

- Syscalls de socket, `libcapy-net`, TCP façade, DNS resolve, active DNS,
  TTL real e negative caching foram entregues.
- Cliente HTTP/1.1 userland foi endurecido contra CRLF, host authority ambígua,
  labels DNS inválidos, `Content-Length` inconsistente, EOF curto,
  `Transfer-Encoding`, `Content-Encoding`, status-line inválida, headers
  truncados e fragment leakage.

## libcapy-tls até alpha.93

- API TLS userland permanece fail-closed até existir backend real.
- `libcapy-tls` já possui política de hostname compartilhada, peer verification
  obrigatório, janela de timeout, configuração efetiva, contexto interno,
  slot gerenciado e backend stub.
- Trust anchors avançaram até catálogo, slots, descritores, manifesto v3,
  resumo de material, bundle metadata-only, backend plan e estado BearSSL
  reservado metadata-only.
- `0.8.0-alpha.93+20260510` adicionou o adaptador BearSSL userland
  metadata-only com fingerprint `0xE73E3E65`, `adapter_initialized=0` e
  `handshake_allowed=0`.
- Nenhuma etapa histórica inicializou BearSSL real ou executou handshake real.

## Update agent

- Manifestos locais/staged/importados/fetched rejeitam downgrade, exigem
  `payload_sha256`, `payload_url` válido e assinatura Ed25519.
- `update-fetch`, `update-download-payload`, `update-prepare`,
  `update-prepare-dry-run`, `update-prepare-explain`, `update-apply`,
  `update-confirm-health` e `update-rollback-check` foram documentados como
  entregues.

## GUI e CapyUI foundation

- Fundação de input observável, fila GUI, dispatcher central, dispatcher de
  janela, publicação de lifecycle, coalescência de eventos, overlays, prompts,
  menus, criação de usuário via Settings e hardening de foco/visibilidade foram
  entregues em incrementos históricos.
- Essas fundações sustentam a nova etapa ativa de polish visual estilo Ubuntu +
  Windows 7, mas não são reabertas como tarefas.

## Linux ABI parcial existente

- Já existem shims Linux ABI parciais para áreas como clock, random, VFS,
  devfs, procfs, tmpfs, futex, epoll, eventfd, signal storage, memfd, pidfd,
  clone validation, fanotify e outras superfícies usadas no planejamento de
  compatibilidade.
- O plano ativo reclassifica essa base como fundação parcial. O objetivo novo é
  `CapyLX`, uma personalidade Linux ABI sequencial, sem kernel Linux e sem
  perder a identidade do CapyOS.

## Arquivos arquivados nesta reorganização

- [`capyos-master-plan-legacy-through-alpha93.md`](capyos-master-plan-legacy-through-alpha93.md)
  preserva o master plan ativo antes da reorganização sequencial.
- [`capyos-status-legacy-through-alpha93.md`](capyos-status-legacy-through-alpha93.md)
  preserva o status executivo antes da remoção de itens concluídos do plano
  ativo.
