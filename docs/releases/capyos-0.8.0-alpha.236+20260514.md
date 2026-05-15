# CapyOS 0.8.0-alpha.236+20260514

## Entrega

`alpha.236` entrega o gate externo `gui-session` para a plataforma oficial VMware + UEFI + E1000.

## Antes

- A sessao grafica ja expunha snapshot de saude e readiness para smokes futuros.
- O handoff seguro loginwindow -> sessao grafica ja liberava `session_begin`, shell context e autostart do desktop apenas apos submit GUI autenticado e usuario elegivel.
- Faltava um marker serial deterministico e um alvo oficial para evidenciar que a sessao grafica basica estava pronta fora desta maquina.

## Agora

- `include/gui/desktop.h` define o contrato publico do gate `gui-session`:
  - `DESKTOP_GUI_SESSION_SMOKE_MARKER` como `[smoke] gui-session ready`;
  - mascara de blockers obrigatorios para sessao grafica;
  - mascara de rotas essenciais de teclado, fila, overlays, window manager, titlebar, taskbar e desktop icons;
  - mascara separada de rotas de `mouse-events`.
- `src/gui/desktop/desktop_smoke_readiness.c` separa `gui-session` de `mouse-events`:
  - `gui_session_ready` nao depende de mouse inicializado, cursor valido ou rotas de mouse;
  - `mouse_events_ready` continua exigindo mouse/cursor/rotas de mouse;
  - `desktop_gui_session_smoke_gate_from_readiness()` falha fechado em snapshot ausente, blockers essenciais, rotas essenciais ausentes e blockers desconhecidos;
  - mouse/cursor/rotas de mouse pendentes aparecem como `mouse_events_deferred`, sem bloquear `gui-session`.
- `src/gui/desktop/desktop_runtime.c` emite uma unica vez `[smoke] gui-session ready` quando o gate aprova a sessao.
- `Makefile` adiciona `smoke-x64-vmware-gui-session`, que exige DHCP + marker `gui-session` no harness VMware.
- Scripts de release passam a exigir `smoke-x64-vmware-gui-session` no handoff, readiness, aceitacao e promocao.
- `release_ci_smoke_evidence.py` exige sempre `[net] DHCP: lease acquired.` e `[smoke] gui-session ready`, preservando markers extras sem permitir substituicao dos obrigatorios.

## Testes adicionados

`tests/test_desktop_smoke_readiness.c` cobre:

- metadados estaveis do gate `gui-session`;
- rejeicao de output nulo;
- fail-closed com readiness nula;
- `gui-session` pronto com `mouse-events` deferidos;
- bloqueio por rota essencial ausente;
- bloqueio por componente base ausente;
- bloqueio fail-closed por blocker desconhecido.

## Impacto

- Usuario final: a esteira externa consegue observar quando a sessao grafica basica esta pronta, sem depender do smoke posterior de mouse.
- Seguranca: nenhum segredo, usuario, senha, salt ou material sensivel entra no marker; blockers desconhecidos bloqueiam o gate por padrao.
- Performance/escalabilidade: o gate e derivado de snapshots ja existentes e emite o marker apenas uma vez.
- Estrutura: `gui-session` e `mouse-events` ficam separados, preservando o fechamento sequencial da Etapa 2.

## Proximo passo

`alpha.237` deve implementar o smoke externo `mouse-events` e fechar a Etapa 2 somente apos os gates externos obrigatorios rodarem fora desta maquina.

## Validacao recomendada fora desta maquina

- `make test`
- `make layout-audit`
- `make all64`
- `make smoke-x64-vmware-gui-session SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'`
- `make release-ci-smoke-readiness RELEASE_TAG=0.8.0-alpha.236+20260514 SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'`
- `make release-ci-smoke-evidence RELEASE_TAG=0.8.0-alpha.236+20260514 SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'`
- `make release-ci-smoke-acceptance RELEASE_TAG=0.8.0-alpha.236+20260514 SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'`
