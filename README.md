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
- **NoirCLI** — shell interativo com comandos nomeados para o universo Noir (`list`, `go`, `mypath`, `print-file` etc.).
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
- **Toolchain:** `i686-elf-gcc`/`ld` (`Makefile` inclui fallback `gcc -m32`)
- **Bootloader:** Multiboot (GRUB/QEMU) com `kernel_entry.s`
- **Hardware alvo:** x86 32-bit (Protected Mode)
- **Drivers atuais:** VGA texto, teclado PS/2 (set 1), PIT, RAMDisk + camada de blocos
- **Criptografia:** SHA-256 + PBKDF2 + AES-XTS (software puro, sem aceleração HW)

---

## ⚙️ Build, Execução e Fluxo de Uso

```bash
make clean && make      # compila bootloader + kernel
make run                # QEMU (-kernel build/kernel.bin -m 64)
```

1. **Senha do volume cifrado** — ao iniciar, digite a senha ou pressione Enter para usar `noiros-passphrase`.
2. **Primeira Execução** — se NoirFS não estiver configurado, o assistente perguntará:
   - Hostname, tema e animação de splash.
   - Senha do `super-admin` (conta raiz). O diretório `/home/super-admin` é criado automaticamente.
   - Opcionalmente, um segundo administrador personalizado.
3. **Login** — sempre requisitado após o boot.
4. **NoirCLI** — prompt `user@host>`; use `help-any`/`help-docs` para consultar a lista de comandos.

> **Dica**: Primeiro entre como `super-admin`. Depois, `list`, `mypath`, `hunt-any <padrao>`, `stats-file <alvo>` e `clone <src> <dst>` dão uma boa visão do novo fluxo.

---

## 📚 Documentação Complementar

- `docs/noiros-cli-reference.md` — detalha cada comando NoirCLI.
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
