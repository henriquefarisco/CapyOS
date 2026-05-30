# CapyOS 0.8.0-alpha.259+20260525

**Data:** 2026-05-25  
**Canal:** alpha (experimental)  
**Versao:** `0.8.0-alpha.259+20260525`  
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)  
**Track laboratorial avançado:** Hyper-V + UEFI + (IDE Gen1 | StorVSC Gen2)  
**Tipo:** pure-internal CapyOS release (nenhum contrato sister tocado)

## Resumo executivo

Este alpha entrega as duas primeiras slices da **stack de compatibilidade
Hyper-V** definida em
[`docs/architecture/hyperv-compatibility-stack-plan.md`](../architecture/hyperv-compatibility-stack-plan.md),
endereçando dois problemas observados em uso real:

1. **Hyper-V Generation 1** não tinha caminho de storage persistente
   apesar do driver ATA-PIO já existir no tree — ele simplesmente nunca
   era oferecido ao native storage runtime.
2. **Hyper-V Generation 2** entrava direto em maintenance pós-instalação
   porque o boot policy fail-closed (introduzido em sessão anterior para
   impedir o wizard fake-em-RAM) prendia o usuário sem caminho para o
   first-boot wizard, já que o StorVSC ainda não tem data plane.

A solução foi entregue como duas slices verticais independentes e
estritamente aditivas, deixando a trilha oficial `VMware + UEFI + E1000`
intocada porque o probe ATA-PIO só executa quando NVMe **e** AHCI não
promoveram `block_device`, e os branches de degradação só executam
quando nenhum backend persistente foi promovido.

## Mudanças entregues

### Slice 1 — ATA-PIO promovido a backend nativo

- Novo enum value `X64_STORAGE_BACKEND_ATA_PIO` em
  `include/arch/x86_64/storage_runtime.h` (append-only sobre
  `NONE` → `EFI_BLOCK_IO` → `AHCI` → `NVME`).
- Novo header público `include/drivers/storage/ata_pio.h` declarando
  `ata_init`, `ata_devices_count`, `ata_device_by_index`,
  `ata_primary_device`.
- `src/arch/x86_64/storage_runtime.c::x64_storage_runtime_backend_name`
  e `x64_storage_runtime_native_candidate_name` reportam `"ata-pio"`.
- `src/arch/x86_64/storage_runtime_native.c::x64_storage_runtime_native_probe`
  ganha terceiro probe após NVMe e AHCI: chama `ata_init` se
  `ata_devices_count() <= 0`, consulta `ata_primary_device()` e roteia
  pelo `probe_native_storage_backend_from_raw` já existente com o novo
  enum value.
- `Makefile` adiciona `$(BUILD)/x86_64/drivers/storage/ata_pio.o` à
  lista de objetos kernel x86_64.
- Driver `src/drivers/storage/ata_pio.c` **não** modificado: já
  implementava LBA28 com canais primary/secondary + master/slave,
  IDENTIFY com retry + soft reset, ghost-check + signature probe ATAPI
  e status decoding completo.

**Target hardware coverage:** Hyper-V Generation 1 com VHD anexado por
IDE, máquinas QEMU/Bochs/VirtualBox legacy IDE, hosts bare-metal com
firmware ATA fallback.

### Slice 2 — Wizard-em-RAM com warning persistente

`src/arch/x86_64/kernel_main.c` estágio 8/8 substitui o override
fail-closed:

```c
if (!g_shell_persistent_storage) {
  validated_storage_ready = 0;
}
```

por uma política fail-degraded de três branches:

| Condição | `validated_storage_ready` | Warning em `g_boot_warnings` | Resultado |
|---|---|---|---|
| `shell_runtime_rc != 0` | 0 (default) | `"Storage runtime unavailable; persistence may fail"` | maintenance (storage real quebrado) |
| `!x64_storage_runtime_has_device()` | 1 | `"Persistent storage unavailable; configuration will NOT survive reboot"` | wizard / login alcançável em RAM |
| `!g_shell_persistent_storage` (mount/unlock falhou e recovery RAM assumiu) | 1 | `"Persistent volume not mounted; running in RAM (no persistence)"` | wizard / login alcançável em RAM |
| Caso ideal: device promovido e mount OK | 1 | (nenhum) | fluxo normal |

VMware path executa exclusivamente a 4ª linha porque NVMe/AHCI sempre
promovem device real.

### Slice 3 plan — StorVSC I/O wire-up (planejado)

Novo documento arquitetural
[`docs/architecture/hyperv-compatibility-stack-plan.md`](../architecture/hyperv-compatibility-stack-plan.md)
(216 linhas) detalha:

- 7 slices da stack Hyper-V completa, com Slice 1+2 marcadas
  **delivered** e Slice 3 detalhada TU-by-TU.
- Slice 3 — camadas planejadas: `storvsc_scsi.c` (CDBs SCSI),
  `storvsc_io.c` (VMBus packets + GPA descriptors), `storvsc_block.c`
  (`block_device_ops` facade), extensão ao native runtime adapter,
  header público `include/drivers/storage/storvsc_block.h`.
- Data-plane invariants: DMA via `pmm_alloc_pages` /
  `kmalloc_aligned(4096)`, GPA descriptor com physical addr (identity
  paging preservada pelo loader), single-LUN inicial, sync wrapper
  com polling bounded.
- Enum value `X64_STORAGE_BACKEND_STORVSC` reservado append-only.
- Plano de validação: focused unit tests + novo gate externo
  `make smoke-x64-hyperv-gen2-storage`.
