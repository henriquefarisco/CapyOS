# Registry de marcadores debugcon (porta 0xE9)

> Status: vivo (Etapa 2.f, 2026-05-02). Toda nova trilha que emitir
> bytes na porta 0xE9 deve adicionar suas tags aqui antes de subir o
> PR. Sem este registro, marcadores conflitam silenciosamente e o
> bisect post-mortem fica adivinhação.

A CapyOS usa a porta debug 0xE9 (debugcon do QEMU + Bochs) como
canal de telemetria fora-de-banda em código onde `printf`/console
não estão prontos ou onde o overhead de log estruturado é
inaceitável (boot precoce, hot-paths do compositor, etc.).

Boot QEMU com `-debugcon stdio` (ou `-debugcon file:capyos.log`) para
ver os bytes; o terminal serial do convidado **não** mostra o
debugcon — ele é um canal separado.

## Convenções gerais

* **Tag de 1 char delimitada por `[` `]`** — usada em hot-paths para
  bisetar por etapa. Ex.: `[O][1][2][3][4][5][K]` = browser_app abriu
  com sucesso (todas as etapas atingidas até `K`).
* **Linha prefixada `[<componente>]`** — usada em harness/smoke para
  log estruturado humanlegível. Ex.: `[browser-smoke] event FRAME w=480 h=360`.
* **Delimitadores `<` … `>` `\n`** — usados para encaminhar texto de
  outra origem (engine ring 3) sem confundir com markers nativos do
  ring 0. Bytes não-printáveis são substituídos por `?`.
* **`[T]`** — sufixo opcional indicando que o conteúdo foi truncado
  pelo cap defensivo do consumidor.

## Browser app (chrome ring 0) — `bapp_mark`

Origem: `src/apps/browser_app/browser_app.c` (helper
`debugcon_putc` + `bapp_mark`). Cada tag é emitida como `[X]`.

| Tag | Função onde sai          | Significado                                                     |
|-----|--------------------------|-----------------------------------------------------------------|
| `O` | `browser_app_open` entry | Início de open                                                  |
| `o` | `browser_app_open`       | Open chamado mas instância já está ativa (no-op)                |
| `1` | `browser_app_open`       | Pipe ops instaladas (chrome_runtime + yield)                    |
| `2` | `browser_app_open`       | `browser_engine_spawn` retornou OK                              |
| `!` | `browser_app_open`       | `browser_engine_spawn` falhou — open abortado                   |
| `3` | `browser_app_open`       | Janela compositor criada                                        |
| `?` | `browser_app_open`       | `compositor_create_window` falhou — engine derrubado            |
| `4` | `browser_app_open`       | Janela mostrada e focada                                        |
| `5` | `browser_app_open`       | URL bar pintada com home page                                   |
| `K` | `browser_app_open`       | Open concluído com sucesso (kept alive)                         |
| `D` | `browser_app_tick`       | Engine detectado morto antes do drain — fechando janela          |
| `h` | `browser_app_tick`       | Tentando enviar NAVIGATE da home pela 1ª vez                    |
| `H` | `browser_app_tick`       | NAVIGATE da home enviado                                         |
| `F` | `browser_app_tick`       | REPAINT_FRAME recebido — pixels copiados para a janela          |
| `Q` | `browser_app_tick`       | FETCH_REQUESTED recebido                                         |
| `q` | `browser_app_tick`       | Fetch despachado com sucesso                                    |
| `X` | `browser_app_tick`       | PROTOCOL_ERR ou ENGINE_EOF no poll — fechando                    |
| `W` | `browser_app_tick`       | Watchdog pediu kill ou broken pipe no tick — fechando            |

Forwards de log do engine usam o formato `<msg>` (ou `<msg[T]>` em
truncamento) — ver `browser_app_log_forward`.

## Browser smoke (boot harness) — linhas prefixadas

