# CapyOS 0.9.0+20260821

**Data:** 2026-08-21
**Canal:** stable (producao)
**Versao:** `0.9.0+20260821`
**Plataforma oficial:** VMware + UEFI + E1000
**Tipo:** promocao para release estavel 0.9.0 com consolidacao da trilha UEFI/GPT x86_64

## Resumo executivo

`0.9.0+20260821` consolida as Etapas 1-7 e o trabalho entregue ate aqui na
Etapa 8, promovendo o CapyOS para a versao estavel `0.9.0`. A release formaliza a trilha oficial
`UEFI/GPT/x86_64` com instalador seguro com selecao explicita e deteccao serial
fail-closed, integracao estavel com a suite de modulos desacoplados, sistema de
arquivos CAPYFS com volume `DATA` criptografado e pipeline de release auditado.

O instalador UEFI detecta a presenca de UART 16550 por loopback antes de usa-la,
rejeita estados flutuantes (como `0xFF` em VMs sem serial configurada), preserva
a entrada de teclado da firmware via EFI ConIn e garante operacao deterministica
em hipervisores com ou sem console serial.

## Destaques da versao 0.9.0

- **Trilha oficial UEFI/GPT:** boot nativo x86_64 via `BOOTX64.EFI`, tabela GPT
  com suporte a particoes `ESP`, `BOOT` e `DATA` criptografada.
- **Instalador robusto:** selecao explicita de disco de destino por `PathId`,
  validacao de disco de guarda intacto, confirmacao por token destrutivo literal
  e suporte a maquinas virtuais sem porta COM1.
- **Desktop e UI:** integracao completa com CapyUI 2.24.2 (desktop grafico,
  janelas, terminal, gerenciador de tarefas, editor de texto e sessao protegida).
- **Navegacao e renderizacao:** CapyBrowser 0.6.7 com renderizacao display-list
  estatica, decodificacao de imagens PNG via CapyCodecs 0.0.12 e pilha HTTP/HTTPS
  com TLS BearSSL e gestao de cache/cookies.
- **Assistencia integrada:** CapyAI 0.2.1 governado com execucao assincrona sem
  bloqueio do compositor e politicas de seguranca tipadas.
- **Armazenamento e seguranca:** volumes gerenciados por header v1 com KDF
  Argon2id, HMAC-SHA256 check tag e criptografia AES-XTS.
- **Esteira de release:** automacao CI/CD com verificacao de integridade SHA-256,
  re-download via API autenticada, promocao para Latest e verificacao publica.

## Validacao

Os gates registrados abaixo foram executados e validados:

| Evidencia | Resultado |
|---|---|
| `make release-check TOOLCHAIN64=elf` | passa; testes host, layout audit (0 warnings), version audit, boot perf baseline e checksums validados |
| `smoke-x64-qemu-installer-no-uart` | passa; topologia sem serial comprovada via QEMU device tree, entrada ConIn e integridade de disco |
| `smoke-x64-iso` | passa; instalacao completa, primeiro boot do disco instalado e persistencia pos-reboot |
| `smoke-x64-qemu-installer-wizard` | passa; selecao explicita multi-disco com disco de guarda de 3 GB intacto |
| `smoke_x64_vmware_installer.py` | passa; gate oficial VMware Workstation com multi-disco, login e persistencia |

## Errata de publicacao — 2026-08-24

A release pública 0.9.0 contém sete assets e prova integridade SHA-256, mas não
inclui `release-artifacts.sha256.sig`, chave pública, manifestos públicos nem
`latest.ini` assinado. Portanto ela não constitui evidência de publicação
criptograficamente autenticada e não fecha o ciclo A/B de produção da Etapa 8.
Uma promoção em duas fases está sendo implementada para as próximas releases:
draft com assinatura offline, validação autenticada e uma única transição para
publicação + Latest sob a proteção de releases imutáveis, seguida da
revalidação pelas rotas públicas.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.1` | `0.9.0` | promocao para release estavel |
| **CapyUI** | `2.24.2` | `2.24.2` | pin mantido compativel |
| **CapyBrowser** | `0.6.7` | `0.6.7` | pin mantido compativel |
| **CapyAI** | `0.2.1` | `0.2.1` | pin mantido compativel |
| **CapyCodecs** | `0.0.12` | `0.0.12` | pin mantido compativel |
| **CapyAgent** | `0.0.10` | `0.0.10` | pin mantido compativel |
| **CapyLang** | `0.1.12` | `0.1.12` | pin mantido compativel |
| **CapyBenchmark** | `0.0.11` | `0.0.11` | pin mantido compativel |

_Build: `0.9.0+20260821`_
