# 🌌 NoirOS

> **NoirOS** é um sistema operacional hobby desenvolvido do zero por **Henrique Schwarz Souza Farisco**.  
> A jornada parte da primeira centelha (**Singularity**) rumo a um ambiente multiusuário cifrado, guiado por uma CLI batizada de **NoirCLI**.

---

## ✨ Visão Geral

NoirOS é um kernel **x86 de 32 bits**, freestanding, que inicializa via **Multiboot**, configura GDT/IDT próprias, remapeia o PIC e habilita IRQs essenciais (PIT + teclado).  
A partir desta base o projeto já entrega:

- **NoirFS** — filesystem cifrado (XTS-AES) com montagem em RAMDisk e derivação de chaves via PBKDF2.
- **Assistente de Primeira Execução** — cria estrutura de diretórios, configura hostname/tema/splash e registra o usuário administrador.
- **Multiusuários** — base de credenciais em `/etc/users.db` com salt + hash PBKDF2.
- **NoirCLI** — shell interativo com comandos nomeados para o universo Noir (`list`, `go`, `mypath`, `print-file` etc.) e relatório de versão/canal em `print-version` / `print-envs`.
- **Temas & Splash** — personalização de cores VGA e animação textual opcional durante o boot.

---

## 🛰️ Estado Atual

- **Boot & Núcleo**
  - Multiboot v1 (`kernel_entry.s`) + GDT mínima (null/code/data) e IDT com handlers para exceções/IRQs.
  - PIC remapeado (0x20/0x28), PIT operando a 100 Hz (`pit_ticks()` disponível) e teclado (IRQ1) com eco/mascara.
- **Memória & Discos**
  - RAMDisk virtual inicializado em 256 blocos de 4 KiB.
  - NoirFS (blocos 4096 B) com inodes, diretórios e bitmaps.
  - Camada de criptografia XTS-AES alimentada por PBKDF2 (16k iterações) a partir da senha digitada no boot.
- **Primeira Execução (wizard)**
  - Cria `/bin`, `/etc`, `/home`, `/tmp`, `/var/log`, `/system`, `/docs`.
  - Pergunta hostname, tema (`noir`, `ocean`, `forest`), splash (on/off) e credenciais do administrador.
  - Gera `/system/config.ini` e `/system/first-run.done` para detectar inicializações futuras.
- **Autenticação + Sessão**
  - Login via terminal (`Usuario:` / `Senha:`) com mascaramento, validação PBKDF2 e abertura da sessão (`SESSION_PATH_MAX = 128`).
  - Sessão mantém `USER`, `ROLE`, `UID`, `GID`, `HOME`, `PWD` e resolve caminhos relativos (`..`, `.`).
- **NoirCLI**
  - Prompt `usuario@hostname>` com temas aplicados.
  - Comandos implementados: `list`, `go`, `mypath`, `print-file`, `page`, `print-file-begin`, `print-file-end`, `mk-file`, `mk-dir`, `print-echo`, `help-any`, `mess`, `bye`, `print-me`, `print-id`, `print-host`, `print-version`, `print-time`, `print-insomnia`, `print-envs`, `do-sync`.
- `help-any`/`help-docs` oferecem consulta rápida dentro do NoirFS.
- **Visual**
  - `system_apply_theme()` configura cores VGA (Noir, Ocean, Forest).
  - `system_show_splash()` apresenta animação textual progressiva quando habilitada.

---

## 🛠️ Stack Tecnológico

- **Linguagens:** C + Assembly (NASM, ELF32 freestanding)
- **Toolchain:** `i686-elf-gcc`/`ld` (`Makefile` inclui fallback `gcc -m32`) — roda em hosts x86_64/hypervisores sem exigir extensões especiais.
- **Bootloader:** Multiboot (GRUB/QEMU) com `kernel_entry.s`
- **Hardware alvo:** x86 32-bit (Protected Mode)
- **Drivers atuais:** VGA texto, teclado PS/2 (set 1), PIT, RAMDisk + camada de blocos
- **Criptografia:** SHA-256 + PBKDF2 + AES-XTS (software puro, sem aceleração HW)

### Compatibilidade x64 / Recursos
- Kernel 32-bit protegido: executa em CPUs x86_64 via modo legado ou VMs BIOS/MBR (UEFI não suportado).
- Memória: heap interno de 2 MiB; validado em VMs até 256 MiB de RAM (ajuste `KHEAP_SIZE` se precisar mais).
- Disco: blocos lógicos de 4096B via wrapper; cabe até ~2 TiB (limite de 32 bits do contador de blocos).
- Boot: requer BIOS/MBR. Sem bootloader no disco, use a entrada da ISO para carregar o kernel.

