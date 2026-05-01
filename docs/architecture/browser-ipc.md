# Browser IPC Protocol (F3.3a)

> **Status:** rascunho v0.1 (2026-05-01) — fase 3a do master plan F3.
> **Escopo:** define o contrato de mensagens entre o **browser chrome**
> (processo do compositor/desktop) e o **browser engine** (processo
> userland ring 3 isolado, binário `capybrowser`).
> **Fonte de verdade:** `include/apps/browser_ipc.h`. Este documento
> explica o porquê; o header é a especificação binária.

## 1. Motivação

Hoje o `html_viewer` roda **dentro do mesmo processo** do compositor:
parser, fetch, decode de imagens e render compartilham heap e thread.
Consequências:

- Um loop infinito ou OOM no parser derruba o desktop inteiro.
- Cancelamento depende de cooperação (op_budget). Não há limite duro.
- W3 (parser yield + timeout 30s) mitigou, mas não eliminou o risco.

F3 move o engine para um processo separado, comunicado por **dois pipes
M5** (request/response). Crash do engine não afeta o desktop. Watchdog
mata e reinicia o processo se exceder budget de tempo/memória.

## 2. Topologia

```
+----------------------------+         +---------------------------+
|   desktop (ring 3 ou       |  pipe   |   capybrowser (ring 3)    |
|   ring 0 transitorio)      | <-----> |                           |
|                            |  req    |   - parser HTML/CSS       |
|   src/apps/browser_chrome  |         |   - fetch HTTP/(F4: TLS)  |
|   - frame da janela        |  pipe   |   - decode PNG/JPEG       |
|   - URL bar, botoes        | <-----> |   - render para surface   |
|   - blita surface RGBA     |  resp   |   - cookies/history       |
|   - watchdog (heartbeat)   |         |                           |
+----------------------------+         +---------------------------+
       ^                                        |
       | render frame (surface RGBA)            |
       +----------------------------------------+
       (via shared scratch ring no pipe resp)
```

Decisão: **dois pipes simples** (request fd e response fd), não um único
duplex. Motivos:

- M5 já entrega pipes simples; não há `socketpair` ainda.
- Permite que o chrome leia eventos do engine enquanto envia comandos
  (sem deadlock circular).

## 3. Tipos de mensagem

Todas as mensagens são **frames TLV binários** prefixados por header
fixo. Big-endian de propósito (consistência com pipes); pode mudar para
little-endian se medirmos custo em x86_64 (default LE seria mais barato
mas estamos em sistema único hoje).

### 3.1 Header comum

```c
struct browser_ipc_header {
    uint16_t magic;       /* 0xCB1B "Capy Browser IPC Browser" */
    uint16_t kind;        /* enum browser_ipc_kind */
    uint32_t seq;         /* sequence number (monotonic per direction) */
    uint32_t payload_len; /* bytes que seguem no mesmo frame */
};
```

Tamanho fixo: 12 bytes. Sempre lido inteiro com `capy_read` antes do
payload.

### 3.2 Direção chrome → engine (request/comando)

| `kind` | Nome | Payload | Semântica |
|---|---|---|---|
| `0x01` | `BROWSER_IPC_NAVIGATE` | `url_len:u16` + `url:utf8` | Inicia navegação para URL. Cancela navegação anterior se houver. |
| `0x02` | `BROWSER_IPC_CANCEL` | vazio | Aborta navegação corrente. Engine emite `EVENT_NAV_CANCELLED`. |
| `0x03` | `BROWSER_IPC_BACK` | vazio | Volta no histórico. |
| `0x04` | `BROWSER_IPC_FORWARD` | vazio | Avança no histórico. |
| `0x05` | `BROWSER_IPC_RELOAD` | vazio | Recarrega URL atual. |
| `0x06` | `BROWSER_IPC_SCROLL` | `delta_y:i32` | Rola conteúdo. |
| `0x07` | `BROWSER_IPC_RESIZE` | `width:u16` + `height:u16` | Notifica novo tamanho da viewport. Engine deve emitir frame novo. |
| `0x08` | `BROWSER_IPC_CLICK` | `x:u16` + `y:u16` + `button:u8` | Click em coordenada (relativa à viewport). |
| `0x09` | `BROWSER_IPC_KEY` | `keycode:u32` + `mods:u8` | Tecla pressionada (find-in-page, etc.). |
| `0x0A` | `BROWSER_IPC_PING` | `nonce:u32` | Heartbeat do watchdog. Engine deve responder com `EVENT_PONG` em ≤ 1s. |
| `0x0B` | `BROWSER_IPC_SHUTDOWN` | vazio | Encerramento limpo. Engine deve fazer flush e sair com `exit(0)`. |

### 3.3 Direção engine → chrome (event/resposta)

