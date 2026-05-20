# CapyOS 0.8.0-alpha.241+20260519

Release alpha `0.8.0-alpha.241+20260519` que conclui a higienização
end-to-end do circuito de instalação modular iniciado em alpha.239
e continuado em alpha.240. O sistema agora abre um wizard interativo
no primeiro boot que pergunta tudo (incluindo módulos a instalar),
mostra progresso de download e reinicia para um sistema customizado.

## Destaques

- **Wizard interativo end-to-end no primeiro boot.** TUI no
  framebuffer coleta idioma, teclado, hostname, tema, splash, usuário
  admin, senha admin **E** perfil de módulos (BASIC | FULL | CUSTOM).
- **Progress callback no bootstrap.** Novo
  `capypkg_bootstrap_run_with_progress` emite eventos de
  `REPO_REGISTER` → `INDEX_FETCH` → `PACKAGE_BEGIN/OK/FAIL/SKIP` →
  `SWEEP_DONE` para a UI mostrar `[i/N] instalando <nome>...` enquanto
  os bytes fluem.
- **Reboot automático após install.** Wizard chama `acpi_reboot` se
  módulos foram instalados, para o segundo boot ativar o desktop
  recém-baixado via gate de ativação.
- **Activation gate em `kernel/module_gate.c`.** Desktop session só
  inicia se `/var/capypkg/org.capyos.ui.desktop-session/installed`
  presente; comando shell mostra mensagem clara em vez de tentar
  iniciar desktop ausente.
- **Sources migram fisicamente para CapyUI 0.7.0.**
  `src/gui/desktop/`, `src/gui/window/` e `src/apps/` agora vivem em
  `CapyUI/src/{desktop,window,apps}/`. Script
  `tools/scripts/migrate_to_capyui.py` automatiza a movimentação.
  CapyUI ganha segundo módulo capypkg
  `org.capyos.ui.desktop-session` (depends widget-core).
- **CapyOS Makefile cross-repo.** `CAPYUI_DIR` autodetect via
  `realpath ../CapyUI`; pattern rules `capyui-desktop/`,
  `capyui-window/`, `capyui-apps/` compilam direto do sibling repo.
  Nova flag `PROFILE=full|core-only` permite kernel ELF mínimo.
- **Installer UEFI minimalista.** Removidos ~310 linhas de prompts
  (language, keyboard, hostname, theme, splash, admin user/password).
  Mantém só seleção de disco + geração da chave de recovery + confirmação.
- **Comando `capy` unificado.** `capy install`, `capy module
  list/status`, `capy wizard [--modules]`, `capy update`, `capy
  help` — slot 39 do registry capysh.
- **Silent provisioning eliminado.** Bloco de ~200 linhas em
  `kernel_shell_runtime.c` removido; o wizard interativo é agora a
  única fonte de truth para a configuração inicial.

## Mudanças por subsistema

### Wizard / first-boot

- `src/config/first_boot/modules.c` (novo TU): `first_boot_module_selection_step`
  com perfil BASIC|FULL|CUSTOM, integra `install_profile_format` +
  `capypkg_bootstrap_run_with_progress` + reboot.
- `src/config/first_boot/program.c`: chama o módulo + reboot;
  `first_boot_setup_impl` simplificado.
- `src/config/internal/first_boot_internal.h`: declara o novo step.

### Bootstrap

- `include/services/capypkg_bootstrap.h`: enum
  `capypkg_bootstrap_event`, typedef `capypkg_bootstrap_progress_fn`,
  nova função `capypkg_bootstrap_run_with_progress(force, *installed,
  *failed, progress, ctx)`.
- `src/services/capypkg_bootstrap.c`: refatorado, contagem deterministica
  de "planned packages" em primeira passada antes de emitir BEGIN/OK/FAIL.

### Install profile

- `include/services/install_profile.h`: declara `install_profile_format`.
- `src/services/install_profile.c`: implementa serializer round-trip
  com gate de printable-ASCII; 4 novos testes host-side cobrem
  basic/full/custom round-trip + buffer pequeno.

### Activation gate

- `include/kernel/module_gate.h` + `src/kernel/module_gate.c` (novos):
  `kernel_module_desktop_session_available`,
  `kernel_module_widget_core_available`,
  `kernel_module_is_installed(canonical_name)`.
- `src/shell/commands/extended.c`: `ensure_desktop` e `cmd_desktop_start`
  consultam o gate antes de iniciar; em `PROFILE=core-only` todos os
  comandos GUI viram stubs `cmd_desktop_unavailable`.