- Out-of-scope explícito: async multi-queue, hot-plug,
  SCSI pass-through, UNMAP / VSS.
- Slices 4-6 placeholders (NetVSC promotion, VMBus keyboard priority
  promotion, device manager hot-plug).

### Self-tests host-only

- Novo `tools/scripts/test_hyperv_compat_storage_stack.py` (156 LOC) —
  cobre Makefile wiring, header presence, enum append-only com ordem
  estrita, runtime impl com `"ata-pio"` em 2 sítios, native probe
  incluindo header + chamadas + promoção, probe order NVMe → AHCI →
  ATA-PIO estrito.
- `tools/scripts/test_hyperv_persistent_boot_policy.py` reescrito (113
  LOC) — três invariantes Slice 2: RAM-fallback branch com warning
  `NOT survive reboot`, mount-fail branch com warning `running in RAM`,
  override fail-closed legado proibido (regression guard).
- `Makefile` plugado: novo self-test corre em
  `hyperv-baseline-evidence-selftest` (já integrado ao gate
  `release-check` no fluxo externo).

## Evidências internas

Validação local executada (host-only, neste Mac):

- `python3 tools/scripts/test_hyperv_compat_storage_stack.py` — **OK**.
- `python3 tools/scripts/test_hyperv_persistent_boot_policy.py` — **OK**.
- `python3 tools/scripts/test_uefi_kernel_load_contract.py` — **OK**
  (contrato kernel base @ `0x10000000` preservado).
- `make hyperv-baseline-evidence-selftest` — **OK** (7 self-tests
  Hyper-V passam, incluindo o novo).
- `make test` — **OK** (>900 testes; nada regrediu).
- `make layout-audit` — **OK** sem warnings.
- `clang --target=x86_64-unknown-elf -fsyntax-only` sobre
  `storage_runtime_native.c`, `storage_runtime.c`,
  `storage_runtime_hyperv.c`, `storage_runtime_gpt.c` e
  `kernel_main.c` — **OK** (apenas warning `unknown pragma` ignorável
  vindo de `#pragma GCC optimize`).

Validação externa **pendente** (não pode ser executada nesta máquina
porque o toolchain cruzado `x86_64-linux-gnu-gcc` não está instalado):

- `make all64 TOOLCHAIN64=elf` — confirmação de link.
- `make iso-uefi TOOLCHAIN64=elf` — ISO release-grade.
- `make release-check` — gate agregado.
- Smoke real em VM Hyper-V Gen1 com VHD anexado por IDE — esperado:
  `runtime-native show` reporta `backend=ata-pio`, wizard completa em
  storage persistente, próximo reboot lê dados persistidos.
- Smoke real em VM Hyper-V Gen2 com VHD anexado por SCSI — esperado:
  warning loud `"Persistent storage unavailable; configuration will
  NOT survive reboot"` na boot, wizard alcançável em RAM (não trava
  em maintenance).
- Smoke real em VM VMware + UEFI + E1000 (track oficial) — esperado:
  `runtime-native show` reporta `backend=nvme` ou `backend=ahci` como
  antes, sem nenhum dos warnings, nenhuma mudança comportamental.

## Mudanças de contrato

**Nenhuma.**

- Adapter `services/capypkg` intacto.
- Activation gate `kernel/module_gate.c` intacto.
- Install profile schema intacto.
- Line-oriented manifest format intacto.
- Ed25519 descriptor scope intacto.
- Quotas `CAPYPKG_PAYLOAD_MAX` / `CAPYPKG_MAX_*` intactas.
- `install_root` scope intacto.
- Sister repos `CapyUI` (`2.13.1` / `capy-ui-widget` v2.13 /
  display-list schema v7), `CapyAgent`, `CapyBrowser`, `CapyCodecs`,
  `CapyLang`, `CapyBenchmark` intocados — nenhuma linha da
  `compatibility-matrix.md` muda neste alpha.
- Enum `enum x64_storage_backend` cresceu por **append-only**; valores
  existentes preservados.

## Atualizações de documentação

- **NOVO** `docs/architecture/hyperv-compatibility-stack-plan.md` —
  plano arquitetural da stack Hyper-V completa.
- `docs/plans/STATUS.md` — header bump + bullet `alpha.259` na seção
  "Extensões posteriores".
- `docs/plans/active/capyos-master-plan.md` — `Versão atual` bump.
- `VERSION.yaml` — `current`, `extended`, `current_summary` e prepend
  history entry para `alpha.259`.
- `README.md` — `Versao de referencia` bump.
- `.windsurf/README.md`, `.windsurf/skills/capyos-project-map/SKILL.md`,
  `.windsurf/skills/capyos-whatis/SKILL.md` — snapshots de versão
  bumped.

## Próximos passos

1. **Validação externa** dos gates listados em "Evidências internas →
   pendentes" para ratificar a release.
2. **Slice 3 implementação** (StorVSC I/O wire-up) seguindo o plano
   arquitetural — abrir como Etapa 4 follow-up paralelo (não bloqueia
   o gate principal de CapyDisplay 2D + scheduler).
3. **Atualizar** `docs/operations/hyperv-gen2-baseline-runbook.md` para
   refletir o novo posicionamento Gen1/Gen2 e listar `backend=ata-pio`
   como signal esperado para Gen1 — pode ser feito quando o smoke
   externo Gen1 for executado pela primeira vez.
4. **Sister repos** continuam sem ação requerida — esta release não
   abre nenhum gate cross-repo.
