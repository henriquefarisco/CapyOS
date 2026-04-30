# Screenshots do CapyOS

Os screenshots oficiais do projeto ficam organizados por versao para permitir
comparacao visual da evolucao do sistema ao longo do tempo.

## Regra

- cada versao publica ganha uma pasta propria em `docs/screenshots/<versao>/`
- o README principal deve apontar para os prints da versao alpha/beta/stable
  atual
- snapshots antigos permanecem preservados como historico visual

## Estrutura atual

- `0.8.0-alpha.4/`
  - snapshots oficiais usados no README e na divulgacao atual; identicos aos
    da `0.8.0-alpha.3` porque o ciclo focou em robustez interna (op_budget
    deep adoption, eventos `[audit]`, verificacao SHA-256 do payload do
    update agent, round-trip sintetico do journal e split do update_agent)
    sem mudanca visual relevante
- `0.8.0-alpha.3/`
- `0.8.0-alpha.2/`
- `0.8.0-alpha.1/`