### Build

- `Makefile`: PROFILE flag (full|core-only), CAPYUI_DIR autodetect,
  pattern rules `capyui-desktop/`, `capyui-window/`, `capyui-apps/`,
  DESKTOP_OBJS/WINDOW_OBJS/APPS_OBJS condicionais ao perfil, fallback
  para src/gui/desktop|window e src/apps quando CapyUI ausente.
- `CapyUI/Makefile`: 2 módulos com macro `CAPY_EMIT_MANIFEST`,
  `package-widget-core`, `package-desktop-session`, `lint-desktop`
  como header-presence check (full compile gated por CapyOS).
- `CapyUI/VERSION` bumped to 0.7.0.

### Installer UEFI

- `src/boot/uefi_loader/installer_run.c`: bloco Steps 1-6 envelopado
  em `#if 0 /* alpha.241: removed */`; Steps 7-8 substituídos por
  versão minimalista com chave de recovery + confirmação Y/n;
  `boot_cfg.flags` perde `BOOT_CONFIG_FLAG_HAS_SETUP_DATA`.

### Shell

- `src/shell/commands/system_control/capy_command.c` (novo TU):
  comando `capy` unificado com subcomandos install/module/wizard/update.
- `src/shell/commands/system_control/internal/system_control_internal.h`:
  declara `cmd_capy`.
- `src/shell/commands/system_control/power_runtime_registry.c`:
  array 39→40 slots, `pkg-bootstrap` em slot 38, `capy` em slot 39.

### Kernel runtime

- `src/arch/x86_64/kernel_shell_runtime.c`: removido bloco de silent
  provisioning (~200 linhas); detecta legacy `HAS_SETUP_DATA` mas
  delega ao wizard.

### Tooling

- `tools/scripts/migrate_to_capyui.py` (novo): script idempotente
  que copia 22 arquivos C/H do CapyOS para CapyUI e substitui os
  originais por stubs de forwarding.

### Docs

- `docs/architecture/modular-build-profiles.md` (novo).
- `docs/architecture/first-boot-wizard.md` (novo).
- `docs/operations/manual-module-deploy-runbook.md`: seção 4.A
  reescrita para wizard interativo + 4.B (profile pré-existente).
- `docs/reference/integration/compatibility-matrix.md`: CapyUI 0.7.0
  + ABI `capy-ui-desktop-session`.

## Versões

- `CapyOS`: `0.8.0-alpha.241+20260519`
- `CapyUI`: `0.7.0`
- demais repos: mantidos (0.0.2/0.6.0/0.1.1 onde aplicável).

## Validação (executar fora desta máquina)

```bash
# Migração de sources (uma vez)
python3 tools/scripts/migrate_to_capyui.py --dry-run
python3 tools/scripts/migrate_to_capyui.py --apply

# Pipeline host
make test                  # inclui 19 testes de install_profile (15+4 novos)
make test-capypkg
make layout-audit
make version-audit

# Kernel + ISO
make all64 PROFILE=full    # default: com desktop+apps via ../CapyUI/src
make all64 PROFILE=core-only  # validar opt-out
make iso-uefi

# Packaging dos módulos
cd ../CapyUI && make package
cd ../CapyOS && make modules-index

# Smokes opcionais
make smoke-x64-vmware-mouse-events TOOLCHAIN64=host SMOKE_X64_VMWARE_ARGS=...
```

## Limitações conhecidas

- Sem loader runtime de módulos: PROFILE=full ainda linka desktop no
  ELF do kernel. O gate de ativação controla apenas o behavior, não
  os bytes finais. Loader real é escopo de Etapa 12+.
- `core-only` ainda é experimental: não há smoke dedicado validando
  que o sistema sobe em modo headless.
- `capy install` não verifica assinatura Ed25519 para repos `signed`
  até o `CapyAgent` publicar o verifier.

## Próximas etapas sugeridas

- Smoke `smoke-x64-vmware-wizard-full` que dirige o wizard via HMP.
- Loader sandbox de módulos (Etapa 12+) para tirar desktop bytes do
  kernel ELF de fato.
- O índice agregado `modules-index.txt` é publicado automaticamente
  pelo workflow `.github/workflows/release-artifacts.yml` do CapyUI no
  canal rolante `latest` a cada push em `main`. O wizard usa o redirect
  estável `releases/latest/download/modules-index.txt`, então não há
  versão hardcoded para manter. Os `.manifest` individuais permanecem
  auxiliares de publisher.
