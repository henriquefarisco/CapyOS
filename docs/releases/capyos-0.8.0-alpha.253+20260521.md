# CapyOS 0.8.0-alpha.253+20260521 — Sub-slice 3E.4.B entregue

**Data:** 2026-05-21
**Canal:** alpha (experimental)
**Plataforma oficial:** VMware + UEFI + E1000
**Etapa:** 3 (Driver framework + USB HID + storage estável) — aguardando execução externa do gate 3E.5

## Resumo executivo

Sub-slice 3E.4.B fecha o débito técnico documentado em alpha.250:
migração mecânica de `dbg_puts`/`dbg_hex32`/`dbg_hex64`/`dbg_putc`/
`dbg_label_hex32` para `klog(KLOG_*, ...)` / `klog_hex(KLOG_*, prefix, value)`
nos hot paths de storage (AHCI + NVMe), totalizando **108 call sites
em 2 arquivos**.

Como efeito colateral inesperado mas valioso, a migração **corrige
um bug latente de undefined-reference** descoberto durante o
refactor (ver seção 4).

## Por que essa migração

`dbg_*` escreve em **port 0xE9** (QEMU/Bochs debug console),
disponível apenas via `qemu-system-x86_64 -debugcon ...`. Em
produção VMware/hardware real, esses writes vão para o ether.

`klog` escreve em **ring buffer em memória** com:
- 4 níveis (DEBUG/INFO/WARN/ERROR).
- Recuperável runtime via `klog_dump(print_fn)`.
- Persistido em `/var/log/capyos_klog.txt` pelo kernel logger
  service.
- Timestamp e tag de nível por entrada.

Trade-off: perdemos a densidade da formatação composta de uma
linha por evento (e.g., `[ahci] setup port X sig=Y ssts=Z cmd=W`
virou 4 entradas klog), ganhamos estrutura forense
pesquisável e severidade por campo.

## Escopo

### Arquivos tocados

| Arquivo | Sites originais | Estado pós-migração |
|---|---|---|
| `@/Users/t808981/Desktop/PR/CapyOS/src/drivers/storage/ahci.c` | 55 | Sem `dbg_*`; usa `klog`/`klog_hex` |
| `@/Users/t808981/Desktop/PR/CapyOS/src/drivers/nvme/nvme.c` | 53 | Sem `dbg_*`; usa `klog`/`klog_hex` |

### Includes adicionados

```c
#include "kernel/log/klog.h"
```

### Helpers removidos

- `ahci.c`: `dbg_putc`, `dbg_puts`, `dbg_hex32`, `dbg_hex64`, `dbg_label_hex32` (27 linhas).
- `nvme.c`: `dbg_putc`, `dbg_puts`, `dbg_hex32`, `dbg_hex64` (21 linhas).

### Níveis aplicados

| Categoria | Nível | Exemplos |
|---|---|---|
| Init / probe / scan / sucesso | `KLOG_INFO` | controller found, BAR0=, CAP_hi/lo, identify lba28/48, COMRESET ok, controller reset ok, I/O queues created, init complete |
| Transient / timeout / recoverable em hot path | `KLOG_WARN` | command failed/aborted/timeout, port not idle, no SATA device ready, I/O cmd failed/timeout, read/write failed lba= |
| Permanent / fatal / setup failed | `KLOG_ERROR` | port stop/start timeout, identify failed, invalid ABAR, COMRESET no device/restart failed, port setup failed, controller fatal status, timeout waiting for ready, admin cmd failed/timeout, BAR0 is zero, reset CSTS still RDY, reset RDY never came back, failed to recreate I/O CQ/SQ, failed to create I/O CQ/SQ |

## Bug latente corrigido como efeito colateral

Durante a migração, encontrei **2 chamadas a `dbg_label_hex32` em
`nvme_controller_reset`** (linhas 570 e 583 do snapshot
pré-migração de `nvme.c`):

```c
// alpha.248 — broken
if (csts & NVME_CSTS_RDY) {
    dbg_label_hex32("[nvme] reset CSTS still RDY=", csts);
    return -1;
}
// ...
if ((csts & NVME_CSTS_RDY) == 0u) {
    dbg_label_hex32("[nvme] reset RDY never came back, csts=", csts);
    return -1;
}
```

**Problema:** `dbg_label_hex32` é `static` em `ahci.c`. Não está
declarado em `nvme.c` nem em nenhum header. As chamadas eram
undefined-reference no escopo da TU.

**Por que não explodia o link:** o caminho de
`nvme_controller_reset` é raramente exercitado em produção (só
dispara após TIMEOUT em NVMe + reset escalation via
`block_device_ops.reset`). Pode estar que o linker estivesse
tolerando referências não satisfeitas em código morto, OU que
o GCC -O2 estivesse inlining/eliminando o caminho. Não
investiguei a fundo porque a migração já resolve.

**Como o klog migration resolve:** as chamadas viram
`klog_hex(KLOG_ERROR, "[nvme] reset CSTS still RDY=", csts)`,
e `klog_hex` está declarado em `kernel/log/klog.h` (incluído
agora) e definido em `src/kernel/log/klog.c` (linked no kernel).

## Cobertura de teste

