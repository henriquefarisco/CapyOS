# CapyOS 0.8.0-alpha.320+20260730

**Data:** 2026-07-30
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.320+20260730`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** correcao de boot/instalacao + hardening da esteira de release

## Resumo executivo

`alpha.320+20260730` restaura o boot e a instalacao UEFI. A causa raiz da
regressao da `alpha.319` era o uso da red zone da ABI SysV no loader UEFI:
firmware e interrupcoes podem sobrescrever essa area, corrompendo o frame antes
da entrada do kernel. O loader agora e compilado sem red zone, reserva espaco
real de pilha para seus frames, valida o ELF, copia e compara cada segmento e
recarrega a GDT.

Dois ciclos KVM focados passaram, com dois boots cada e persistencia preservada
(quatro boots no total). O smoke oficial da ISO em KVM tambem passou: boot 0
instalou a partir da ISO, boot 1 iniciou o disco instalado e boot 2 confirmou
persistencia pelo marker `marker:persist-ok`. O smoke CLI em TCG passou em
86,2 s, com dois boots e persistencia. O smoke oficial da ISO em TCG, o ciclo
update A/B e os gates VMware ainda nao foram executados; por isso a Etapa 8
permanece ativa em 7/16.

O conjunto coordenado tambem avanca CapyUI de `2.24.1` para `2.24.2`. Trata-se
de hardening exclusivo da cadeia de release: tag, `VERSION`, `PUBLISH_TAG` e
checkout passam a ser validados de forma fail-closed; as ABIs widget 2.22,
desktop-session v1 e display-list schema 7 permanecem inalteradas.

## Mudancas

- **Boot UEFI:** compilacao do loader sem red zone, fazendo cada helper reservar
  explicitamente o proprio frame. O `objdump` da build corrigida confirma uso
  da pilha real, sem dependencia dos 128 bytes abaixo de `RSP`.
- **Carga do kernel:** validacao estrutural dos segmentos ELF antes da copia,
  copia com comparacao byte a byte e falha fechada em divergencia.
- **Entrada x86_64:** recarga completa da GDT antes de entrar no kernel,
  eliminando dependencia residual do estado deixado pelo firmware.
- **Instalacao:** o smoke oficial KVM cobre instalacao pela ISO, primeiro boot
  do disco e segundo reboot com persistencia.
- **Indice de modulos:** contrato imutavel da release passa a conter exatamente
  nove entradas HTTPS unicas, cada uma com tamanho e SHA-256. A verificacao
  remota pos-publicacao continua bloqueada ate a tag e os assets existirem. Os
  dois payloads CapyUI apontam para a tag imutavel `v2.24.2` e seus metadados
  publicados ficam fixados no catalogo CapyOS.
- **Esteira:** actions externas foram pinadas por SHA nas versoes
  `actions/checkout` 7.0.1, `actions/setup-python` 7.0.0,
  `github/codeql-action` 4.37.3, `ossf/scorecard-action` 2.4.4 e
  `softprops/action-gh-release` 3.0.2.
- **Seguranca:** o achado CodeQL #121 ganhou fixture de regressao local; o
  fechamento remoto depende do proximo scan. Os avisos Scorecard #50
  (`CodeReviewID`) e #49 (`CIIBestPracticesID`) sao requisitos de governanca
  externa e nao sao declarados como corrigidos por mudanca de codigo.

## Validacao

| Evidencia | Resultado registrado ate 2026-07-31 |
|---|---|
| `objdump` do loader corrigido | passa; pilha real e ausencia de red zone confirmadas |
| validacao ELF + copia/comparacao de segmentos | passa nos dois ciclos KVM focados |
| recarga da GDT e entrada no kernel | passa nos dois ciclos KVM focados |
| KVM focado, 2 ciclos x 2 boots | passa; 4 boots e persistencia |
| smoke oficial ISO KVM | passa; instalacao, boot do disco e reboot persistente (`marker:persist-ok`); runner: `[ok] smoke x64 ISO install + persistence passed` |
| `make smoke-x64-cli` em TCG | passa em 86,2 s; dois boots e persistencia |
| smoke oficial ISO TCG | **pendente; ainda nao executado** |
| `smoke-x64-qemu-update-ab` | **pendente; ciclo update A/B ainda nao executado** |
| `smoke-x64-vmware-update-ab` | **pendente; aceite oficial da Etapa 8** |
| indice de nove modulos, verificacao pos-publicacao | **pendente ate tag/assets publicados** |

Os gates ainda pendentes nao devem ser inferidos como verdes a partir dos
smokes ja aprovados. A release so pode ser promovida depois que os gates
obrigatorios da esteira terminarem e a verificacao remota dos artefatos aceitar.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.319+20260728` | `0.8.0-alpha.320+20260730` | restaura boot/instalacao; `capyos-base` v3, package v1 e handoff v10 inalterados |
| **CapyUI** | `2.24.1` | `2.24.2` | hardening supply-chain-only; widget 2.22, desktop-session v1 e schema 7 inalterados |

Sem mudanca de ABI externa. Permanecem pinados: CapyUI `2.24.2`, CapyAI `0.2.1`,
CapyBrowser `0.6.7`, CapyCodecs `0.0.12`, CapyAgent `0.0.10`, CapyLang `0.1.12`
e CapyBenchmark `0.0.11`.

_Build: `0.8.0-alpha.320+20260730`_
