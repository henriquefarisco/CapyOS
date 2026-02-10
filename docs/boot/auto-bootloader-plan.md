# Plano: Partição de Boot e Recuperação Automática (ISO)

Objetivo: durante a instalação via ISO, criar e preparar automaticamente uma partição BOOT pequena com um bootloader próprio (ou GRUB embarcado) capaz de inicializar o NoirOS sem depender do host. A partição deve conter:
- MBR com stage1 capaz de localizar e carregar um stage2 seguro.
- stage2 que carregue o kernel NoirOS (multiboot) e exponha um menu de recuperação.
- Manifesto de recuperação e alvos de fallback (ex.: kernel alternativo / modo de recuperação).

## Requisitos funcionais
- Instalação “hands-off”: após formatar, o disco deve dar boot direto sem passos manuais.
- Particionamento automático: BOOT (pequena, BIOS/MBR, não cifrada) + DATA (NoirFS cifrada).
- Boot resiliente: se o kernel principal falhar, oferecer entrada de recuperação / kernel alternativo.
- Compatibilidade Hyper-V: modo texto, sem dependência de VESA; terminal serial habilitado.

## Arquitetura proposta
1) **Stage1 (MBR)**: código de 512B que:
   - Usa INT13h LBA estendido; carrega stage2 de um LBA conhecido (partição BOOT).
   - Carrega a unidade (DL) detectada pelo BIOS e passa para stage2.

2) **Stage2 (loader multiboot minimalista)**:
   - Lê um manifesto fixo na partição BOOT (`BOOT_MANIFEST`, setor logo após o stage2).
   - Manifesto contém: LBA + tamanho dos kernels (`noiros.bin`, `noiros-recovery.bin`), checksum e caminho textual para logs.
   - Carrega o ELF do kernel para memória, interpreta program headers e salta para a entry com EAX=MultibootMagic, EBX=ptr multiboot_info (mínimo viável).
   - Oferece menu simples em modo texto/serial (Hyper-V safe) com timeout e opções: normal / recovery.

3) **Partição BOOT (não cifrada)**:
   - Layout simples (raw, sem FS) para manter contiguidade: [stage2][manifest][kernel principal][kernel recovery].
   - Tamanho planejado: 32–64 MiB (ajustável), alinhamento 1MiB.

4) **Instalador (NGIS)**:
   - Após criar BOOT/DATA, escreve stage1 no MBR, stage2+manifest+kernels na partição BOOT.
   - Manifesto inclui checksums para validar leitura; se inválido, stage2 cai em recovery/serial.
   - Mantém kernel principal em /system para operação normal; BOOT contém cópia só para boot.

5) **Recuperação**:
   - Kernel recovery é construído junto da ISO (perfil mínimo: monta DATA em read-only e exporta logs via serial).
   - Manifesto permite adicionar entradas de recuperação futuras sem recompilar stage1.

## Passos técnicos (incremental)
- [ ] Gerar binários `stage1.bin` (MBR) e `stage2.bin` (loader multiboot) no build host.
- [ ] Definir formato do manifesto (ex.: magic `NIBT`, version, entries com {type, lba, bytes, crc32}).
- [ ] Estender Makefile para produzir `noiros.kernel.raw` e `noiros.recovery.raw` (ELF intacto) e empacotar no ISO.
- [ ] NGIS: escrever BOOT layout (stage2 + manifesto + kernels) após particionar.
- [ ] NGIS: validar escrita e re-gerar manifesto em caso de falha (retry).
- [ ] Testar boot em Hyper-V Gen1, QEMU (BIOS) e VM sem VESA.
- [ ] Acrescentar modo “chainload ISO” como último recurso (stage2 tenta CD se disco falhar).

## Considerações de risco
- Precisamos de um carregador ELF em 32 bits protegido (stage2) — inclui habilitar A20/GDT/PM antes do salto.
- Core.img/GRUB embarcado é alternativa, mas exige gerar blocklists estáveis; optar por loader próprio evita dependência externa.
- Garantir contiguidade dos binários na partição BOOT para simplificar stage2 e evitar blocklists dinâmicos.
- Recuperação deve ser assinada/checksum para evitar boot de imagem corrompida.

## Métricas de aceite
- Boot direto do disco instalado (sem ISO) em Hyper-V Gen1.
- Menu de recuperação funcional via modo texto/serial.
- Instalação 100% automatizada: zero comandos no host após formatar.
