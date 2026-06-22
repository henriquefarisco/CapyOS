# CapyOS 0.8.0-alpha.268+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.268+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Endurecimento do exec userland ring-3 (Etapa 6)

## Resumo executivo

Dois fixes de kernel que permitem programas ring-3 com syscalls de rede e
pilhas grandes rodarem, descobertos ao trazer o caminho CapyBrowse Text apos o
CR3 fix do alpha.267. **A Etapa 6 NAO esta fechada**: os gates externos VMware
`smoke-x64-vmware-capybrowse-text` e `smoke-x64-vmware-apps-basic-roundtrip`
seguem pendentes.

## Root cause + fixes

- **FIX 1 (`src/arch/x86_64/process_user_mode.c`):** a entrada boot-direct em
  ring 3 nao passava pelo scheduler, entao `process_current()` ficava NULL.
  Resultado: `vmm_handle_page_fault` nao resolvia o address space (crescimento
  de pilha demand-paged escalava para fault fatal) e syscalls baseadas em FD
  nao tinham processo. Fix: setar a task corrente (`task_set_current` + estado
  RUNNING + `arch_sched_apply_kernel_stack`), igual ao scheduler/TWO_BUSY.
- **FIX 2 (`src/arch/x86_64/arch_sched_hooks.c`):** o topo da pilha de kernel
  era gravado no RSP de syscall (cpu-local) e no TSS RSP0 sem alinhamento de 16
  bytes. `kmalloc` nao garante 16B; um topo 8B-alinhado quebra a invariante
  SysV, e um handler que faz spill de SSE alinhado para local de pilha da `#GP`
  (`sys_connect` copia `struct sockaddr_in local` via `movaps`). Latente para
  todas as tasks; exposto pelo FIX 1. Fix: mascarar o topo para 16B.
- **`userland/bin/capybrowse/main.c`:** `resp`/`doc` movidos para `.bss` para
  encolher o frame de ~140 KiB do `main`.

## Validacao (host, QEMU+OVMF, exit=0)

- `make smoke-x64-hello-user` -- PASSA (markers + zero panics).
- Embedded `capybrowse` roda ring-3 ponta-a-ponta SEM faults (CR3 +
  task-corrente + alinhamento), chegando ao estagio de fetch HTTPS.
- `make version-audit` -- verde.

## Escopo pendente

- Gate QEMU `smoke-x64-qemu-capybrowse-text` (scaffold no branch
  `wip/etapa6-capybrowse-qemu`): falta apenas alcance de rede do guest ao
  endpoint sob SLIRP -- nao e mais bug de kernel.
- Gates externos VMware da Etapa 6 seguem pendentes.
- Assinatura Ed25519 (CapyAgent) segue P0.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.267+20260617` | `alpha.268+20260617` | Fixes de kernel exec userland ring-3 (Etapa 6) |

Sem mudanca de ABI base do kernel nem de contrato cross-repo; os 6 repos irmaos
permanecem nas versoes de `alpha.266` (sem bump).

_Build: `0.8.0-alpha.268+20260617`_
