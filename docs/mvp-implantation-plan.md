# Plano de implantacao do MVP do NoirOS

Status de referencia: 2026-02-10

Objetivo do ciclo atual:
- consolidar o caminho x86_64 para uso real em VM (especialmente Hyper-V Gen2)
- remover dependencia de input por COM para uso diario
- recuperar paridade dos comandos de CLI apos migracao
- expandir cobertura de NFS/NoirFS via shell (criacao, navegacao e manipulacao)
- mapear backlog tecnico de multithread, seguranca, criptografia e multiusuario

## 1. Mapa de estado atual x esperado

### 1.0 Estabilidade de carga relocada no x64

| Tema | Como esta agora | O que falta implementar | Comportamento esperado apos implantacao |
|---|---|---|---|
| Tabelas com ponteiros de funcao (ops/comandos) | caminhos criticos foram migrados para inicializacao em runtime (NoirFS ops, wrappers de bloco, crypto, ATA/NVMe, comandos CLI, layouts de teclado) | auditoria final dos modulos restantes com tabelas estaticas | kernel x64 carrega em endereco dinamico sem saltos para ponteiros invalidos |
| Boot x64 em ISO UEFI | `make all64`, `make iso-uefi` e `make smoke-x64-cli` validados no ciclo atual | ampliar matriz de teste (QEMU + Hyper-V + cenarios com disco real) | bootstrap previsivel, login e comandos basicos funcionais em VMs alvo |

### 1.1 Entrada de teclado e compatibilidade de drivers

| Tema | Como esta agora | O que falta implementar | Comportamento esperado apos implantacao |
|---|---|---|---|
| Teclado em Hyper-V Gen2 | x64 prioriza EFI ConIn; COM nao e mais requisito primario | hardening do fallback VMBus + testes em mais hosts | login e CLI funcionam com teclado da VM, sem Putty/COM |
| PS/2 | detectado quando presente | telemetria adicional de falha e debounce | fallback estavel em VMs legadas e hardware com PS/2 |
| Hyper-V VMBus keyboard | caminho experimental habilitado apenas quando EFI/PS2 indisponiveis | estabilizar negociacao e watchdog-safe retries | fallback funcional para cenarios sem EFI input |
| USB teclado (XHCI/HID) | detecta controlador XHCI, sem cadeia HID completa | enumeracao, parser HID e polling de teclado | suporte a teclado USB em hardware/VM sem EFI/PS2/VMBus |

### 1.2 CLI e experiencia de shell

| Tema | Como esta agora | O que falta implementar | Comportamento esperado apos implantacao |
|---|---|---|---|
| Comandos legados pos-migracao | loop x64 usa shell modular novamente; aliases de compatibilidade ativos | fechar comandos planejados ainda ausentes (jobs/pipeline etc.) | comandos historicos principais funcionando com UX consistente |
| Prompt e sessao | prompt dinamico `user@host:cwd>` com logout via `bye` | historico persistente e autocomplete | uso continuo sem reset de contexto entre comandos |
| Documentacao in-shell | `help-any` e `help-docs` operacionais | manter doc embarcada sincronizada com release | usuario sempre encontra ajuda local e atualizada |

### 1.3 NFS/NoirFS no runtime x64

| Tema | Como esta agora | O que falta implementar | Comportamento esperado apos implantacao |
|---|---|---|---|
| Bootstrap de FS no x64 | NoirFS em RAM (ramdisk), estrutura base e users.db criados no boot | montar volume persistente real no caminho x64 | criacao/edicao/navegacao persistem entre boots |
| CLI de arquivos e diretorios | `mk-file`, `mk-dir`, `list`, `go`, `open`, `find` funcionando no contexto atual | validacao de permissao, recovery e integridade em volume persistente | fluxo de trabalho completo de arquivos pelo CLI |
| Sincronizacao | `do-sync` existente | writeback policy + journal/replay | sem perda de metadata apos queda abrupta |

### 1.4 Seguranca, criptografia e multiusuario

| Tema | Como esta agora | O que falta implementar | Comportamento esperado apos implantacao |
|---|---|---|---|
| Auth de usuarios | `userdb_authenticate` ativo no x64 e 32-bit | politicas de senha, lockout e auditoria | login robusto e rastreavel |
| Criptografia em disco | AES-XTS + PBKDF2 no fluxo 32-bit | integridade autenticada por bloco/metadata, rotacao de chaves | confidencialidade + deteccao de adulteracao |
| Multiusuario | modelo base de UID/GID/sessao/permissoes | comandos de gestao de usuarios e grupos | administracao multiusuario completa via CLI |

