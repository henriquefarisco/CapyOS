# CapyOS 0.8.0-alpha.306+20260701

**Data:** 2026-07-01
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.306+20260701`
**Plataforma oficial:** VMware + UEFI + E1000
**Tipo:** correção crítica pós-teste real em VMware (Etapa 7 / Slice B)

## Resumo executivo

Teste real em VMware do `alpha.305` (via `open-browser-graphical`, em uma
sessão de desktop logada de verdade) travou o sistema (tela sólida, sem
diagnóstico, sem log de serial disponível). Sem dados de crash, a
investigação foi 100% estática, comparando o novo caminho de spawn com
TODOS os outros que já existiam no repositório.

**Causa raiz identificada:** `process_create` (varredura da tabela de
processos), `elf_load_into_process` (mutações de tabela de páginas /
memória física) e `scheduler_add` (inserção na fila do scheduler) **não têm
nenhum lock interno** em lugar nenhum do kernel. Isso nunca foi um problema
porque **todo outro chamador dessa sequência é boot-exclusivo**
(`kernel_boot_run_embedded_hello`/`_two_busy_users`/`_capysh`/
`_tls_handshake`/`_capybrowse`/`_capymultifetch`/`_capygfx`, todos em
`user_init.c`) e roda **antes** do timer preemptivo ser armado — nada mais
pode executar concorrentemente ainda, então a sequência é atômica por
construção. O único outro chamador de `process_create`, `sys_fork`
(`src/kernel/syscall.c`), chega lá através de um *syscall trap gate*, que
nesta arquitetura mascara IF durante a chamada.

`kernel_spawn_capygfx_desktop` (introduzida no `alpha.304`) é a **primeira**
chamada dessa sequência a rodar de dentro de uma sessão de desktop **já
viva**, com o scheduler preemptivo já batendo e interrupções habilitadas —
um comando de shell é código C de kernel comum, não um syscall gate. Sem
proteção, um IRQ de timer no meio da sequência pode entregar a tabela de
processos / tabelas de página / fila do scheduler **parcialmente
inicializadas** para o que quer que o scheduler escale a seguir,
corrompendo estado compartilhado — exatamente o tipo de trava sem
diagnóstico relatada.

**Correção:** o trecho crítico de `kernel_spawn_capygfx_desktop`
(`process_create` até `scheduler_add`) agora roda protegido por
`cli()`/`sti()` (salva/restaura `RFLAGS`, para o caso de um chamador futuro
já rodar com interrupções desabilitadas), tornando a sequência atômica
também neste novo caminho — mesma garantia que os outros chamadores já
tinham "de graça" por rodarem no boot.

## Sobre o segundo travamento relatado (comando `help`)

O operador também relatou o sistema travar ao rodar `help` no shell
gráfico "com os pacotes instalados", **em um boot novo e limpo, sem
relação com o teste do navegador gráfico**. Esta release **não** investiga
nem corrige esse segundo problema: nenhum código tocado nesta sessão
(Slice B) está no caminho de `help`/listagem de pacotes, e o sintoma foi
reproduzido de forma independente. Fica registrado como um problema
separado e não confirmado se é pré-existente ou uma regressão — precisa de
mais dados (idealmente um log de serial/COM1) para investigar.

## Mudancas

- `src/kernel/user_init.c`: `kernel_spawn_capygfx_desktop` agora executa `process_create` → `scheduler_add` dentro de uma seção crítica `cli()`/salva-RFLAGS / `sti()`/restaura-RFLAGS.

## Validacao

- `make test` — verde (sem regressão; a seção protegida só existe sob `CAPYOS_GFX_SMOKE`/`CAPYOS_DESKTOP_GRAPHICAL_BROWSER`, nunca compilada em host tests).
- **`make smoke-x64-qemu-capygfx-desktop-spawn` (regressão do mecanismo de spawn sintético) — PASSOU** após a correção.
- Novo artefato gerado para reteste em VMware: `make all64 PROFILE=full CAPYOS_DESKTOP_GRAPHICAL_BROWSER=1 EXTRA_USERLAND_CFLAGS='-DCAPYGFX_DESKTOP_INTERACTIVE'` + `iso-uefi` + `manifest64` — build limpo, ISO gerada.
- **Não validado ainda em VMware real** — esta correção é baseada em análise estática rigorosa (sem log de crash disponível); precisa do reteste do operador para confirmação definitiva.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.305+20260701` | `0.8.0-alpha.306+20260701` | Correção de condição de corrida real (kernel_spawn_capygfx_desktop sem proteção contra preempção); achada via teste real em VMware. Pendente de reteste do operador. |

_Build: `0.8.0-alpha.306+20260701`_
