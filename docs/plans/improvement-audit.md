# CapyOS - Auditoria Completa e Plano de Melhorias

Data de referencia: 2026-04-09.
Versao de referencia: `0.8.0-alpha.0`
Gerado a partir de leitura integral do repositorio.

Este documento cruza o estado real do codigo com os planos existentes
(`system-master-plan.md`, `system-delivery-roadmap.md`, `system-roadmap.md`,
`platform-hardening-plan.md`, `refactor-plan.md`,
`network-hyperv-refactor-and-update-plan.md`, `hyperv-network-reset-plan.md`)
e pontua o que falta para concluir cada etapa, junto com novas melhorias
identificadas.

---

## 1. Raio-X do sistema atual

### 1.1 O que existe e funciona

| Subsistema | Artefatos-chave | Estado |
|---|---|---|
| Boot UEFI | `src/boot/uefi_loader.c`, handoff v7, manifest GPT | Funcional; ExitBootServices concluido em QEMU (AHCI/NVMe) |
| Kernel x64 | `src/arch/x86_64/kernel_main.c` (~2866 linhas), GDT/IDT nativos | Funcional em QEMU/VMware; monolitico |
| CAPYFS | `src/fs/capyfs/capyfs.c`, superblock v2, bitmap, inode, dirs fixos | Operacional; sem journal/fsck/recovery |
| VFS | `src/fs/vfs/vfs.c`, resolucao de caminho, dentries, metadata | Funcional; sem symlinks, quotas ou ACL |
| Buffer cache | `src/fs/cache/buffer_cache.c` | Funcional; sync explicito |
| Criptografia | `src/security/crypt.c` (SHA-256, PBKDF2, AES-XTS), `csprng.c` | Funcional; sem integridade autenticada |
| Auth/sessao | `src/core/user.c`, `session.c`, `login_runtime.c` | Funcional; sem lockout/auditoria |
| CapyCLI | `src/shell/` (~16 arquivos), ~70 comandos | Estavel; sem historico/autocomplete/pipes |
| Rede | `src/net/` (~20 arquivos), ARP/IPv4/ICMP/UDP/TCP/DHCP/DNS | e1000 funcional; Hyper-V bloqueado |
| Servicos | `service_manager.c`, `work_queue.c`, `service_boot_policy.c` | Cooperativo; sem scheduler real |
| Update agent | `src/core/update_agent.c` | Staging/channel local; sem download/aplicacao real |
| Localizacao | `src/core/localization.c`, `user_prefs.c` | pt-BR/en/es; catalogo parcial |
| Drivers | e1000, tulip, AHCI, NVMe, XHCI (parcial), Hyper-V VMBus/keyboard | Varia; USB HID incompleto |
| Testes | 39 arquivos de teste, smokes CLI/ISO/cancel | Boa cobertura de host; sem fuzzing |
| Tooling | provision_gpt.py, inspect_disk.py, smoke scripts | Maduro para o estagio atual |

### 1.2 O que nao existe

- Scheduler / multitarefa
- Modelo de processos / userspace
- Memoria virtual por processo
- Syscall ABI
- IPC
- Journal / WAL / fsck.CAPYFS
- Sockets para userland
- TLS / PKI
- GUI / mouse / compositor
- Package manager
- Download e aplicacao real de updates
- Assinatura de boot e artefatos
- CI/CD pipeline endurecido

---

## 2. Alinhamento com planos existentes

### 2.1 `refactor-plan.md` - Migracao para trilha unica x64

| Fase | Estado | O que falta |
|---|---|---|
| Fase 0-4 | Concluidas | - |
| Fase 5 - Remover dependencias hibridas | Em progresso | Fechar EFI ConIn como fallback-only em VMware; PS/2 como primario universal |
| Fase 6 - Cobertura ISO + limpeza legado | Parcial | Smoke ISO existe; falta remover codigo legado BIOS/x86_32 residual; falta audit de includes orfaos |

**Para concluir:**
1. Grep completo de referencias a `__i386__`, `BIOS`, `MBR`, `x86_32` no fonte e remover
2. Atualizar `compile_flags.txt` (ainda tem `-m32 -D__i386__`)
3. Adicionar smoke que falhe se toolchain 32-bit for necessario
4. Remover `src/core/kernel.c` e `src/core/installer_main.c` se forem legado morto

### 2.2 `platform-hardening-plan.md` - Marcos A-E

