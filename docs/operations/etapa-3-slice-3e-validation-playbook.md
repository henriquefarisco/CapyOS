# Etapa 3 — Slice 3E.5 External Validation Playbook

**Gate alvo:** `make smoke-x64-vmware-storage-resilience`
**Marker canônico:** `[smoke] storage-stack ready`
**Plataforma oficial:** VMware Workstation/ESXi + UEFI + E1000
**Versão do gate:** `STORAGE_SMOKE_GATE_VERSION = 1`
**Pré-requisito:** Slice 3D externamente validado (alpha.245+)

---

## 1. Contrato de prontidão

O kernel emite `[smoke] storage-stack ready` em COM1 **exatamente
uma vez** por boot, na primeira ocorrência de qualquer uma das
duas condições:

1. Uma chamada a `ahci_read_block_ex` ou `ahci_write_block_ex`
   retorna `BLOCK_IO_OK`.
2. Uma chamada a `nvme_block_read_ex` ou `nvme_block_write_ex`
   retorna `BLOCK_IO_OK`.

Latch idempotente em `struct storage_smoke_state` por driver:
emissão acontece exatamente uma vez por boot.

O marker é redundante com um `klog(KLOG_INFO, ...)` para que o
trace seja recuperável mesmo sem captura serial.

## 2. Pipeline observado

```
                                  block_device_read/write
                                          │
                                          ▼
                          ┌───────────────┴───────────────┐
                          │                               │
                          ▼                               ▼
              ahci_read_block_ex                 nvme_block_read_ex
              ahci_write_block_ex                nvme_block_write_ex
                          │                               │
                          ▼                               ▼
              ahci_exec_classified              nvme_io_cmd_classified
                          │                               │
            (MMIO PxCI + spin + classify)      (SQ doorbell + spin + classify)
                          │                               │
                          ▼                               ▼
                     class == OK?                    class == OK?
                          │ yes                           │ yes
                          ▼                               ▼
              ahci_smoke_signal_ok              nvme_smoke_signal_ok
                          │                               │
                          └───────────────┬───────────────┘
                                          ▼
                          storage_smoke_observe(state, source)
                                          │
                          first OK across either source?
                                          │ yes (latch flips)
                                          ▼
                          storage_smoke_emit_marker
                                          │
                          ┌───────────────┴───────────────┐
                          ▼                               ▼
                  com1_puts(MARKER "\n")          klog(KLOG_INFO, ...)
                          │                               │
                          ▼                               ▼
              Serial captured by                  Recoverable via
              smoke_x64_vmware.py                 /var/log/capyos_klog.txt
```

## 3. Pré-requisitos

1. Slice 3D externamente validado (USB HID gate aprovado).
2. Workspace na revisão CapyOS `0.8.0-alpha.250+20260521` ou superior
   com o scaffolding desta playbook aplicado:
   - `include/drivers/storage/storage_smoke.h`
   - `src/drivers/storage/storage_smoke.c`
   - `src/drivers/storage/storage_smoke_io.c`
   - `tests/stubs/stub_storage_smoke_io.c`
   - `tests/drivers/test_storage_smoke_gate.c`
   - alvo `smoke-x64-vmware-storage-resilience` no `Makefile`
3. Hardware/VMware:
   - VMware Workstation/ESXi com a VM oficial UEFI + E1000.
   - Pelo menos um dos seguintes storage backends habilitado na VM:
     - **AHCI** (SATA controller) — preferido para o smoke porque
       exercita o caminho mais antigo e o COMRESET escalonado.
     - **NVMe** — exercita CC.EN-based Controller Level Reset.
   - Captura serial habilitada (`serial0.fileType = "file"`).
4. Network DHCP funcional (o marker `[net] DHCP: lease acquired.`
   precede o marker de storage).

## 4. Comandos de execução

### 4.1 Build + smoke (caminho padrão)

```bash
make smoke-x64-vmware-storage-resilience \
  SMOKE_X64_VMWARE_ARGS="--vmx /path/to/capyos-vmware-e1000.vmx \
                         --serial-log /path/to/serial.log \
                         --timeout 240"
```