Origem: `src/kernel/browser_smoke.c`. Sempre emite linhas terminadas
em `\n` no formato `[browser-smoke] <evento>`.

| Linha                                         | Significado                                  |
|-----------------------------------------------|----------------------------------------------|
| `[browser-smoke] spawn pid=N req=R resp=W`    | Engine ring-3 spawned                        |
| `[browser-smoke] tasks-armed`                 | Poller kernel-side enfileirado               |
| `[browser-smoke] poller-start`                | Poller começou execução                      |
| `[browser-smoke] navigate-sent`               | NAVIGATE enviado pelo chrome                 |
| `[browser-smoke] event NAV_STARTED`           | Engine ack do NAVIGATE                       |
| `[browser-smoke] event LOG <msg>`             | LOG_FORWARD repassado do engine              |
| `[browser-smoke] event FRAME w=W h=H`         | EVENT_FRAME recebido                         |
| `[browser-smoke] event NAV_READY`             | Engine sinalizou READY                       |
| `[browser-smoke] event PONG`                  | Watchdog reconheceu PONG do engine           |
| `[browser-smoke] ping-sent`                   | Watchdog despachou PING                      |
| `[browser-smoke] shutdown-sent`               | SHUTDOWN enviado ao engine                   |
| `[browser-smoke] engine-eof`                  | Engine fechou pipe — fim ordenado            |
| `[browser-smoke] OK`                          | Smoke completo (todas as etapas)             |
| `[browser-smoke] FAIL <razao>`                | Smoke falhou — razão na linha                |

## Outras origens

| Componente / arquivo                         | Padrão / função                              | Observação                                  |
|----------------------------------------------|----------------------------------------------|---------------------------------------------|
| `src/util/debug.c`                           | `dbg_printf`/`dbg_putc`                      | Canal genérico kernel-wide                   |
| `include/util/debug.h`                       | macros `DBG_*`                               | Enable via flag de compilação                |
| `src/arch/x86_64/panic.c`                    | `panic_print` direto via 0xE9                | Última linha de defesa pré-halt              |
| `src/auth/login_runtime.c`                   | `[login]` prefix em strings                  | Diagnóstico de login                         |
| Drivers de storage (`ahci`, `nvme`, `ramdisk`) | `[ahci]`, `[nvme]`, `[ramdisk]` prefix     | Init e erros                                 |

## Como adicionar novos marcadores

1. Escolha um caractere ASCII printável (32–126) ainda **não usado**
   na tabela do componente. Se já estiver tomado, prefira mudar a
   semântica antes de criar uma tag duplicada.
2. Documente aqui antes de subir o PR. PRs que adicionam emitters
   novos sem entrada nesta tabela falham na revisão de Etapa N.
3. Em hot-paths, prefira tags single-char + `[X]`. Em harnesses,
   prefira linhas prefixadas (`[componente] mensagem`).
4. Para encaminhar texto vindo de outra origem (ex.: log do engine
   ring-3), use o formato `<msg>` e sanitize bytes não-printáveis
   para `?`. Aplique cap defensivo contra length corrompido.

## Convenções de truncamento

Quando o consumidor precisa cortar uma string longa (ex.:
`browser_app_log_forward` cap `BROWSER_CHROME_LOG_MSG_MAX = 192`),
emita `[T]` antes do delimitador final para que o operador veja
imediatamente que o conteúdo está incompleto.

Exemplo:

```text
<  [capybrowser] parsed N nodes title=...   [T]>
```

## Referências cruzadas

* `src/apps/browser_app/browser_app.c` — `debugcon_putc`, `bapp_mark`,
  `browser_app_log_forward`.
* `src/kernel/browser_smoke.c` — `bs_putc`, `bs_write`, `bs_write_dec`.
* `include/apps/browser_chrome.h` — `BROWSER_CHROME_LOG_MSG_MAX`.
* `include/apps/browser_dimensions.h` — dimensões compartilhadas.