| Marco | Estado | O que falta |
|---|---|---|
| A - Contrato e telemetria | Concluido | - |
| B - Fundacao x64 minima | Em progresso | Timer PIT/IRQ0 nativo pos-EBS adiado; falta ativar condicionalmente |
| C - Input sem firmware | Concluido para Hyper-V Gen2 | Falta PS/2 IRQ-driven confiavel em VMware; falta USB HID |
| D - Saida do modo hibrido | Concluido em QEMU (AHCI/NVMe) | Falta validar em VMware; falta consolidar timer nativo |
| E - Fechamento | Concluido como merge | Falta limpeza de codigo temporario de migracao |

**Para concluir:**
1. Ativar PIT/IRQ0 nativo apos ExitBootServices em cenario controlado
2. Fechar PS/2 IRQ-driven no x64 (hoje e polled)
3. Avancar XHCI: completar enumeracao, adicionar parser HID, registrar como fonte de input
4. Remover stubs temporarios que serviram apenas a migracao
5. Rodar 100 boots consecutivos em QEMU e VMware sem travamento

### 2.3 `system-delivery-roadmap.md` - Entregas A1-D3

| Entrega | Estado | O que falta |
|---|---|---|
| A1 - Cancelamento ISO | Concluida | - |
| A2 - Input BR/dead keys | Concluida | - |
| A3 - Tema/splash | Concluida | - |
| A4 - UX shell | Concluida | - |
| A5 - Idiomas por usuario | Concluida | - |
| A6 - Controle de energia | Concluida | - |
| **B1 - Rede** | **Em andamento** | Ver secao 3.1 |
| B2 - Scheduler | Pendente | Ver secao 3.2 |
| B3 - Servicos/jobs | Parcial (service_manager existe) | Ver secao 3.3 |
| C1 - Seguranca/confiabilidade | Pendente | Ver secao 3.4 |
| C2 - GUI/mouse | Pendente | Ver secao 3.5 |
| C3 - Apps basicos | Pendente | Depende de C2 |
| D1 - Runtime extensivel | Pendente | Depende de B2/B3/C1 |
| D2 - ML/automacao | Futuro | Depende de quase tudo |
| D3 - Integracoes | Futuro | Depende de quase tudo |

### 2.4 `system-master-plan.md` - Fases 0-12

| Fase | Estado | O que falta para concluir |
|---|---|---|
| Fase 0 - Release atual | Quase concluida | Fechar B1 (rede), validar VMware end-to-end |
| Fase 1 - Hardening plataforma | Avancada | Timer nativo, USB HID, 100 boots consecutivos |
| Fase 2 - Memoria/tarefas/servicos | Nao iniciada | Task struct, scheduler, run queue (secao 3.2) |
| Fase 3 - VM e fronteira kernel/user | Nao iniciada | Paginacao, syscalls, loader ELF, libc (secao 3.6) |
| Fase 4 - FS recovery/seguranca | Nao iniciada | Journal, fsck, integridade autenticada (secao 3.4) |
| Fase 5 - Rede completa + update | Parcial (stack existe) | Sockets, TLS, download real (secao 3.1) |
| Fase 6 - Desktop foundation | Nao iniciada | Mouse, compositor, toolkit (secao 3.5) |
| Fase 7 - Apps basicos | Nao iniciada | Terminal grafico, file manager, editor |
| Fase 8 - Pacotes/update auto | Estrutura inicial | Download, assinatura, rollback A/B |
| Fase 9 - SDK/plataforma apps | Nao iniciada | ABI, libsys, formato de app |
| Fase 10 - CapyLang | Nao iniciada | Parser, VM, bindings |
| Fase 11 - Seguranca de produto | Nao iniciada | Secure Boot, ACL, firewall |
| Fase 12 - Ecossistema | Futuro | Browser, Python, compat Linux |

---

## 3. Plano detalhado de melhorias por dominio

### 3.1 Rede (B1) - Em andamento

**Estado atual:** e1000 funcional com IPv4/DHCP/DNS/ICMP em QEMU e VMware.
Hyper-V bloqueado por plataforma hibrida.

**O que falta para concluir B1:**
1. Fechar backend Hyper-V NetVSC (StorVSC primeiro, depois NetVSC)
   - Prerequisito: saida segura do modo hibrido em Hyper-V
   - Sequencia: H1->H2->H3->H4->H5->H6->H7 do `hyperv-network-reset-plan.md`
2. Camada de sockets minima para kernel
   - API: `socket()`, `bind()`, `connect()`, `send()`, `recv()`, `close()`
   - Primeiro TCP client funcional