---

## ⚙️ Build, Execução e Fluxo de Uso

```bash
make clean && make          # compila bootloader + 2 kernels (NoirOS e Instalador)
make run                    # executa o kernel do NoirOS diretamente no QEMU

# Disco persistente (IDE emulado)
make disk-img               # cria build/disk.img (128 MiB por padrão; ajuste via DISK_SIZE=256M)
make run-disk               # NoirOS + -drive file=build/disk.img,if=ide

# ISO SOMENTE DO INSTALADOR (NGIS) — requer grub-mkrescue/xorriso
make iso                    # gera build/NoirOS-Installer.iso (NGIS)
make run-installer-iso      # QEMU com a ISO do instalador e o disco em build/disk.img
# A ISO inclui menu "NoirOS (boot via ISO + disco instalado)" para usar o kernel direto da ISO como bootloader

# (Opcional) deixar o disco bootável (GRUB no MBR) e iniciar sem -kernel
sudo make disk-bootable     # particiona build/disk.img (sda1 ext2/GRUB + sda2 NoirFS) e instala o GRUB
make run-disk-boot          # inicia o QEMU em -boot c (GRUB -> NoirOS)
```

Fluxo recomendado:

1. **Instalação (NGIS)** — inicialize com a ISO do instalador:
   - Lista discos detectados (ex.: `ata0-master`) e permite escolher o alvo.
   - Cria tabela MBR com 2 partições: `sda1` BOOT (16–100 MiB, padrão 64 MiB) e `sda2` dados (NoirFS cifrado).
   - Define a senha do NoirFS e formata a partição de dados.
   - Executa o assistente para hostname, tema (`noir`, `ocean`, `forest`), splash (on/off) e cria usuários.
   - Escreve `/system/config.ini` e o marcador `/system/first-run.done` no NoirFS.
2. **Primeiro boot do NoirOS** — inicie o kernel do NoirOS apontando para o mesmo disco:
   - Informe a senha do NoirFS para montagem do volume.
   - Vai direto ao login (sem assistente, pois a configuração foi feita no instalador).
   - Sem GRUB no disco, mantenha a ISO anexada e escolha o menu "NoirOS (boot via ISO + disco instalado)".
3. **NoirCLI** — prompt `user@host>`; use `help-any`/`help-docs` para consultar a lista de comandos.

Saude do build:
- `make test` executa testes unitários em modo host para validar wrappers de bloco e parsing do MBR.

Notas sobre boot pelo disco:
- O instalador cria a tabela MBR e reserva `sda1` para BOOT. Para tornar o disco bootável sem ISO, é necessário instalar um bootloader (ex.: GRUB) no MBR/BOOT.
- Use `sudo make disk-bootable` no host (Linux) para instalar GRUB no `build/disk.img` já particionado, ou replique os passos com seu disco/VDI via loop device.
- O NGIS detecta automaticamente a `sda2` (partição 2) e formata/monta o NoirFS lá.

> **Dica**: Primeiro entre como `super-admin`. Depois, `list`, `mypath`, `hunt-any <padrao>`, `stats-file <alvo>` e `clone <src> <dst>` dão uma boa visão do novo fluxo.

---

## 📚 Documentação Complementar

- `docs/noiros-cli-reference.md` — detalha cada comando NoirCLI.
- `docs/releases/` — changelog por build (ex.: `0.7.1-alpha.1`).
- `VERSION.yaml` — manifesto oficial de canais (`alpha`, `beta`, `stable`) com histórico resumido.
- `include/` — headers comentados com contratos de API (VGA, TTY, VFS, NoirFS etc.).

---

## 🚀 Roadmap Próximo (v0.2 “Inflaton”)

1. **Scheduler & Jobs** — base para `print-ps`, `watch`, `run-bg/fg` com tarefas cooperativas.
2. **Pipelines & Redirecionamento** — ampliar o NoirCLI com operadores `|` e `>`, incluindo histórico persistente.
3. **Rede inicial** — abstrações de link + comandos `print-ip`, `tickle`, `print-ports`, `ask-dns`.
4. **Permissões avançadas** — suporte a grupos extras/ACLs e utilitários `config-*` completos.
5. **Splash aprimorado** — frames ASCII externos carregados de `/system/splash/`.
6. **Tests automáticos** — harness mínimo para validar NoirFS/VFS em modo host.

---

## 📜 Licença

MIT — use, estude, evolua.
