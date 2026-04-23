# CapyOS 0.8.0-alpha.1+20260423

Data: 2026-04-23
Canal: develop
Base: consolidacao continua da trilha `UEFI/GPT/x86_64`

## Destaques

- browser interno fecha a Fase 1 de estabilizacao e passa a falhar de forma
  muito mais previsivel
- arvore de fontes recebe reorganizacao estrutural ampla, com modulos e
  linguagens melhor segmentados
- documentacao, README, screenshots e manifesto de versao ficam alinhados com
  o estado real do sistema

## Navegador

- `html_viewer` agora tem estados formais de navegacao, `navigation_id`,
  `final_url`, `redirect_count`, `safe_mode`, `last_stage` e
  `last_error_reason`
- redirects, cancelamento, limites de recursos e degradacao controlada foram
  consolidados
- respostas JS/JSON nao devem mais aparecer como texto bruto do documento
- base de codigo do navegador foi modularizada para preparar Fase 2:
  isolamento por processo, watchdog e pipeline mais robusto

## Organizacao do codigo

- modulos grandes foram reorganizados para reduzir monolitos e fronteiras
  ambiguas
- `src/arch/x86_64/` deixa de misturar Assembly e C na raiz para os pontos
  principais de boot/CPU/syscall
- `html_viewer`, `css_parser`, `first_boot` e outros blocos passam a seguir o
  padrao de modulos reais compilados

## Documentacao e versao

- `README.md` atualizado para refletir a nova alpha
- screenshots oficiais agora ficam em `docs/screenshots/0.8.0-alpha.1/`
- `VERSION.yaml` e `include/core/version.h` alinhados com
  `0.8.0-alpha.1+20260423`

## Validacao

```bash
make test
make layout-audit
make all64
```
