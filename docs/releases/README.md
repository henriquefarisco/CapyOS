# Release Notes do CapyOS

Indice das release notes mantidas no repositorio.

## Release atual

- `capyos-0.8.0-alpha.4+20260429.md`
  - adocao profunda do `op_budget` no navegador, eventos `[audit]`
    estruturados (browser strict + capyfs journal auth + update payload),
    verificacao SHA-256 do payload no `update_agent`, round-trip sintetico
    de dirty-shutdown e split do `update_agent` em `update_agent_transact.c`
- `capyos-0.8.0-alpha.3+20260429.md`
  - fechamento do ciclo de robustez M5/M6/M8 com journal CAPYFS autenticado
    por volume, primitiva `op_budget` reutilizavel, API de privilegios
    centralizada, pacing do buffer cache, parse-budget no navegador e modo
    estrito de transicoes
- `capyos-0.8.0-alpha.2+20260424.md`
  - avanco do plano de robustez com DHCP automatico, gates de release
    endurecidos, metricas de performance, caches DNS/HTTP e smoke x64
    persistente validado
- `capyos-0.8.0-alpha.1+20260423.md`
  - refresh completo da trilha alpha com browser Fase 1 estabilizado,
    reorganizacao estrutural do codigo, snapshots versionados e documentacao
    alinhada
- `capyos-0.8.0-alpha.0+20260419.md`
  - correcao critica do checksum TCP, retransmissao de SYN, redirect HTTP,
    diagnostico e UX do `net-fetch` e do navegador HTML interno
- `capyos-0.8.0-alpha.0+20260411.md`
  - consolidacao em `develop` com desktop x64 estabilizado, trilha 32-bit removida,
    ampliacao de drivers e revisao da documentacao
- `capyos-0.8.0-alpha.0.md`
  - consolidacao do nome CapyOS, trilha oficial x64, auditoria de disco e
    reorganizacao completa da documentacao

## Historico recente

- `capyos-0.7.3-alpha.1.md`
- `capyos-0.7.3-alpha.0.md`
- `capyos-0.7.2-alpha.0.md`
- `capyos-0.7.1-alpha.1.md`
- `capyos-0.7.0-alpha.1.md`

## Nota

As releases anteriores a `0.8.0-alpha.0` pertencem ao periodo de transicao
entre os fluxos legados e a trilha atual `UEFI/GPT/x86_64`. Elas podem citar
identificadores tecnicos antigos de boot e layout de disco. Leia essas notas
como historico, nao como guia de setup atual.