3. DNS cache em memoria
4. Base para TLS (depende de sockets)

**Melhorias adicionais identificadas:**
- `stack.c` ainda tem ~500 linhas; considerar mais uma quebra (separar estado global/config de protocol handlers)
- Falta teste unitario para DHCP lease parsing
- Falta teste unitario para ARP table overflow
- `net_stack_stats` nao tem contadores de erros por protocolo
- Nao ha mecanismo de timeout configuravel para operacoes de rede
- `tulip` driver esta em estado experimental sem smoke

### 3.2 Scheduler e Multitarefa (B2) - Nao iniciado

**Para concluir esta etapa:**
1. Definir `struct task` (pid, estado, stack pointer, contexto de registradores)
2. Implementar run queue simples (lista encadeada ou array circular)
3. Context switch basico em assembly x64
4. Scheduler cooperativo (`yield()`) como primeiro passo
5. Timer tick via PIT/APIC para preempcao
6. Primitivas de sincronizacao: spinlock, mutex, semaforo
7. Idle task
8. Worker pool para I/O, crypto e rede

**Prerequisitos:**
- Timer nativo pos-EBS funcional
- GDT/IDT x64 nativos (ja existem)
- TSS configurado para troca de pilha

**Criterio de aceite:**
- Shell e servicos rodam como tarefas separadas
- Tarefas lentas nao bloqueiam input
- `make test` e smokes continuam passando

### 3.3 Servicos e Observabilidade (B3) - Parcialmente iniciado

**Estado atual:** `service_manager` existe com 3 servicos (logger, networkd,
update-agent), `work_queue` com 1 job (recovery-snapshot), `service_boot_policy`
com degradacao automatica.

**O que falta para concluir:**
1. Ligar servicos ao scheduler real (hoje e poll cooperativo no main loop)
2. Adicionar servicos: `fsckd`, `dhcpd`, `syslogd`
3. Logs persistentes com rotacao (klog existe; falta rotacao e flush periodico)
4. Crash dump: salvar registradores e stack trace em fault handler
5. Metricas de boot: tempo boot->login, tempo login->prompt
6. Health check estruturado no boot (storage ok? rede ok? servicos ok?)

**Melhorias adicionais:**
- `klog` ring buffer tem apenas 256 slots de 128 bytes; considerar expandir
- `klog_persist` nao tem rotacao de arquivo de log
- Falta observabilidade de uso de memoria (`kmem` nao expoe stats)
- Falta comando `print-mem` ou `mem-status` na CLI

### 3.4 Filesystem, Recovery e Seguranca (C1) - Nao iniciado

**CAPYFS - O que falta:**
1. **Journal/WAL** - Critico
   - Alocar regiao de journal no superblock
   - Antes de cada operacao de metadata (create/delete/rename), gravar intent no journal
   - Replay no mount apos shutdown sujo
   - Teste de power-loss simulado
2. **fsck.CAPYFS** - Ferramenta de host
   - Validar superblock, bitmaps, inodes, arvore de diretorios
   - Detectar blocos marcados como livres mas referenciados (e vice-versa)
   - Detectar inodes orfaos
   - Modo reparo com confirmacao
3. **Versionamento de superblock**
   - Adicionar campo `compat_flags` e `incompat_flags` ao `capy_super`
   - Permitir evolucao sem reformatacao
4. **Integridade autenticada**
   - HMAC ou tag de autenticacao por bloco de metadata
   - Detectar tampering silencioso
5. **Escalabilidade**
   - Nomes variaveis (hoje fixo em 32 bytes)
   - Extent-based mapping (hoje direct+indirect)
   - Quotas por usuario

**Seguranca - O que falta:**
1. Comparacao constante de hash (`secure_clear` existe; falta `constant_time_compare`)
2. Lockout apos N tentativas de login falhas
3. Politica de senha minima
4. Auditoria de login (tentativas falhas -> log persistente)
5. Rotacao de chave do volume sem reformatacao
6. Key slots (multiplos usuarios podem desbloquear o volume)
7. ACL por arquivo/diretorio (hoje so uid/gid/perm basico)

### 3.5 GUI e Mouse (C2) - Nao iniciado

**Prerequisitos:**
- Scheduler funcional (B2)
- Input de mouse funcional (PS/2 mouse driver + USB HID)