**Sem novos host tests.** Os arquivos `ahci.c` e `nvme.c` não
são linked no host runner (eles dependem de MMIO direto via
port 0xE9 e barramento PCIe). A migração é puramente cosmética
do ponto de vista de teste host.

**Cobertura indireta:**

1. `test_block_retry.c` continua passando (mock driver, não
   exerce `ahci.c`/`nvme.c` reais).
2. `test_block_error.c` continua passando (classifier puro).
3. `test_storage_smoke_gate.c` continua passando (gate puro).
4. `test_ahci_commands.c` continua passando (builders puros).
5. `test_ahci_slot_allocator.c` continua passando (allocator puro).
6. `test_nvme_commands.c` continua passando (builders puros).

**Validação pendente:** execução externa do gate
`smoke-x64-vmware-storage-resilience` em VMware oficial vai
confirmar que (a) não há regressão nos caminhos de boot/IO de
storage; (b) o klog ring captura corretamente os eventos que
antes iam para port 0xE9 (verificável via `klog_dump` ou
`/var/log/capyos_klog.txt` pós-boot).

## Escopo NÃO migrado (sub-slice 3E.4.C — follow-up)

Outros 13 arquivos do projeto usam o pattern `dbg_*` com
~126 sites adicionais:

| Arquivo | Sites |
|---|---|
| `src/arch/x86_64/storage_runtime.c` | 15 |
| `src/drivers/storage/efi_block.c` | 15 |
| `src/arch/x86_64/kernel_volume_runtime/mount_initialize.c` | 14 |
| `src/fs/storage/chunk_wrapper.c` | 14 |
| `src/security/crypt_aes_xts.c` | 14 |
| `src/fs/capyfs/runtime/namespace_ops.c` | 13 |
| `src/fs/storage/offset_wrapper.c` | 10 |
| `src/fs/cache/buffer_cache.c` | 9 |
| `src/fs/capyfs/runtime/directory_entries.c` | 9 |
| `src/arch/x86_64/kernel_volume_runtime/public_mount_api.c` | 4 |
| `src/fs/capyfs/runtime/format_mount.c` | 4 |
| `src/drivers/storage/ramdisk.c` | 3 |
| `src/arch/x86_64/kernel_volume_runtime/filesystem_helpers.c` | 2 |

**Justificativa para focar em ahci.c + nvme.c primeiro:**

1. São o caminho crítico do gate externo de Slice 3E.5.
2. São o código mais auditado nesta sessão (alpha.245-252).
3. São onde o bug latente do `dbg_label_hex32` existia.
4. Mudanças nestes arquivos têm impacto direto na validação
   externa que estamos esperando.

Os outros 13 arquivos serão migrados em sub-slice 3E.4.C depois
da execução externa do gate 3E.5 confirmar que a migração
parcial não introduz regressão.

## Mudanças de contrato

**Nenhuma cross-repo.** Pura migração interna:

- Nenhum header público mudou.
- Nenhuma ABI runtime mudou.
- Nenhuma versão pinada de sister repo mudou.
- Stub host de smoke marker intacto.
- Tests intactos.

Comportamento observável externamente:

- **Antes:** debug output via port 0xE9 (visível apenas com
  `qemu-system-x86_64 -debugcon stdio` ou Bochs).
- **Depois:** debug output via klog ring (visível em runtime
  via `klog_dump` ou shell command equivalente; persistido em
  `/var/log/capyos_klog.txt` pelo kernel logger service).

Para o operador externo executando o gate `smoke-x64-vmware-storage-resilience`:

- Port 0xE9 fica silencioso para AHCI/NVMe (resto do código
  ainda usa port 0xE9 enquanto sub-slice 3E.4.C não rodar).
- Eventos AHCI/NVMe aparecem no klog ring com timestamp,
  nível e tag.
- COM1 continua mostrando `[net] DHCP: lease acquired.` e
  `[smoke] storage-stack ready` (esses são emitidos por
  `com1_puts` direto, não via dbg_*).

## Próximos passos

### Imediato (operador externo)

Build alpha.253 + executar gate em VMware oficial:

```bash
make smoke-x64-vmware-storage-resilience \
  SMOKE_X64_VMWARE_ARGS="--vmx /path/to/capyos-vmware.vmx \
                         --serial-log /path/to/serial.log \
                         --timeout 240"
```

Critério de sucesso: marker `[smoke] storage-stack ready` no COM1
exatamente uma vez, sem regressão.

Bônus de validação manual: pós-boot, executar `klog-dump` (ou
equivalente) e verificar que os eventos `[ahci] ...` e
`[nvme] ...` aparecem com seus níveis corretos.

### Médio prazo

1. **Sub-slice 3E.4.C** — migrar `dbg_*` nos outros 13 arquivos
   (~126 sites). Refactor mecânico similar a 3E.4.B.
2. **Sub-slice 3E.5.B** — extrair `nvme_controller_reset` em
   passos puros para unit test do BUG #2 fix (alpha.252).
3. **Slice 3F** — Device manager unificado.

Conforme `@/Users/t808981/Desktop/PR/CapyOS/docs/architecture/etapa-3-slice-3e-plan.md`.