### 1.5 Multithread, performance e estabilidade

| Tema | Como esta agora | O que falta implementar | Comportamento esperado apos implantacao |
|---|---|---|---|
| Multithread/scheduler | ainda nao implantado no kernel | task model, run queue, sincronizacao basica | jobs de fundo e manutencao sem bloquear shell |
| Performance de I/O | cache e wrappers existentes | read-ahead/writeback, profiling e tuning NVMe | menor latencia em listagem/busca/copia |
| Robustez de sistema | base funcional em single-thread | testes de estresse, lock ordering e diagnostico | menos regressao em cenarios reais de VM/hardware |

## 2. Fases de entrega (branch atual em diante)

## Fase A - Input e drivers para VM UEFI
- Entrega:
  - consolidar prioridade EFI -> PS/2 -> VMBus -> COM
  - remover dependencia operacional de COM no Hyper-V Gen2
  - manter tabelas de ponteiros sensiveis inicializadas em runtime no x64
  - manter logs de deteccao por backend
- Validacao minima:
  - boot em Hyper-V Gen2
  - login com teclado da VM sem Putty
  - executar `help-any`, `list`, `bye`
- Resultado esperado:
  - ambiente usavel sem serial obrigatoria

## Fase B - Paridade de CLI e NFS via shell
- Entrega:
  - fluxo x64 100% pelo shell modular para comandos principais
  - aliases de compatibilidade (`help`, `clear`, `reboot`, `halt`)
  - navegacao/manipulacao de arquivos e diretorios via NoirFS
- Validacao minima:
  - `mk-dir`, `go`, `mk-file`, `open`, `find`, `do-sync`
  - logout/login sem perder consistencia de sessao
- Resultado esperado:
  - CLI funcional para uso cotidiano de filesystem

## Fase C - Persistencia real no x64
- Entrega:
  - substituir bootstrap em RAM por montagem de volume persistente
  - preparar caminho para volume cifrado no runtime x64
- Validacao minima:
  - criar arquivo, reboot, validar persistencia
  - autenticar usuario existente apos reboot
- Resultado esperado:
  - x64 com comportamento de sistema instalado, nao apenas ambiente efemero

Plano incremental sugerido para Fase C:
- C1 (mount persistente minimo):
  - detectar particao de dados no caminho x64
  - montar NoirFS sem criptografia (modo de transicao)
  - manter fallback para ramdisk se mount falhar
- C2 (auth + users.db persistentes):
  - migrar `userdb` para volume persistente
  - garantir login/sessao com dados reaproveitados entre boots
- C3 (volume cifrado no x64):
  - integrar unlock de volume no boot x64
  - validar reboot e retomada de dados cifrados com consistencia

## Fase D - Seguranca e multiusuario avancados
- Entrega:
  - politicas de senha e lockout configuraveis
  - auditoria basica (login falho, operacoes sensiveis)
  - comandos de gestao de usuarios/grupos
- Validacao minima:
  - cenarios de permissao por owner/group/others
  - tentativas de autenticacao invalida e rastreio
- Resultado esperado:
  - baseline de seguranca e operacao multiusuario

## Fase E - Multithread e performance
- Entrega:
  - scheduler inicial + workers para I/O/flush
  - melhorias de cache e metricas de performance
- Validacao minima:
  - estresse de I/O sem travar shell interativo
  - benchmark comparativo antes/depois
- Resultado esperado:
  - ganho mensuravel de responsividade e throughput

## 3. Checklist de fechamento por fase

Checklist obrigatorio antes de merge:
- codigo compilando no alvo impactado
- documentacao atualizada
- smoke de comandos criticos
- evidencias de build/artefatos

Smoke automatizado recomendado:

```bash
make smoke-x64-cli
```

Checklist obrigatorio de build (WSL):

```bash
# 64-bit
make all64
make iso-uefi

# 32-bit (nao regressao cruzada)
make
make iso
```

Para este plano, cada fechamento de fase deve incluir geracao de novas ISOs via WSL.

## 4. Politica de branches e deploy

Fluxo recomendado:
1. desenvolvimento na branch de trabalho atual
2. merge em `develop` apos validacao tecnica
3. promote para `main` apos smoke final de release

Requisitos para deploy:
- artefatos de ISO gerados no ciclo
- changelog da fase
- mapeamento de risco e rollback

## 5. Referencias cruzadas

- `README.md` (estado geral do sistema)
- `docs/system-roadmap.md` (roadmap macro por dominio)
- `docs/noiros-cli-reference.md` (referencia de comandos)
- `docs/HYPERV_SETUP.md` (setup de VM Hyper-V)