**Para concluir:**
1. Driver de mouse PS/2 (3 bytes: buttons + dx + dy)
2. Driver de mouse USB HID (depende de XHCI funcional)
3. Cursor de hardware ou software sobre framebuffer
4. Compositor simples: surfaces, z-order, dirty rects, double buffer
5. Protocolo de eventos (mouse move, click, drag, key)
6. Gerenciamento de foco
7. Toolkit minimo: window, button, label, text input, list, dialog, menu
8. Font renderer (bitmap fonts como primeiro passo)
9. Sessao grafica: login grafico, launcher, terminal grafico

### 3.6 Memoria Virtual e Userland (Fase 3) - Nao iniciado

**Para concluir:**
1. Page allocator fisico (buddy allocator ou bitmap)
2. Mapeamento de paginas (page tables x86_64 com 4 niveis)
3. Espaco de enderecamento por processo (kernel em upper half, user em lower half)
4. Page fault handler
5. Syscall entry via `syscall`/`sysret`
6. Definir ABI de syscalls inicial (open, read, write, close, exec, exit, fork/spawn)
7. Loader ELF para binarios de userspace
8. `libc` minima do sistema
9. Migrar shell para userspace como primeiro teste

### 3.7 Update e Distribuicao (Fase 5/8) - Estrutura inicial

**Estado atual:** update-agent com staging/channel local, sem download.

**O que falta:**
1. Cliente HTTP/HTTPS minimo (depende de sockets + TLS)
2. Download real de manifesto remoto do GitHub Releases
3. Validacao de checksum SHA-256 do payload
4. Assinatura Ed25519 de manifestos e artefatos
5. Boot slot A/B para rollback
6. Health check pos-boot: se novo kernel nao confirmar saude, rollback
7. `pkgd` para instalar/remover pacotes locais
8. Formato de pacote definido

---

## 4. Melhorias novas nao cobertas pelos planos existentes

### 4.1 Qualidade de codigo e build

| Melhoria | Impacto | Esforco |
|---|---|---|
| `compile_flags.txt` ainda tem `-m32 -D__i386__` | Confusao de ferramentas de IDE | Trivial |
| Diretorios vazios: `src/installer/`, `src/lib/`, `src/system/` | Ruido no repositorio | Trivial |
| `kernel_main.c` com ~2866 linhas | Dificuldade de manutencao | Medio (mais extracoes) |
| Sem CI automatizado (GitHub Actions) | Regressoes manuais | Medio |
| Sem analise estatica (cppcheck, clang-tidy) | Bugs latentes | Baixo |
| Sem fuzzing de parsers (CAPYFS, network, command parser) | Vulnerabilidades | Medio |
| `.gitignore` bloqueia `*.txt` (exceto compile_flags) | Pode ignorar docs .txt futuros | Baixo |

### 4.2 Performance

| Melhoria | Impacto | Esforco |
|---|---|---|
| `#pragma GCC optimize("O0")` no kernel_main.c | Performance degradada sem motivo | Trivial (remover ou mover para debug) |
| Sem profiling de boot | Impossivel medir otimizacoes | Medio |
| Buffer cache sem read-ahead | I/O sequencial lento | Medio |
| Buffer cache sem write-back periodico | Depende de `do-sync` manual | Medio |
| Sem batch operations em CLI | Operacoes em lote lentas | Baixo |
| NVMe single queue | Nao aproveita hardware | Alto |

### 4.3 Resiliencia

| Melhoria | Impacto | Esforco |
|---|---|---|
| Sem watchdog de boot | Boot travado sem diagnostico | Medio |
| Sem panic handler com dump | Crash silencioso (halt loop) | Medio |
| Sem checksum de superblock | Corrupcao silenciosa | Baixo |
| Ramdisk como fallback unico | Sem recovery real | Alto |
| `kmem` sem stats/limites | Alocacao sem controle | Medio |

### 4.4 CLI e UX

| Melhoria | Impacto | Esforco |
|---|---|---|
| Sem historico de comandos | UX ruim | Medio |
| Sem autocomplete de paths/comandos | UX ruim | Medio |
| Sem pipes (`cmd1 \| cmd2`) | Limitacao de poder | Alto |
| Sem redirecionamento (`> arquivo`) | Limitacao de poder | Medio |
| Sem variavel de ambiente expansivel | Limitacao de scripts | Medio |
| `page` sem scroll interativo | Arquivos grandes ilegíveis | Baixo |
| Sem comando `grep`/`search` inline | Falta ferramenta de busca em conteudo | Baixo |

### 4.5 Multiusuario

