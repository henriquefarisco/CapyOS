# Plano de implantacao do MVP do NoirOS

Objetivo: entregar um MVP operavel do NoirOS com entrada de teclado confiavel (US e BR-ABNT2), CLI utilizavel, fluxos de instalacao/login estaveis e trilhas claras para evolucao segura. O plano segue gitflow (feature branches + PRs para `develop`), e cada etapa lista entregaveis, qualidade e riscos.

## Fase 0 - Fundacao de engenharia
- Definir Definition of Done: testes de host (`make test`), boot em QEMU (`make run-disk`), revisao entre pares, changelog e checklists de rollout.
- Instrumentar logs minimos: teclado (layout aplicado, dead keys ativados), CLI (hotkeys acionadas), instalador (erros de particao/NoirFS).
- Automatizar builds locais: alvo `make ci` empacotando `make clean && make test && make run-disk` em modo headless, com artefatos e logs em `build/ci/`.

## Fase 1 - Entrada e UX do terminal
- Corrigir e validar layouts: dead keys (`'`, `` ` ``, `~`, `^`, `"`), tecla extra ABNT2 (`/?`), separador decimal do keypad em `,` e hotkey F1 para `help-docs`. Cobrir com testes de layout host e smoke manual em QEMU.
- UX do prompt: manter `tty_show_prompt()` sincronizado com hotkeys; limpar eco parcial quando comandos forem injetados (F1) e garantir queue unica de linhas.
- Documentacao acessivel: publicar `docs/noiros-cli-reference.(md|txt)` na imagem; alinhar `help-docs` + F1 para apontar para o mesmo caminho.

## Fase 2 - Confiabilidade e seguranca
- Hardening do teclado: tratar prefixos `0xE0` (setas, keypad sem NumLock), debouncing simples e reset de dead key ao trocar layout.
- Sessao/CLI: isolar contexto de sessao no shell, impedir hotkeys de atuarem durante instalacao/login; validar `config-keyboard` contra layouts registrados.
- Seguranca basica: auditar manipulacao de buffers do TTY (limites e mascaras), revisar chamadas de VFS em comandos de ajuda e I/O para evitar estouros/IOCTL nao tratados.

## Fase 3 - Entrega, rollout e operacao
- Artefatos de release: ISO do instalador, imagem de disco de dev e checksum; changelog detalhado com riscos conhecidos e passos de rollback.
- Testes de regressao: suite de layout (host) + smoke (boot, login, CLI hotkeys, digitar acentos, keypad), script de execucao unica para QEMU (logs serial + captura de tela opcional).
- Observabilidade de campo: rotinas para despejar buffer de log do teclado/CLI em `/var/log/input.log` on-demand; comando `diag-input` (planejado) para testar acentos e numpad ao vivo.

## Diretrizes de execucao
- Cada feature em branch dedicada (`feature/teclado-abnt2`, `feature/hotkey-f1`), PR para `develop` com checklist (testes, logs, docs).
- Releases datadas em `releases/<versao>.md` com validacoes e passos de validacao.
- Gate de qualidade: nenhum merge sem testes verdes e validacao manual em VM (layout BR-ABNT2 + US).
