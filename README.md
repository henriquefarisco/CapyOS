# CapyOS

![CapyOS symbol](assets/branding/icon.svg)

CapyOS e um sistema operacional experimental, open source, focado na trilha
`UEFI/GPT/x86_64`. O projeto implementa boot UEFI, kernel proprio, desktop
grafico, login, shell, filesystem CAPYFS, rede, criptografia e um pipeline de
release validado por testes automatizados.

Versao de referencia: `0.8.0-alpha.268` (build `0.8.0-alpha.268+20260617`; canal `alpha`; ver `VERSION.yaml`)

## Destaques

- Boot UEFI x86_64 com imagem ISO e disco GPT provisionado.
- Desktop CapyUI com login grafico, taskbar, janelas, apps e terminal.
- Persistencia em disco com CAPYFS e volume `DATA` cifrado.
- CapyCLI com comandos de arquivos, sessao, rede, diagnostico e pacotes.
- Pilha de rede x64 com `E1000`, IPv4, ICMP, TCP, DNS e HTTP/HTTPS.
- Adaptador `capypkg` para receber pacotes Capy remotos verificados
  (SHA-256 + Ed25519 sobre descritor canonico) via `capysh`.
- Gates de release com testes de host, build x64, ISO UEFI e smoke QEMU.

## Screenshots

![Desktop CapyUI v1.1](docs/screenshots/CapyUI/v1.1/desktop-version1.png)

Mais imagens oficiais:

- [Catalogo de screenshots](docs/screenshots/README.md)
- [CapyUI v1.1](docs/screenshots/CapyUI/v1.1/README.md)

## Caminho Suportado

| Area | Estado |
|---|---|
| Arquitetura oficial | `UEFI/GPT/x86_64` |
| VM principal | VMware UEFI com NIC `E1000` |
| VM de laboratorio | QEMU/OVMF com `E1000` |
| BIOS/MBR 32-bit | legado, fora da trilha de release |
| Hyper-V | investigacao, sem suporte oficial |

## Dependencias e Build Rapido

Dependencias principais: `make`, `python3`, `nasm`, `xorriso`, `gnu-efi`,
QEMU/OVMF para smokes e toolchain `x86_64-elf-*` para builds
reprodutiveis.

### Linux / WSL

```bash
./install-linux.sh
source ~/.bashrc
make test
make all64 TOOLCHAIN64=elf
make iso-uefi TOOLCHAIN64=elf
make smoke-x64-iso TOOLCHAIN64=elf
```

`./install.sh` tambem despacha para o instalador Linux quando executado em
Linux/WSL.

Nao execute `make`, `./install.sh` ou `./install-linux.sh` com `sudo` dentro do
workspace. O build recusa execucao como root por padrao para evitar artefatos
`build/` root-owned. Se um checkout antigo ja tiver artefatos bloqueados,
corrija a posse uma vez antes de continuar:

```bash
sudo chown -R "$(id -u):$(id -g)" build ../CapyUI/build 2>/dev/null || true
```

### macOS

```bash
bash install-macos.sh --local-deps Dependencias
make test HOST_CC=clang
```

O modo acima usa apenas ferramentas ja presentes no macOS e arquivos aprovados
em `Dependencias/`; ele nao usa Homebrew, nao baixa fontes, nao instala QEMU e
nao compila a toolchain cruzada.

Quando a compilacao local longa de GCC/binutils for permitida:

```bash
bash install-macos.sh --local-deps Dependencias --with-cross
source ~/.zprofile
make all64 TOOLCHAIN64=elf
```

Quando `gnu-efi`, `xorriso` e a toolchain estiverem aprovados/localmente
disponiveis:

```bash
make iso-uefi TOOLCHAIN64=elf
make smoke-x64-iso TOOLCHAIN64=elf
```

Se o `gnu-efi` local nao for detectado automaticamente, informe `EFI_PREFIX`,
por exemplo:

```bash
make iso-uefi TOOLCHAIN64=elf EFI_PREFIX=/Users/t808981/opt/gnu-efi
```

### Windows

Windows e suportado pelo caminho WSL. Execute em PowerShell:

```powershell
.\install-windows.ps1 -SkipSmoke
wsl -d Ubuntu -- bash -lc "cd /mnt/c/caminho/para/CapyOS && source ~/.bashrc && make test"
wsl -d Ubuntu -- bash -lc "cd /mnt/c/caminho/para/CapyOS && source ~/.bashrc && make all64 TOOLCHAIN64=elf"
wsl -d Ubuntu -- bash -lc "cd /mnt/c/caminho/para/CapyOS && source ~/.bashrc && make iso-uefi TOOLCHAIN64=elf"
```

Use `-Distro <Nome>` se a distro WSL nao se chamar `Ubuntu`, e
`-ProjectPath /mnt/.../CapyOS` se o caminho automatico precisar ser
sobrescrito.

### Gates comuns

```bash
make layout-audit
make version-audit
make boot-perf-baseline-selftest
make smoke-marker-policy-selftest
make release-check TOOLCHAIN64=elf
```

## Documentacao

- [Visao tecnica do projeto](docs/project-overview.md)
- [Indice da documentacao](docs/README.md)
- [Arquitetura](docs/architecture/system-overview.md)
- [Planos e roadmap](docs/plans/README.md)
- [Status executivo](docs/plans/STATUS.md)
- [Release notes](docs/releases/README.md)
- [Referencia do CapyCLI](docs/reference/cli-reference.md)
- [Checklist de PR e release](docs/testing/pr-and-release-checklist.md)
- [Contratos de integracao cross-repo](docs/reference/integration/README.md)
- [Matriz de compatibilidade cross-repo](docs/reference/integration/compatibility-matrix.md)
- [Runbook de deploy manual de modulos remotos](docs/operations/manual-module-deploy-runbook.md)

## Licenca

CapyOS e distribuido sob a licenca `Apache-2.0`.

- [LICENSE](LICENSE)
- [NOTICE](NOTICE)
- [BRANDING.md](BRANDING.md)
- [LAWFUL_USE.md](LAWFUL_USE.md)

Desenvolvedor principal: Henrique Schwarz Souza Farisco.