| `kind` | Nome | Payload | Semântica |
|---|---|---|---|
| `0x81` | `BROWSER_IPC_EVENT_TITLE` | `title_len:u16` + `title:utf8` | Título da página atualizou. |
| `0x82` | `BROWSER_IPC_EVENT_NAV_STARTED` | `nav_id:u32` + `url_len:u16` + `url:utf8` | Navegação começou (após resolução de redirect). |
| `0x83` | `BROWSER_IPC_EVENT_NAV_PROGRESS` | `nav_id:u32` + `stage:u8` + `percent:u8` | Progresso (stage: fetch/parse/render). |
| `0x84` | `BROWSER_IPC_EVENT_NAV_READY` | `nav_id:u32` | Página renderizada com sucesso. |
| `0x85` | `BROWSER_IPC_EVENT_NAV_FAILED` | `nav_id:u32` + `reason_len:u16` + `reason:utf8` | Falha controlada (timeout, budget, parse error). |
| `0x86` | `BROWSER_IPC_EVENT_NAV_CANCELLED` | `nav_id:u32` | Navegação abortada por `CANCEL` ou navegação subsequente. |
| `0x87` | `BROWSER_IPC_EVENT_FRAME` | `nav_id:u32` + `width:u16` + `height:u16` + `stride:u32` + `pixels:rgba32[width*height]` | Frame renderizado (formato BGRA8888 little-endian, idêntico ao framebuffer). Pode ser dirty rect parcial em versões futuras. |
| `0x88` | `BROWSER_IPC_EVENT_CURSOR` | `cursor:u8` | Cursor sugerido (default/pointer/text). |
| `0x89` | `BROWSER_IPC_EVENT_PONG` | `nonce:u32` | Resposta ao `PING`. Watchdog usa para verificar liveness. |
| `0x8A` | `BROWSER_IPC_EVENT_LOG` | `level:u8` + `msg_len:u16` + `msg:utf8` | Log do engine para o klog do chrome. Encaminhado a `[browser:engine]` no klog. |

## 4. Fluxos canônicos

### 4.1 Navegação simples

```
chrome                                engine
  |                                     |
  | NAVIGATE("https://example.org") --> |
  | <-- EVENT_NAV_STARTED(nav=42)       |
  | <-- EVENT_NAV_PROGRESS(stage=fetch) |
  | <-- EVENT_NAV_PROGRESS(stage=parse) |
  | <-- EVENT_TITLE("Example Domain")   |
  | <-- EVENT_FRAME(nav=42, surface)    |
  | <-- EVENT_NAV_READY(nav=42)         |
```

Chrome blita o frame na janela ao receber `EVENT_FRAME`. Múltiplos
frames durante a mesma navegação são válidos (paint progressivo).

### 4.2 Cancelamento por timeout no chrome

```
chrome (após 30s sem NAV_READY)        engine
  |                                     |
  | CANCEL ----------------------------> |
  | <-- EVENT_NAV_CANCELLED(nav=42)     |
```

Se o engine não responde em ≤ 2s, o watchdog do chrome envia SIGKILL.

### 4.3 Watchdog

Chrome envia `PING(nonce=N)` a cada 5s. Espera `PONG(N)` em ≤ 1s.

- Se 1× falha: log warning.
- Se 2× consecutivas: kill + restart automático com mensagem
  `"Browser engine reiniciado por travamento"`.

Engine implementa o handler de PING como fast-path (não bloqueia em
parse/render).

### 4.4 Crash do engine

Pipe do chrome retorna `EOF` ou `EBADF` na próxima leitura. Chrome:

1. Loga `[browser] engine crashed (status=N)` no klog.
2. Mostra UI de erro com botão "Recarregar".
3. Em `Recarregar`: spawn novo `capybrowser` via `capy_fork+capy_exec`.

## 5. Restrições de tamanho e segurança

- `payload_len` máximo: `BROWSER_IPC_MAX_PAYLOAD = 1 MiB`. Frames maiores
  são fragmentados em N `EVENT_FRAME` com mesmo `nav_id` (não suportado
  na v0.1; viewport máx 1024×768×4 = 3 MiB cabe em 4 frames).
- Chrome **valida** `payload_len` contra o `kind` esperado antes de ler.
- URLs no payload são validadas pelo chrome (`http://` ou `https://`)
  antes de propagar para o engine. Engine **não confia** que a URL veio
  validada — revalida.
- Engine roda como usuário normal (UID do session owner), sem cap
  privilégios. Filesystem visível: read-only de `/usr/share/capybrowser`
  + read-write de `~/.cache/capybrowser`.

## 6. Versionamento

Magic `0xCB1B` é v0. Mudança de breaking layout incrementa para
`0xCB1C`. Chrome rejeita engines com magic incompatível durante o
handshake (primeiro frame após spawn deve ser `EVENT_LOG` com
`"capybrowser v=<sem>"` — informativo).

## 7. Implementação

| Arquivo | Papel |
|---|---|
| `include/apps/browser_ipc.h` | Estruturas, enums, constantes |
| `src/apps/browser_ipc/codec.c` | encode/decode + validação (compartilhado entre chrome e engine via build híbrido kernel+userland) |
| `src/apps/browser_chrome/` | Chrome (ring 0/compositor) |
| `userland/bin/capybrowser/` | Engine (ring 3) |
| `userland/lib/libcapy-browser-ipc/` | Versão userland do codec (link estático no engine) |

`codec.c` é deliberadamente **livre de dependências de kernel ou libc
do desktop**: só usa `<stdint.h>`, `<string.h>` e write-funcs injetadas.
Isso permite o mesmo arquivo C ser linkado nos dois processos.

## 8. Testes

Cobertura mínima na entrega de F3.3a:

- `tests/test_browser_ipc.c`:
  - Round-trip de cada `kind` (encode → decode → comparar).
  - Rejeição de magic incorreto.
  - Rejeição de `payload_len > MAX`.
  - Rejeição de `kind` inválido.

## 9. Próximas fases (referência)

- F3.3b: stub `capybrowser` que lê NAVIGATE e responde com frame
  estático colorido (1 sessão).
- F3.3c: migração do `html_viewer` atual para userland (2 sessões).
- F3.3d: watchdog + restart UI (1 sessão).
- F3.3e: smokes + estabilização (1 sessão).
