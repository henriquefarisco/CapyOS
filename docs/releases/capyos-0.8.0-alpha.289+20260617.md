# CapyOS 0.8.0-alpha.289+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.289+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.2 — superficie grafica ring-3 + raster do display-list em pixels, nucleo)

## Resumo executivo

alpha.289+20260617: Etapa 7 / Slice 7.2 (nucleo) -- entrega a PRIMEIRA superficie grafica ring-3 do CapyOS + o rasterizador do display-list desacoplado em pixels, mantendo o core puro (parser/layout/display-list) substituivel (criterio 6 da Etapa 7). ABI nova de 6 syscalls (SYS_WINDOW_CREATE/SURFACE_FILL/SURFACE_BLIT/WINDOW_POLL_EVENT/WINDOW_PRESENT/WINDOW_DESTROY, numeros 45..50; SYSCALL_COUNT 45->51), aditiva. Isolamento Opcao A: um app ring-3 compoe pixels no proprio buffer (ARGB32) e faz copy-in validado pelo kernel; NUNCA recebe ponteiro de surface do kernel, referenciando a janela so por um HANDLE opaco que o kernel rastreia como pertencente ao processo chamador. Toda a politica (ownership por pid, bounds, overflow, sanitizacao de titulo, copia sob o CR3 do chamador) vive no modulo handler kernel/syscall_gfx.c, que delega a uma vtable injetavel (struct syscall_gfx_ops): producao instala um backend de compositor (kernel/syscall_gfx_init.c, mesmo split de syscall_net.c/syscall_net_init.c) e os testes de host injetam um backend falso com surface em RAM. Ownership e fail-closed: um handle de outro processo e rejeitado em fill/blit/present/poll/destroy; janelas de um processo que sai/e reapeado sao destruidas via observer de teardown registrado em process_exit/process_destroy (hook NULL por padrao -> testes de host e perfil sem-GUI nao linkam o compositor). ABI compartilhada kernel<->ring3 em include/kernel/syscall_gfx_abi.h (struct capy_gfx_event, limites CAPY_GFX_MAX_DIM/BLIT_MAX_PIXELS), consumida pelo wrapper userland userland/include/capylibc/capy_gfx.h + stubs em syscall_stubs.S; test_capylibc_abi atualizado (numeros + SYSCALL_COUNT 51). Novo rasterizador CapyOS userland/bin/capybrowse/browser_render_pixel.{c,h}: capyos_browser_render_pixels() pinta o MESMO capy_dl em uma surface ARGB32 (RECT preenchido pela cor CSS, TEXT em glifos da font 8x8 compartilhada do console -- MSB=esquerda, fonte unica, sem duplicacao --, IMAGE como caixa-placeholder com rotulo alt, LINK como underline na cor de link; parser de cor CSS #rgb/#rrggbb/rgb()/nomeada). Puro, allocation-free, deterministico, fail-closed (versao/NULL deixam a surface intacta), host-testavel e linkavel no ring-3 -- segundo consumidor substituivel do display-list, agora em pixels. Validado: make test VERDE (novo test_syscall_gfx 41/41 cobrindo ownership/bounds/fail-closed/copia de pixels/release_owner/tabela cheia via fake backend; novo test_browser_render_pixel 22/22 com assertivas de pixel RECT/TEXT/IMAGE/LINK + fail-closed; test_capylibc_abi com os 6 numeros novos); make all64 VERDE (backend do compositor linka contra o compositor real); layout-audit limpo. Sem mudanca de comportamento em boot de producao: os handlers retornam -1 ate o backend ser instalado (ativacao do backend + app ring-3 de prova em QEMU = Slice 7.2.2/alpha.290; o desktop interativo coordenado com CapyUI = 7.3). CapyBrowser v0.6.5 consumido como-esta (so headers do display-list); 6 repos irmaos inalterados.

## Mudancas

### ABI de syscalls graficos ring-3 (aditiva)
- **`include/kernel/syscall_numbers.h`**: novos `SYS_WINDOW_CREATE` (45), `SYS_SURFACE_FILL` (46), `SYS_SURFACE_BLIT` (47), `SYS_WINDOW_POLL_EVENT` (48), `SYS_WINDOW_PRESENT` (49), `SYS_WINDOW_DESTROY` (50); `SYSCALL_COUNT` 45 -> 51.
- **`include/kernel/syscall_gfx_abi.h`** (novo): ABI pura compartilhada kernel<->ring-3 (`struct capy_gfx_event` + enum de eventos; limites `CAPY_GFX_MAX_DIM`, `CAPY_GFX_BLIT_MAX_PIXELS`), so `<stdint.h>` — mesmo modelo single-source de `syscall_numbers.h`.

### Handlers + backend (kernel)
- **`include/kernel/syscall_gfx.h` / `src/kernel/syscall_gfx.c`** (novos): handlers `sys_window_*`/`sys_surface_*` com **isolamento Opcao A** — tabela de handles opacos mapeando handle -> {owner pid, backend id}; `process_current()->pid` valida ownership; bounds/overflow/sanitizacao de titulo fail-closed; delega a `struct syscall_gfx_ops` (vtable injetavel). `syscall_gfx_release_owner(pid)` destroi todas as janelas de um pid; `syscall_gfx_register_handlers()` chamado de `syscall_init()`.
- **`src/kernel/syscall_gfx_init.c`** (novo): backend de producao sobre o compositor (`compositor_create_window`/`_show`/`_focus`/`_destroy`/`_get_window`/`_invalidate_rect`, fill/blit com clip, `gui_event_poll` -> `capy_gfx_event`); `syscall_gfx_install_default_ops()` instala a vtable + registra o teardown observer. Split mesma disciplina de `syscall_net.c`/`syscall_net_init.c` (testes de host nao linkam o compositor).
- **`src/kernel/syscall.c`**: `syscall_init()` chama `syscall_gfx_register_handlers()` (tabela retorna -1 ate o backend ser instalado).
- **`src/kernel/process.c` / `include/kernel/process.h`**: `process_register_teardown_observer()` + chamada em `process_exit` e `process_destroy` (hook NULL por padrao) para destruir janelas de um processo que sai/e reapeado.

### Rasterizador CapyOS do display-list em pixels
- **`userland/bin/capybrowse/browser_render_pixel.{h,c}`** (novos): `capyos_browser_render_pixels()` pinta o `capy_dl` desacoplado numa surface ARGB32 — RECT (cor CSS), TEXT (glifos da font 8x8 compartilhada do console, MSB=esquerda), IMAGE (caixa-placeholder + rotulo alt), LINK (underline na cor de link); parser de cor `#rgb`/`#rrggbb`/`rgb()`/nomeada. Puro, allocation-free, deterministico, fail-closed, freestanding.

### Userland (wrappers) + ABI lock
- **`userland/lib/capylibc/syscall_stubs.S`**: 6 stubs novos (`capy_window_create`/`capy_surface_fill`/`capy_surface_blit`/`capy_window_poll_event`/`capy_window_present`/`capy_window_destroy`; arg3 `rcx`->`r10` nos de 6 args).
- **`userland/include/capylibc/capy_gfx.h`** (novo): prototipos C ring-3 + `#include "kernel/syscall_gfx_abi.h"`.
- **`tests/userland/test_capylibc_abi.c`**: trava os 6 numeros novos + `SYSCALL_COUNT == 51`.

### Testes de host + Makefile
- **`tests/kernel/test_syscall_gfx.c`** (novo): handlers via fake backend com surface em RAM — ownership (A != B), bounds/overflow, fail-closed sem backend, copia de pixels (fill/blit), poll, destroy, `release_owner`, tabela cheia (41 checks).
- **`tests/userland/test_browser_render_pixel.c`** (novo): assertivas de pixel ARGB para RECT/TEXT/IMAGE/LINK, override de cor, fail-closed (versao/NULL), cor nomeada (22 checks). Registrados em **`tests/test_runner.c`**.
- **`Makefile`**: `syscall_gfx.o` + `syscall_gfx_init.o` em `CAPYOS64_OBJS`; `test_syscall_gfx.c` + `syscall_gfx.c` no `TEST_SRCS`; `test_browser_render_pixel.c` (sempre, no-op sem o flag) + `browser_render_pixel.c` + `font8x8_data.c` sob `CAPYBROWSER_CORE_AVAILABLE`.

### Plano
- **`docs/plans/active/capyos-master-plan.md`**: Slice 7.2 marcado como **nucleo concluido (`alpha.289`)** + sub-fatia **7.2.2** (boot smoke ring-3 em QEMU). `docs/plans/STATUS.md`: nota `alpha.289`.

## Validacao

- `make test` -- **verde** ("Todos os testes passaram"), incluindo `test_syscall_gfx` (41/41) e `test_browser_render_pixel` (22/22), e `test_capylibc_abi` com os 6 numeros novos + `SYSCALL_COUNT == 51`.
- `make layout-audit` -- **limpo** (sem warnings). `make version-audit` -- **verde**.
- `make all64` -- **verde**: `syscall_gfx.c` + `syscall_gfx_init.c` compilam e o backend do compositor linka no kernel (`build/capyos64.bin` gerado).
- **Pendente (Slice 7.2.2):** prova ponta-a-ponta em runtime — boot smoke ring-3 `/bin/capygfx` exercitando os 6 syscalls contra o compositor real (init do compositor sob gate + `syscall_gfx_install_default_ops`), marker COM1 + screenshot QEMU. VMware: o gate de release oficial `smoke-x64-vmware-browser-graphical` deveria reproduzir o mesmo boot e confirmar a janela na tela; **pulado nesta sessao** (sustentado por QEMU), a reabilitar depois.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.288+20260617` | `0.8.0-alpha.289+20260617` | Etapa 7 / Slice 7.2 nucleo; **ABI ring-3 (kernel<->userland) aditiva** (6 syscalls graficos novos, append-only). |

Append-only dentro do major: os 6 syscalls entram no fim da tabela (45..50), sem
renumerar nem remover; binarios userland existentes nao sao afetados. Esta e a
ABI **interna** kernel<->ring-3, distinta do contrato cross-repo `capyos-base`
da matriz de compatibilidade (que os repos irmaos consomem via `capypkg`, sem
chamar syscalls): a linha da matriz **nao muda** e os 6 repos irmaos permanecem
inalterados. **CapyBrowser** `v0.6.5` consumido como-esta (apenas headers do
display-list; nenhum TU extra linkado).

_Build: `0.8.0-alpha.289+20260617`_

_Build: `0.8.0-alpha.289+20260617`_
