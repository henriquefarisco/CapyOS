# CapyOS 0.8.0-alpha.251+20260521 — Slice 3E.5 entregue (scaffolding)

**Data:** 2026-05-21
**Canal:** alpha (experimental)
**Plataforma oficial:** VMware + UEFI + E1000
**Etapa:** 3 (Driver framework + USB HID + storage estável) — scaffolded; aguarda execução externa para fechamento formal

## Resumo executivo

Esta alpha entrega o **Slice 3E.5** — o external validation gate
`smoke-x64-vmware-storage-resilience` que consome o marker
`[smoke] storage-stack ready` emitido em Slice 3E.4. Com isto, todo
o pipeline de Slice 3E (storage hardening) está plumbed:

- Slice 3E.1: host-testable command builders (alpha.246).
- Slice 3E.2.A: unified error classifier (alpha.247).
- Slice 3E.2.B: retry policy + reset escalation (alpha.248).
- Slice 3E.3: AHCI slot allocator infrastructure (alpha.249).
- Slice 3E.4: storage smoke marker forense (alpha.250).
- **Slice 3E.5: external gate `smoke-x64-vmware-storage-resilience` (alpha.251).**

**Etapa 3 ainda não fecha formalmente.** O fechamento depende da
execução externa do gate por um operador em VMware oficial,
conforme `docs/operations/etapa-3-slice-3e-validation-playbook.md`.

## Mudanças entregues

### Novo target Makefile

`@/Users/t808981/Desktop/PR/CapyOS/Makefile`:

```make
smoke-x64-vmware-storage-resilience: all64 iso-uefi manifest64
	@echo "Executando smoke test VMware+E1000 storage-resilience..."
	python3 tools/scripts/smoke_x64_vmware.py \
		--marker "[net] DHCP: lease acquired." \
		--marker "[smoke] storage-stack ready" \
		$(SMOKE_X64_VMWARE_ARGS)
```

Dependências: `all64 iso-uefi manifest64` (build + ISO + manifest
verificado). Markers em ordem: DHCP primeiro, storage segundo.

### Decisão de design — sem novo harness Python

`@/Users/t808981/Desktop/PR/CapyOS/tools/scripts/smoke_x64_vmware.py`
já aceita `--marker` repetido desde o gate USB HID (Slice 3D). O
multi-marker é processado por `markers_in_order` em
`smoke_marker_policy.py`. Criar um wrapper Python seria duplicação
sem valor — o target Makefile **é** o gate.

### Novo runbook

`@/Users/t808981/Desktop/PR/CapyOS/docs/operations/etapa-3-slice-3e-validation-playbook.md`
(~140 linhas) com:

1. **Contrato de prontidão** — emissão exata + latch idempotente.
2. **Pipeline observado** (ASCII art):
   ```
   block_device_read/write → ahci_/nvme_*_block_ex →
   *_exec_classified → classify == OK? → *_smoke_signal_ok →
   storage_smoke_observe → storage_smoke_emit_marker →
   com1_puts(MARKER) + klog(KLOG_INFO)
   ```
3. **Pré-requisitos** — Slice 3D aprovado, alpha.250+, VM oficial
   com AHCI ou NVMe, captura serial habilitada.
4. **Comandos de execução** — `vmrun` e `govc`.
5. **Critérios de aceite** — markers em ordem, sem panic, exit 0
   no timeout.
6. **Triagem por sintoma** — 7 modos de falha documentados:
   - Marker DHCP mas timeout em storage → classifier inspect.
   - Storage marker antes do DHCP → serial buffering / ajuste de ordem.
   - Nenhum marker → boot trava → dbg via klog + port 0xE9.
   - `[ahci] COMRESET begin` repetido → retry loop bug.
   - `[nvme] controller reset` antes do marker → queue setup.
   - USB HID OK mas storage não → regressão 3E.4.
   - Marker do USB HID mas storage não → regressão isolada.
7. **Protocolo pós-aprovação** — STATUS bullet, version bump,
   plan update, avaliação de fechamento de Etapa 3.
8. **Local execution policy** reafirmada.

## Critério de fechamento

| Item do plano original | Status |
|---|---|
| Alvo Makefile `smoke-x64-vmware-storage-resilience` | ✅ |
| Markers `[net] DHCP: lease acquired.` + `[smoke] storage-stack ready` | ✅ |
| Runbook completo com triagem por sintoma | ✅ |
| Execução externa pelo operador em VMware oficial | ⏳ pendente |

## Mudanças de contrato

**Nenhuma cross-repo.** Pura adição interna:
- Target Makefile é wrapper sobre script existente.
- Runbook é documentação operacional.
- Nenhum código C novo (Slice 3E.4 já entregou o emitter).

Sister repos não afetadas — o marker é uma string que harness
externo opcional observa; não há schema versionado a quebrar.

## Próximos passos

### Imediato (operador externo)

```bash
# Em máquina com VMware oficial:
make smoke-x64-vmware-storage-resilience \
  SMOKE_X64_VMWARE_ARGS="--vmx /path/to/capyos-vmware.vmx \
                         --serial-log /path/to/serial.log \
                         --timeout 240"
```

Após aprovação, registrar em STATUS.md, bumpar versão e marcar
Slice 3E.5 como fechado em
`docs/architecture/etapa-3-slice-3e-plan.md`.

### Continuação do roadmap

1. **Avaliação de fechamento da Etapa 3.** Após execução externa
   bem-sucedida de 3E.5, decidir se Etapa 3 fecha (Slices 3D + 3E
   completos + USB HID externamente validado em alpha.245 +
   storage externamente validado em alpha.251) ou se Slices 3F-3J
   continuam dentro de Etapa 3 antes do fechamento formal.
2. Sub-slice 3E.4.B — migração mecânica de `dbg_puts → klog` em
   ahci.c e nvme.c (~50 sites).
3. Slice 3F — multi-table AHCI provisioning + remoção do
   spin-wait em `ahci_exec_classified` (requer scheduler async
   da Etapa 4).
4. Slices 3G-3J conforme `docs/architecture/etapa-3-driver-foundation-plan.md`.

Conforme `@/Users/t808981/Desktop/PR/CapyOS/docs/architecture/etapa-3-slice-3e-plan.md`
e `@/Users/t808981/Desktop/PR/CapyOS/docs/operations/etapa-3-slice-3e-validation-playbook.md`.