Dependências do target: `all64 iso-uefi manifest64` (build + ISO +
manifest verificado).

### 4.2 Provider govc (lab remoto)

```bash
make smoke-x64-vmware-storage-resilience \
  SMOKE_X64_VMWARE_ARGS="--provider govc \
                         --vm-name capyos-storage-smoke \
                         --govc-serial-log datastore1/capyos/serial.log \
                         --serial-log /tmp/serial.log \
                         --timeout 300"
```

### 4.3 Pre-checks recomendados antes do gate

```bash
make version-audit            # confirma alpha.250 em todos os pinos
make test                     # 92 host tests, inclui storage_smoke_gate
make layout-audit             # estrutura de árvore consistente
make release-check            # gate agregado release
```

## 5. Critérios de aceite

O gate passa quando:

1. O harness `smoke_x64_vmware.py` observa os dois markers no COM1
   na ordem:
   1. `[net] DHCP: lease acquired.`
   2. `[smoke] storage-stack ready`
2. Nenhum marker de falha (`panic:`, `kernel oops`, etc.) aparece
   antes dos markers de sucesso.
3. O harness retorna exit code 0 dentro do `--timeout`.

O artefato de prova é o tail do serial log gravado em
`build/ci/smoke_x64_vmware_summary.log` (ou caminho explícito via
`--summary-log`).

## 6. Triagem por sintoma

| Sintoma | Causa provável | Próximo passo |
|---|---|---|
| Marker DHCP observado, mas storage-stack timeout | Nenhum read/write OK aconteceu durante o boot — controlador presente mas com erro permanente, ou nenhum filesystem sendo lido pós-boot | Inspecionar klog: `klog_dump` deve mostrar `[ahci] command failed class=...` ou `[nvme] cmd failed class=...`. Se class=DEVICE_GONE, verificar SATA/PCIe enumeration no boot log |
| Storage marker emitido antes do DHCP | Ordem invertida não viola contrato lógico, mas `markers_in_order` rejeita | Confirmar que a captura serial não tem buffering reordenado; ajustar `--marker` order ou aceitar reorder |
| Nenhum marker | Boot trava antes do storage init | Investigar dbg via `klog` e `dbg_puts` (port 0xE9) em early-boot |
| `[ahci] COMRESET begin` repetido | Driver entrando em loop de TIMEOUT → COMRESET → TIMEOUT | Bug em retry loop. Cada chamada `block_device_read_ex` deve emitir COMRESET no máximo uma vez (verificar `reset_attempted` flag em `block_device.c`) |
| `[nvme] controller reset` antes do marker | NVMe wedged no primeiro request | Pode indicar VM config: queue depth alocada mas controller não habilitado. Verificar CSTS.RDY no boot |
| Marker do USB HID emitido (alpha.245) mas storage não | Slice 3D OK, Slice 3E.4 com regressão | Comparar binário com alpha.250: confirmar `storage_smoke_io.o` linkado e que ahci.c/nvme.c chamam `*_smoke_signal_ok()` |

## 7. Após aprovação externa

Quando o operador externo aprova:

1. Registrar o aceite em `docs/plans/STATUS.md` como bullet sob
   "Slice 3E.5 fechado em YYYY-MM-DD (alpha.NNN)".
2. Bumpar versão se aplicável (workflow `bump-alpha-version.md`).
3. Atualizar `docs/architecture/etapa-3-slice-3e-plan.md` §3.Slice
   3E.5 marcando como entregue.
4. Avaliar se a Etapa 3 pode fechar formalmente ou se Slices 3F-3J
   continuam pendentes.

## 8. Local execution policy

Este playbook é executado em uma máquina **diferente** desta
workspace. Aqui (no review/edit) apenas:

- editamos o scaffolding;
- ajustamos triagem por sintoma quando aprendemos novos modos de falha;
- nunca rodamos `make smoke-x64-vmware-storage-resilience` localmente.
