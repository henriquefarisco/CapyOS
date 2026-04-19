# CapyOS 0.8.0-alpha.0+20260411

Data: 2026-04-11
Canal: develop
Base: consolidacao da trilha `UEFI/GPT/x86_64`

## Destaques

- desktop x64 ficou mais estavel e responsivo para uso basico
- trilha legada `BIOS/MBR/32-bit` foi removida da consolidacao atual
- suporte de drivers e runtime foi ampliado para a fase atual do projeto
- documentacao de roadmap, release e consolidacao foi alinhada com `develop`

## Mudancas tecnicas relevantes

- compositor e loop do desktop foram ajustados para reduzir redraw desnecessario
- fluxo x64 foi consolidado como caminho principal de build, boot e validacao
- componentes legados e scripts auxiliares antigos sairam da trilha ativa
- plano mestre de consolidacao passou a refletir o estado real das fases ja entregues

## Validacao recomendada

```bash
make all64
make test
```

## Observacoes

- esta nota registra a consolidacao em `develop`, sem promover nova versao semantica
- `main` continua sendo o canal de promocao posterior, apos nova rodada de validacao
