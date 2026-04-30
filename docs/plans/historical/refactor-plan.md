# Plano de Refatoracao CapyOS -> trilha unica UEFI/GPT x86_64

## Metas

- [x] Fase 0: definir `UEFI/GPT/x86_64` como caminho oficial
- [x] Fase 1: ajustar build e payloads 64-bit
- [x] Fase 2: subir kernel x64 com handoff UEFI
- [x] Fase 3: provisionar disco GPT com `ESP/BOOT/DATA`
- [x] Fase 4: validar boot por HDD com login, CLI e persistencia
- [ ] Fase 5: remover dependencias hibridas do firmware no runtime
- [ ] Fase 6: fechar cobertura do instalador ISO e remover legado residual

## Estado atual

### Build e release
- O repositorio opera com uma unica trilha suportada: `UEFI/GPT/x86_64`.
- Alvos principais:
  - `make`
  - `make all64`
  - `make iso-uefi`
  - `make disk-gpt`
  - `make smoke-x64-cli`
  - `make inspect-disk`
- Fluxos `BIOS/x86_32` nao fazem mais parte da release.

### Loader UEFI
- `BOOTX64.EFI` localiza manifesto e kernel a partir da particao `BOOT`.
- Existe fallback para arquivos na ESP quando necessario.
- O loader ainda opera em modo hibrido em parte do fluxo de input e nao fecha
  completamente o ciclo esperado de `ExitBootServices`.

### Runtime x64
- O kernel x64 sobe com framebuffer, login, shell e volume `DATA` persistente.
- O runtime agora preserva o handle logico da particao `DATA` quando o probe
  inicial passa, usando fallback RAW apenas em erro real de probe.
- Persistencia de arquivos e usuarios ja foi validada em reboot automatizado.

### Instalacao e provisionamento
- O caminho validado hoje e `provision_gpt.py` + boot por HDD.
- O instalador via ISO continua existente, mas ainda nao tem smoke ponta a
  ponta dedicado.

## Fases restantes

### Fase 5 - Remover dependencias hibridas
- completar input nativo no x64 para nao depender de `EFI ConIn`
- fechar a transicao do loader para ambiente runtime sem acoplamento residual
  ao firmware
- endurecer fallback de teclado para Hyper-V e outros hosts UEFI

### Fase 6 - Cobertura completa e limpeza de legado
- adicionar smoke de `ISO -> instalar -> reboot por HDD -> login`
- remover codigo, scripts e documentacao residuais de `BIOS/x86_32`
- reduzir divergencias de nomenclatura historica onde nao forem mais
  necessarias

## Validacao minima por fechamento

```bash
make test
make all64
make smoke-x64-cli
make inspect-disk IMG=build/disk-gpt.img
```

## Riscos conhecidos

- dependencia parcial de firmware no input/boot x64
- cobertura insuficiente do caminho de instalacao por ISO
- residuos de legado ainda presentes em codigo e documentacao historica
