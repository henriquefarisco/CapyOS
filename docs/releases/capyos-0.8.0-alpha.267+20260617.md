# CapyOS 0.8.0-alpha.267+20260617

**Data:** 2026-06-17 | **Canal:** alpha (experimental) | **Plataforma oficial:** VMware + UEFI + E1000
**Tipo:** Correcao de kernel -- exec userland ring-3 (desbloqueio da Etapa 6)

## Resumo

Corrige o bloqueador central de execucao de programas de usuario (ring-3).
`process_enter_user_mode` (`src/arch/x86_64/process_user_mode.c`) entrava em
ring 3 sem trocar o CR3 para o address space do processo; o caminho boot-direct
nao passa pelo context switch do scheduler, entao o CR3 ficava no kernel AS
(huge supervisor de identidade sem a imagem ELF do usuario) -> `#PF` de
instruction-fetch em `_start` (err 0x15, present+user) em TODO programa.
FIX: `vmm_switch_address_space(proc->address_space)` antes da transicao.
FIX 2 (`kernel_main.c`): marcador de spawn do `user_init` movido para o caminho
vivo (a chamada real e `noreturn`; o marcador anterior era dead code).
Commit d759900.

## Validacao (host, QEMU+OVMF, exit=0)

- `make smoke-x64-hello-user TOOLCHAIN64=host` PASSA:
  `[user_init] CAPYOS_BOOT_RUN_HELLO defined;` + `hello, capyland`, zero panics, <=30s.
- `make test` e `make version-audit` verdes.

## Pendente

Etapa 6 NAO fechada: gates externos VMware `smoke-x64-vmware-capybrowse-text` e
`smoke-x64-vmware-apps-basic-roundtrip` pendentes (QEMU = feedback de dev).
Sem mudanca de ABI base do kernel nem de contrato cross-repo (6 irmaos sem bump).
Assinatura Ed25519 (CapyAgent) segue P0.

_Build: `0.8.0-alpha.267+20260617`
