# Plano de implantacao do MVP do CapyOS

Status de referencia: 2026-03-05

Este plano parte do estado atual validado:

- trilha oficial unica em `UEFI/GPT/x86_64`
- boot por HDD provisionado funcionando
- volume `DATA` cifrado montando no runtime x64
- login, CLI e persistencia de arquivos/usuarios validados em reboot

O legado `BIOS/x86_32` foi descontinuado e permanece apenas como divida de
remocao no repositorio.

## 1. Estado atual x proximo alvo

### 1.1 Boot, disco e persistencia

| Tema | Estado atual | Proximo alvo |
|---|---|---|
| Boot x64 por HDD | validado com `make smoke-x64-cli` | ampliar matriz de hosts e incluir cenarios de disco real |
| Provisionamento GPT | `provision_gpt.py` e `inspect-disk` cobrem o fluxo oficial | endurecer mensagens de erro e selecao de disco |
| Runtime de storage | handle logico da particao `DATA` e priorizado; RAW e fallback de erro | reduzir dependencia de firmware no runtime |
| Persistencia | arquivos e usuarios sobrevivem ao reboot | ampliar cobertura de recovery e corrupcao simulada |

### 1.2 Login, shell e operacao

| Tema | Estado atual | Proximo alvo |
|---|---|---|
| Login x64 | fluxo unificado com `system_login` | adicionar auditoria e lockout |
| CLI | comandos principais operacionais no CapyCLI | historico, autocomplete, jobs e pipes |
| Gestao de usuarios | comandos basicos disponiveis | grupos, remocao e politicas de senha |

### 1.3 Input e drivers

| Tema | Estado atual | Proximo alvo |
|---|---|---|
| Teclado em VMs UEFI | `EFI ConIn` e prioridade atual; PS/2 e fallback adicionais | fechar input nativo e remover modo hibrido |
| Hyper-V keyboard | fallback experimental ainda existe | hardening e cobertura real em mais hosts |
| USB HID/XHCI | inicializacao parcial | enumeracao e parser HID completos |
| Rede x64 | `e1000` funcional, `tulip-2114x` em validacao, `netvsc` pendente | fechar baseline de rede no Hyper-V |

### 1.4 Segurança e filesystem

| Tema | Estado atual | Proximo alvo |
|---|---|---|
| Criptografia | AES-XTS + PBKDF2 no volume `DATA` | integridade autenticada e rotacao de chaves |
| NoirFS | volume persistente operacional | journal, recovery e fsck |
| Multiusuario | modelo base funcional | ACL, grupos e auditoria |

## 2. Fases de fechamento

## Fase A - Hardening do boot x64
- Entrega:
  - remover dependencia residual de firmware no input/runtime
  - consolidar handoff UEFI sem comportamento hibrido
  - revisar selecao do disco alvo no instalador
- Validacao minima:
  - `make test`
  - `make all64`
  - `make smoke-x64-cli`
  - boot manual em Hyper-V Gen2

## Fase B - Instalador ISO confiavel
- Entrega:
  - adicionar smoke de `ISO -> instalar -> reboot por HDD -> login`
  - alinhar prompts e persistencia da chave do volume
  - garantir paridade entre instalador e provisionamento direto
- Validacao minima:
  - install path automatizado em QEMU/OVMF
  - auditoria do disco final com `make inspect-disk`
  - reboot validando arquivo persistido

## Fase C - Filesystem e seguranca
- Entrega:
  - journal/recovery do NoirFS
  - integridade de metadata
  - auditoria basica de login e operacoes sensiveis
- Validacao minima:
  - testes de recovery
  - regressao de autenticacao
  - reboot apos escrita intensa

## Fase D - Limpeza definitiva do legado
- Entrega:
  - remover codigo residual `BIOS/x86_32`
  - remover scripts/alvos nao usados
  - manter somente documentacao historica explicitamente marcada como arquivo
    de release
- Validacao minima:
  - pipeline oficial funcionando sem toolchain 32-bit
  - nenhum doc principal recomendando fluxo legado

## 3. Checklist obrigatorio antes de merge

- codigo compilando no alvo impactado
- documentacao atualizada
- smoke do caminho oficial executado
- evidencias de build e teste registradas

Checklist base:

```bash
make test
make all64
make smoke-x64-cli
make inspect-disk IMG=build/disk-gpt.img
```

## 4. Politica de branches

Fluxo recomendado:
1. desenvolvimento na branch de trabalho
2. merge em `develop` apos validacao tecnica
3. merge em `main` apos smoke final de release

## 5. Atualizacao deste ciclo

### 5.1 Problema atacado
- regressao de boot/persistencia apos a migracao de 32 bits para 64 bits
- risco de o runtime cair para acesso RAW mesmo quando a particao logica
  `DATA` ja estava valida
- lacuna de documentacao entre o fluxo oficial e o fluxo opcional da ISO

### 5.2 O que foi ajustado
- runtime x64 passou a manter o handle logico da particao `DATA` quando o
  probe inicial tem sucesso
- `make inspect-disk` foi incorporado ao fluxo documentado
- documentacao principal foi alinhada para a trilha unica `UEFI/GPT/x86_64`

### 5.3 Evidencia validada neste ciclo
- `make test`: OK
- `make all64`: OK
- `make smoke-x64-cli`: OK com boot 1, boot 2 e persistencia
- `make inspect-disk`: OK com GPT, `BOOT` raw, kernel ELF e `CAPYCFG.BIN`
  consistentes

## 6. Referencias cruzadas

- `README.md`
- `docs/architecture.md`
- `docs/cli_test_plan.md`
- `docs/system-roadmap.md`
- `docs/noiros-cli-reference.md`
- `docs/HYPERV_SETUP.md`
