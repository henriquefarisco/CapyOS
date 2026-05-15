# CapyOS 0.8.0-alpha.237+20260514

## Entrega

`alpha.237` entrega o gate externo `mouse-events` para a plataforma oficial VMware + UEFI + E1000.

## Antes

- `alpha.236` entregou o gate externo `gui-session` com marker `[smoke] gui-session ready`.
- O smoke oficial ainda não exigia mouse, cursor e rotas de mouse como contrato final.
- A Etapa 2 ainda dependia do slice `mouse-events` para fechar o escopo técnico de sessão gráfica operacional.

## Agora

- `include/gui/desktop.h` adiciona o contrato público `mouse-events`:
  - `DESKTOP_MOUSE_EVENTS_SMOKE_MARKER` como `[smoke] mouse-events ready`;
  - masks de blockers obrigatórios incluindo `gui-session`, mouse, cursor e rotas do dispatcher;
  - mask de rotas exigidas combinando rotas essenciais de GUI e rotas de mouse.
- `src/gui/desktop/desktop_smoke_readiness.c` implementa `desktop_mouse_events_smoke_gate_from_readiness()`:
  - falha fechado para readiness ausente;
  - falha fechado para snapshot ausente;
  - falha fechado para blockers essenciais, rotas GUI/mouse ausentes e blockers desconhecidos;
  - aprova apenas quando `gui_session_ready` e `mouse_events_ready` estão verdadeiros.
- `src/gui/desktop/desktop_runtime.c` emite `[smoke] mouse-events ready` uma única vez por sessão, após o gate aprovar mouse, cursor e rotas de mouse.
- `Makefile` adiciona `smoke-x64-vmware-mouse-events`, exigindo no mesmo harness:
  - `[net] DHCP: lease acquired.`;
  - `[smoke] gui-session ready`;
  - `[smoke] mouse-events ready`.
- Scripts de handoff, readiness, evidência, aceitação e promoção passam a usar `smoke-x64-vmware-mouse-events` como gate oficial final.
- `release_ci_smoke_evidence.py` exige sempre os três markers obrigatórios e preserva markers extras sem permitir substituição dos obrigatórios.

## Testes adicionados

`tests/test_desktop_smoke_readiness.c` cobre:

- metadados estáveis do gate `mouse-events`;
- rejeição de output nulo;
- fail-closed com readiness nula;
- sessão pronta aprovada;
- bloqueio por mouse ausente;
- bloqueio por rota de mouse ausente;
- bloqueio por rota essencial de GUI ausente;
- bloqueio fail-closed por blocker desconhecido.

## Impacto

- Usuário final: o smoke externo passa a observar readiness de sessão gráfica e entrada de mouse como contrato único de handoff gráfico operacional.
- Segurança: markers continuam públicos e sem credenciais, nomes de usuário, salts, hashes ou material sensível; blockers desconhecidos bloqueiam por padrão.
- Performance/escalabilidade: o gate deriva apenas de snapshots já existentes e emite marker uma única vez.
- Estrutura: `gui-session` permanece como etapa interna observável e `mouse-events` vira o gate oficial final da trilha VMware + UEFI + E1000.

## Próximo passo

A Etapa 2 fica tecnicamente fechada por código, testes host-side e documentação; os gates externos ainda devem ser executados fora desta máquina antes de promoção pública.

## Validação recomendada fora desta máquina

- `make test`
- `make layout-audit`
- `make all64`
- `make smoke-x64-vmware-mouse-events SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'`
- `make release-ci-smoke-readiness RELEASE_TAG=0.8.0-alpha.237+20260514 SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'`
- `make release-ci-smoke-evidence RELEASE_TAG=0.8.0-alpha.237+20260514 SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'`
- `make release-ci-smoke-acceptance RELEASE_TAG=0.8.0-alpha.237+20260514 SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'`
- `make release-ci-smoke-promotion RELEASE_TAG=0.8.0-alpha.237+20260514 SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'`
