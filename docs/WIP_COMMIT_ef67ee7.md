# WIP Commit ef67ee7

Data: 2025-10-09
Branch: feature/noiros-cli-bootstrap
Commit: ef67ee7c1e3f4478ca1e86325077f8f11ab06744
Autor: Henrique Schwarz Souza Farisco

## Objetivo

Salvar o estado parcial do sistema — implementação em progresso do CLI e vários módulos do kernel — para armazenamento remoto e checkpoints durante o desenvolvimento.

## Alterações principais

Resumo geral:
- Múltiplos arquivos do kernel foram adicionados e modificados para integrar um shell/CLI inicial (NoirOS CLI).
- Novos headers e fontes para `session`, `shell`, `system_init` e `user` foram criados.
- Documentação básica do CLI adicionada em `docs/`.

Arquivos criados (10):
- `docs/cli_test_plan.md`
- `docs/noiros-cli-reference.md`
- `include/session.h`
- `include/shell.h`
- `include/system_init.h`
- `include/user.h`
- `src/session.c`
- `src/shell.c`
- `src/system_init.c`
- `src/user.c`

Arquivos modificados (resumo):
- `Makefile`, `README.md`
- Includes: `include/crypt.h`, `include/isr.h`, `include/keyboard.h`, `include/noirfs.h`, `include/vfs.h`
- src: `src/crypt.c`, `src/interrupts.asm`, `src/interrupts.s`, `src/isr.c`, `src/kernel.c`, `src/keyboard.c`, `src/noirfs.c`, `src/vfs.c`

## Pontos conhecidos / Trabalho restante

- A CLI está em estágio inicial (bootstrap); comportamentos ainda incompletos e casos de uso não implementados.
- Testes formais ausentes; é recomendado criar um plano de testes automatizado para integração do CLI com o kernel.
- Possíveis melhorias de segurança e validações de entrada do usuário ainda necessárias.

## Como reverter este checkpoint

Para desfazer o commit localmente:

```bash
# Reverter o commit, mantendo as mudanças no working directory
git reset --soft ef67ee7^ 
# ou desfazer completamente (perda de mudanças locais)
# git reset --hard ef67ee7^
```

## Notas adicionais
- Commit criado a partir de trabalho local para preservar progresso antes de mudanças experimentais subsequentes.
- Use tags temporárias se quiser criar checkpoints nomeados (ex.: `git tag wip-20251009 ef67ee7`).
