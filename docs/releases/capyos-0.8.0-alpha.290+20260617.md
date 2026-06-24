# CapyOS 0.8.0-alpha.290+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.290+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.2.2 — prova de runtime em QEMU da superficie grafica ring-3)

## Resumo executivo

alpha.290+20260617: Etapa 7 / Slice 7.2.2 -- prova ponta-a-ponta EM RUNTIME (QEMU) da superficie grafica ring-3 entregue (host+build) na 7.2/alpha.289. Novo app ring-3 gated /bin/capygfx (userland/bin/capygfx/main.c), auto-contido (so capylibc; SEM dependencia do CapyBrowser, para isolar a ABI grafica), que: cria uma janela via SYS_WINDOW_CREATE, preenche com SYS_SURFACE_FILL, compoe um ARGB32 determinista no proprio buffer .bss e o blita com SYS_SURFACE_BLIT, apresenta com SYS_WINDOW_PRESENT e faz poll nao-bloqueante com SYS_WINDOW_POLL_EVENT -- saindo 0 so se TODOS os 6 syscalls graficos retornarem sucesso contra o compositor REAL do kernel. Wiring de boot (mesmo padrao de tls_smoke/capybrowse): registro em embedded_progs.c (#ifdef CAPYOS_GFX_SMOKE, simbolos _binary_capygfx_elf_*), hook kernel_boot_run_capygfx em user_init.c, e um ramo CAPYOS_GFX_SMOKE em kernel_main.c que -- como o caminho login/desktop (que normalmente sobe o compositor) e desviado -- inicializa o compositor sobre o framebuffer de boot (compositor_init(g_con.fb,...)) e instala o backend dos syscalls graficos (syscall_gfx_install_default_ops) ANTES de spawnar o app. Quarteto de smoke marker (mesmo padrao dos gates Etapa 4/5/6): include/kernel/capygfx_smoke.h + src/kernel/capygfx_smoke.c (latch puro) + capygfx_smoke_io.c (emite o marker COM1 [smoke] capygfx ready) + tests/kernel/test_capygfx_smoke_gate.c (8 checks host) + tests/stubs/stub_capygfx_smoke_io.c; process_exit faz o latch sob o gate, e o observer de teardown (Slice 7.2) destroi a janela do app que sai. Makefile: regras capygfx ELF/blob (decoupladas, sem gate CapyBrowser; blob embarcado em CAPYOS64_OBJS so sob CAPYOS_GFX_SMOKE), capygfx_smoke.o/_io.o nos objs do kernel, triple de teste do gate, e os alvos make smoke-x64-qemu-capygfx (harness generico smoke_x64_qemu_marker.py) + make smoke-x64-vmware-browser-graphical (gate oficial VMware MAPEADO + documentado de como testar, PULADO neste ciclo e sustentado por QEMU conforme instruido). Validado: make test VERDE (novo test_capygfx_smoke_gate 8/8 + sem regressoes), make capygfx-elf linka o app ring-3, layout-audit limpo, e make smoke-x64-qemu-capygfx PASSOU em QEMU+OVMF (booto direto no app, marker [smoke] capygfx ready observado no COM1, smoke passou) -- a primeira prova de runtime de um app ring-3 desenhando atraves do compositor do CapyOS. Sem mudanca de ABI (a ABI dos 6 syscalls e a do alpha.289); o backend so e ativado sob o gate de smoke (ou, futuramente, no bring-up do desktop coordenado com CapyUI = Slice 7.3). Nota de present: no smoke nao ha loop de desktop, entao win_present apenas invalida (sem render ao framebuffer) -- o marker COM1 (saida 0 do app apos os 6 syscalls) e a prova; o render visual ao vivo + screenshot ficam para o gate VMware/7.3. CapyBrowser v0.6.5 e os 6 repos irmaos inalterados.

## Mudancas

### App ring-3 de prova
- **`userland/bin/capygfx/main.c`** (novo): app gated auto-contido (so capylibc; sem CapyBrowser) que cria janela, preenche (2x `SYS_SURFACE_FILL`), compoe um ARGB32 determinista no proprio `.bss` e o blita (`SYS_SURFACE_BLIT`), apresenta (`SYS_WINDOW_PRESENT`) e faz poll (`SYS_WINDOW_POLL_EVENT`); sai 0 so se todos retornarem sucesso, senao imprime diagnostico e sai 1.

### Wiring de boot (gate CAPYOS_GFX_SMOKE)
- **`src/kernel/embedded_progs.c`**: registro de `/bin/capygfx` (`#ifdef CAPYOS_GFX_SMOKE`, simbolos `_binary_capygfx_elf_*`).
- **`src/kernel/user_init.c` / `include/kernel/user_init.h`**: `kernel_boot_run_capygfx()` (spawn; mesma forma de `kernel_boot_run_capybrowse`).
- **`src/arch/x86_64/kernel_main.c`**: ramo `CAPYOS_GFX_SMOKE` que inicializa o compositor sobre o framebuffer de boot (`compositor_init(g_con.fb, ...)`) + instala o backend (`syscall_gfx_install_default_ops`) ANTES de spawnar `/bin/capygfx` (o caminho login/desktop, que normalmente sobe o compositor, e desviado). Includes `gui/compositor.h` + `kernel/syscall_gfx.h` gated.

### Quarteto de smoke marker
- **`include/kernel/capygfx_smoke.h` + `src/kernel/capygfx_smoke.c`** (latch puro) + **`src/kernel/capygfx_smoke_io.c`** (marker COM1 `[smoke] capygfx ready`) + **`tests/kernel/test_capygfx_smoke_gate.c`** (8 checks) + **`tests/stubs/stub_capygfx_smoke_io.c`**; `process_exit` faz o latch sob o gate (o observer de teardown da Slice 7.2 destroi a janela do app que sai). Registrado em **`tests/test_runner.c`**.

### Makefile + alvos de smoke
- Regras `capygfx` ELF/blob (decoupladas; blob em `CAPYOS64_OBJS` so sob `CAPYOS_GFX_SMOKE`); `capygfx_smoke.o`/`_io.o` nos objs do kernel; triple de teste do gate; alvos **`smoke-x64-qemu-capygfx`** (harness `smoke_x64_qemu_marker.py`) e **`smoke-x64-vmware-browser-graphical`** (gate VMware oficial **mapeado + documentado**, pulado neste ciclo).

### Plano
- **`docs/plans/active/capyos-master-plan.md`**: Slice 7.2.2 marcado **CONCLUÍDO (`alpha.290`)**. `docs/plans/STATUS.md`: nota `alpha.290` (e correcao da linha de versao executiva, que estava defasada em 287 por um descompasso de cache do mount SMB nos bumps 288/289 — VERSION.yaml/version.h sempre estiveram corretos).

## Validacao

- `make test` -- **verde** ("Todos os testes passaram"), incluindo `test_capygfx_smoke_gate` (8/8) e sem regressoes.
- `make capygfx-elf` -- **linka** o app ring-3. `make layout-audit` -- **limpo**. `make version-audit` -- **verde** (current=0.8.0-alpha.290).
- `make smoke-x64-qemu-capygfx` -- **PASSOU** em QEMU+OVMF: boot direto no `/bin/capygfx`, marker `[smoke] capygfx ready` observado no COM1, "qemu-marker smoke passed". Primeira prova de runtime de um app ring-3 desenhando atraves do compositor do CapyOS.
- **VMware (oficial):** `make smoke-x64-vmware-browser-graphical` mapeado (mesma imagem) e **pulado** neste ciclo conforme instruido (sustentado por QEMU; QEMU = feedback de dev, VMware = aceite oficial). Como testar ao reabilitar: build sob `CAPYOS_GFX_SMOKE`, boot da ISO em VMware+UEFI+E1000, confirmar o marker COM1 (e, quando a 7.3 ligar o loop do compositor, o render visual ao vivo).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.289+20260617` | `0.8.0-alpha.290+20260617` | Slice 7.2.2: prova de runtime (QEMU) da ABI grafica ring-3 do alpha.289. **Sem mudanca de ABI.** |

Sem mudanca de ABI nem de contrato cross-repo: os 6 syscalls graficos sao os do
alpha.289; o backend so e ativado sob o gate de smoke (futuramente no bring-up do
desktop coordenado com CapyUI = Slice 7.3). **CapyBrowser** `v0.6.5` e os 6 repos
irmaos permanecem inalterados.

_Build: `0.8.0-alpha.290+20260617`_

_Build: `0.8.0-alpha.290+20260617`_