| Melhoria | Impacto | Esforco |
|---|---|---|
| Sem `del-user` | Impossivel remover usuarios | Baixo |
| Sem grupos (`add-group`, `del-group`) | Sem gestao de permissao por grupo | Medio |
| Sem `passwd` separado de `set-pass` (UX) | Confuso para usuario final | Trivial |
| Sem expiracao de sessao/idle timeout | Seguranca basica | Baixo |
| Sem heranca de permissao em novos arquivos | Permissoes inconsistentes | Medio |

---

## 5. Sequencia recomendada de execucao

### Imediato (0.8.x - Estabilizacao)

1. **Corrigir `compile_flags.txt`** - remover `-m32 -D__i386__` (trivial)
2. **Remover `#pragma GCC optimize("O0")` do kernel_main.c** (avaliar impacto)
3. **Limpar diretorios vazios** (`src/installer/`, `src/lib/`, `src/system/`)
4. **Fechar B1** - concluir rede funcional em VMware e avanca Hyper-V conforme plan
5. **Adicionar `constant_time_compare`** na camada de auth

### Curto prazo (0.9.x - Hardening)

6. **Timer nativo PIT/IRQ0** pos-ExitBootServices
7. **Panic handler com dump** de registradores e stack trace
8. **klog com rotacao e flush periodico**
9. **Checksum de superblock** no CAPYFS
10. **Historico de comandos** na CLI
11. **Lockout de login** apos tentativas falhas
12. **PS/2 IRQ-driven** para input confiavel

### Medio prazo (0.10.x - Concorrencia)

13. **Task struct e scheduler cooperativo**
14. **Context switch x64**
15. **Spinlock/mutex basicos**
16. **Worker pool para I/O**
17. **Servicos como tarefas reais** (migrar service_manager do poll para task)
18. **Autocomplete** na CLI

### Medio-longo prazo (0.11.x-0.12.x - Userland e FS)

19. **Journal/WAL** para CAPYFS
20. **fsck.CAPYFS** ferramenta de host
21. **Syscall ABI** e entry via `syscall/sysret`
22. **Page allocator e paginacao x64**
23. **Loader ELF** para userspace
24. **libc minima**
25. **Sockets** para userland

### Longo prazo (0.13.x+ - Rede/Update/GUI)

26. **TLS minimo** (depende de sockets)
27. **Download real de updates** via HTTP/TLS
28. **Assinatura Ed25519** de artefatos
29. **Boot slot A/B** com rollback
30. **Mouse PS/2 + cursor**
31. **Compositor** simples
32. **Toolkit** minimo
33. **Terminal grafico**

### Futuro (0.17.x+ - Plataforma)

34. **Package manager** completo
35. **SDK** documentado
36. **CapyLang** - linguagem propria
37. **Browser** / apps complexos
38. **Compatibilidade Linux**

---

## 6. Resumo de prioridades criticas

As 10 acoes mais importantes para a saude do projeto agora:

1. **Fechar B1 (rede)** - e o bloqueio atual; VMware funciona, Hyper-V e o desafio
2. **Timer nativo** - prerequisito para scheduler e tudo que vem depois
3. **Panic handler** - sem ele, bugs de kernel sao invisíveis
4. **Journal do CAPYFS** - sem ele, power loss pode corromper o volume
5. **Scheduler cooperativo** - sem ele, o sistema e single-task para sempre
6. **Historico de CLI** - UX basica que faz diferenca enorme no dia a dia
7. **Lockout de auth** - seguranca basica que deve existir antes de rede ativa
8. **CI automatizado** - detectar regressoes antes do merge
9. **Remover legado x86_32** - divida que so cresce com o tempo
10. **Sockets** - sem eles, nao existe TLS, update nem apps de rede

---

## 7. Referencias cruzadas

- `docs/plans/system-master-plan.md` - fases 0-12, visao completa
- `docs/plans/system-delivery-roadmap.md` - entregas A1-D3, estado atual
- `docs/plans/system-roadmap.md` - roadmap por dominio
- `docs/plans/platform-hardening-plan.md` - marcos A-E
- `docs/plans/refactor-plan.md` - migracao x64
- `docs/plans/hyperv-network-reset-plan.md` - rede Hyper-V
- `docs/plans/network-hyperv-refactor-and-update-plan.md` - refatoracao rede
- `docs/architecture/system-overview.md` - arquitetura atual
- `docs/testing/boot-and-cli-validation.md` - roteiro de testes
- `docs/reference/cli-reference.md` - comandos implementados
