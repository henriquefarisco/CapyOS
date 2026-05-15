# CapyOS — Smoke VMware+E1000 mouse-events

## Objetivo

Este smoke cobre a trilha oficial de release VMware + UEFI + E1000. O harness
`tools/scripts/smoke_x64_vmware.py` orquestra uma VM externa via `vmrun` ou
`govc`, observa o log serial/debugcon e valida markers de rede/DHCP e sessão
gráfica pronta.

A automação é intencionalmente fina: a infraestrutura VMware, a VM template e a
configuração de serial file pertencem ao operador ou à CI privada.

## Pré-requisitos

- VMware Workstation/Fusion/ESXi acessível pelo operador ou runner.
- `vmrun` no `PATH` para Workstation/Fusion, ou `govc` configurado para ESXi.
- VM x86_64 UEFI com NIC E1000 em rede bridged ou portgroup equivalente.
- Serial/debug console configurado para arquivo legível pelo host/runner.
- Artefatos atuais de build disponíveis: `build/boot/uefi_loader.efi`,
  `build/capyos64.bin`, `build/manifest.bin` e ISO UEFI.

## Execução com vmrun

```bash
SMOKE_X64_VMWARE_ARGS="--provider vmrun --vmx /path/to/CapyOS.vmx --serial-log build/ci/smoke_x64_vmware.serial.log"
make smoke-x64-vmware-mouse-events SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

O arquivo `.vmx` deve apontar o dispositivo serial para o mesmo caminho usado em
`--serial-log`.

## Execução com govc

```bash
SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log' --serial-log build/ci/smoke_x64_vmware.serial.log"
make smoke-x64-vmware-mouse-events SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

O runner deve ter `GOVC_URL`, `GOVC_USERNAME`, `GOVC_PASSWORD`, `GOVC_DATACENTER`
e demais variáveis de ambiente já configuradas.

## Markers

O alvo `smoke-x64-vmware-mouse-events` exige três markers reais em comparação
case-insensitive:

- `[net] DHCP: lease acquired.`
- `[smoke] gui-session ready`
- `[smoke] mouse-events ready`

A CI pode acrescentar markers extras em `SMOKE_X64_VMWARE_ARGS`; o alvo oficial
mantém os markers obrigatórios e repassa os extras ao harness:

```bash
SMOKE_X64_VMWARE_ARGS="--provider vmrun --vmx /path/to/CapyOS.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --marker capysh"
make smoke-x64-vmware-mouse-events SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

## Evidencia publica pos-smoke

Depois do smoke oficial, a CI pode materializar e conferir um manifesto publico das evidencias sem ligar a VM novamente:

```bash
make release-ci-smoke-evidence RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
make verify-release-ci-smoke-evidence RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
make release-ci-smoke-acceptance RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
make verify-release-ci-smoke-acceptance RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
make release-ci-smoke-promotion RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
make verify-release-ci-smoke-promotion RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

O manifesto registra hashes SHA-256 e tamanhos do serial log e do summary log em `build/ci/*.log`, alem dos markers obrigatorios. O marker `[smoke] gui-session ready` só é emitido após o contrato `desktop_gui_session_smoke_gate_from_readiness()` aprovar framebuffer, dimensões, taskbar, dispatcher essencial, fila saudável e ausência de overlays/drag; o marker `[smoke] mouse-events ready` só é emitido após `desktop_mouse_events_smoke_gate_from_readiness()` também aprovar mouse, cursor e rotas de mouse. O manifesto de aceitacao final amarra essas evidencias ao handoff oficial antes da promocao publica. O manifesto de promocao final amarra publicacao, handoff e aceitacao.

## Troubleshooting

- Se o harness falhar por timeout, conferir o tail em
  `build/ci/smoke_x64_vmware.summary.log`.
- Se o log serial estiver vazio, revisar a configuração serial da VM antes de
  investigar DHCP.
- Se DHCP não aparecer, confirmar que a NIC é E1000 e que a rede bridged/portgroup
  oferece lease para guests novos.
- Para validar só wiring de argumentos sem ligar a VM, usar `--dry-run`.

## Preflight CI

Antes de rodar o smoke em CI de release, use `make release-ci-preflight` para
validar a chave pública/fingerprint de release e os argumentos VMware sem ligar
a VM. Na esteira oficial, use também `make release-ci-official-provisioning-contract`
para exigir tag, chave pública oficial, manifesto e logs em `build/ci/*.log`, e
`make release-official-handoff-manifest` para registrar o handoff antes do smoke real.
O procedimento completo fica em `docs/operations/release-ci-preflight.md`.

## Status atual

`0.8.0-alpha.237` versiona o gate externo `mouse-events`, o target Makefile oficial, os markers obrigatórios DHCP + `gui-session` + `mouse-events` e a integração com handoff/readiness/evidência/aceitação/promoção de release. A execução real em CI continua dependendo da infraestrutura VMware externa e da chave pública oficial de release.
