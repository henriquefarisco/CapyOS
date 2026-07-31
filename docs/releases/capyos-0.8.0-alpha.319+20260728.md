# CapyOS 0.8.0-alpha.319+20260728

**Data:** 2026-07-28
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.319+20260728`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** gate externo novo + observabilidade + fixes (candidata não promovida)

> **Resultado histórico:** esta candidata não foi promovida. O gate entregue
> aqui encontrou, na sua primeira execução real, uma regressão de boot anterior
> à lógica de update e a Etapa 8 continuou aberta. A interpretação inicial de
> execução em `.rodata` foi posteriormente descartada: a auditoria
> [`alpha.320`](../reference/integration/compatibility-audit-2026-07-30-alpha320.md)
> registra a causa real (red zone no loader UEFI), a correção e os boots KVM
> confirmados.

## Resumo executivo

Esta release entrega o **gate que produz a evidencia** do unico criterio que resta
na Etapa 8 -- update assinado baixado, validado, aplicado no slot inativo, com
consumo do token de tentativa pelo UEFI, confirmacao de saude pos-reboot e
rollback real. O criterio deixa de depender de runbook manual no nivel de
ferramenta, mas **nao esta fechado**: o gate ainda nao produziu evidencia porque
o kernel nao boota (ver Bloqueador).

`make smoke-x64-vmware-update-ab` (aceite oficial VMware+UEFI+E1000) e
`make smoke-x64-qemu-update-ab` (preflight) instalam a ISO oficial num disco vazio
e conduzem **dois ciclos completos em quatro power cycles**:

1. boot 1 -- provider A/B pronto, manifesto Ed25519 buscado por HTTP, payload
   verificado por tamanho declarado + SHA-256, escrita no slot inativo com
   autorizacao geracional e flush duravel, uma unica tentativa armada;
2. boot 2 -- o loader gastou a tentativa; `update-rollback-check` reporta a
   tentativa pendente, `update-confirm-health` confirma duravelmente e o segundo
   ciclo e armado;
3. boot 3 -- a tentativa e gasta e deliberadamente **nao** confirmada;
4. boot 4 -- o loader restaurou o slot confirmado e publicou token de ROLLBACK; o
   updater reporta o rollback e desarma o staged update.

## Ancora de confianca de laboratorio (a mudanca sensivel)

A chave privada de release e offline-only e nunca entra em CI nem em runner
automatizado, entao nenhum gate automatizado conseguiria produzir um manifesto que
o kernel aceite. O gate resolve isso gerando **um par Ed25519 descartavel por
execucao** e compilando o kernel com `CAPYOS_UPDATE_LAB_TRUST_KEY_HEX`. O mesmo
macro pina o catalogo em `CAPYOS_UPDATE_LAB_MANIFEST_URL` e libera `payload_url`
com `http://`, porque a pilha TLS do kernel sempre verifica o peer e um servidor
hermetico nao apresenta certificado publicamente confiavel. **Um unico switch
controla as tres relaxacoes**, logo nao existe configuracao intermediaria.

A chave de producao pinada em `src/services/update_agent_parse.c`
(`be230bdd...ae6d`) permanece byte-identica e continua sendo a unica aceita por
qualquer artefato publicavel.

Garantias fail-closed:

- hex malformado nao verifica nada e **nao** cai de volta na chave de producao;
- o boot de um kernel de laboratorio imprime
  `[lab] update trust anchor overridden; kernel not for production`;
- `iso-uefi` recusa um ELF com esse banner fora do proprio target de smoke;
- `release-check` reprova um kernel de release que carregue o banner;
- o fingerprint de variante em `prepare-x64-toolchain` invalida kernel e userland
  ao voltar para a build oficial.

O tooling Python espelha o runtime: `payload_url_prefixes(False)` continua
`("https://", "/system/update/")` e `http://` exige o opt-in explicito
`--allow-lab-http-payload-url` em `build_update_manifest.py` e
`verify_update_manifest.py`.

O que **nao** e provado pelo gate: que a chave de producao especifica esta pinada
e que existe asset assinado por ela publicado. Isso e custodia de chave, nao
engenharia, e fica documentado como passo de operador em
[`../operations/etapa-8-signed-update-playbook.md`](../operations/etapa-8-signed-update-playbook.md) (§4).

## Observabilidade: `-60` deixa de ser opaco

Antes desta release, um `-60` na plataforma oficial era indistinguivel entre falta
de flush, faixa ESP nao vinculada, disco de identidade legada e geracao obsoleta:
os nove motivos existiam apenas como enum, sem nenhum consumidor fora da propria
TU.

- `x64_storage_boot_provider_reason_label` da nome estavel a cada motivo
  (`ready`, `no-persistent-mount`, `no-raw-device`, `no-data-binding`,
  `no-esp-binding`, `no-flush`, `no-control`, `control-unknown`,
  `token-mismatch`).
- `print-boot-slot` passa a imprimir `Boot provider: ready=<yes|no> reason=<label>`
  depois da tabela de slots.
- Quando a capability nao registra, o boot log ganha
  `[boot] provider reason=<label>` junto da linha existente.
- Novo `[boot] A/B attempt slot=<n> state=<confirmed|pending|rollback> generation=<hex>`,
  derivado do token que `kernel_main` ja validava. Isso existe porque a decisao
  A/B do loader sai pela console de firmware, que o contrato VMware mantem
  deliberadamente fora da COM1 (`efi.serialConsole.enabled = "FALSE"`); sem essa
  linha o gate oficial nao teria como afirmar qual slot bootou.

## Correcoes

- **`update-rollback-check` mentia depois de um rollback real.** A linha `[ok]`
  era escolhida por `rc > 0`, valor que `update_agent_check_rollback` nunca
  retorna (um rollback observado e rc `0` com summary distinto). O operador
  recebia "nenhum rollback de boot pendente" logo apos o loader ter revertido, e
  o historico registrava `rollback-check` em vez de `rollback`. Novo campo
  `system_update_status.rollback_applied` carrega o desfecho duravel e decide
  tanto a linha quanto o nome do evento.
- **`http_get` falhava em URL com IP literal.** Todo host ia para o resolvedor
  (`dns_cache_lookup` -> `net_stack_dns_resolve`), entao uma URL apontando para um
  endereco IPv4 cru -- servidor de update na LAN, mirror self-hosted, gateway
  SLIRP do gate -- retornava `HTTP_ERR_DNS` sem nenhuma resolucao ser necessaria.
  Novo `http_parse_ipv4_literal`: estrito (exatamente quatro octetos decimais, sem
  zero a esquerda, sem bytes sobrando), emite a forma canonica de host order de
  `NET_IPV4_ADDR` e mantem qualquer outra coisa no caminho do resolvedor.
- **Textos de ajuda desatualizados desde o `alpha.318`.**
  `update-prepare`, `update-stage` e `update-apply` ainda diziam "indisponivel ate
  existir escrita persistente de boot slot", exatamente as operacoes que o
  `alpha.318` habilitou.
- **Documentacao contradizendo a arvore.** O plano mestre e a auditoria
  `alpha.317` afirmavam que um slot `VALID` e recusado antes de qualquer I/O. O
  `alpha.318` adicionou a transicao de invalidacao duravel (os dois espelhos de
  controle vao para `FAILED` antes de tocar um byte de payload), entao restage de
  slot `VALID` e legal. Ambos os textos foram corrigidos.

## Mudancas por area

**Kernel / runtime**

- `src/services/update_agent_parse.c` -- ancora de laboratorio gated por macro
  (fail-closed) e `payload_url` `http://` sob o mesmo macro.
- `src/services/update_agent_transact.c` + `include/services/update_agent.h` --
  campo `rollback_applied`.
- `src/config/system_settings.c` -- override do `remote_manifest` de laboratorio.
- `src/arch/x86_64/storage_boot_provider_policy.c` +
  `include/arch/x86_64/storage_boot_provider_policy.h` -- rotulo de motivo.
- `src/arch/x86_64/kernel_main.c` + `include/arch/x86_64/storage_runtime.h` --
  `x64_storage_runtime_current_boot_attempt`.
- `src/arch/x86_64/kernel_shell_runtime.c` -- linhas de boot do provider e do
  token de tentativa; banner de laboratorio.
- `src/shell/commands/extended.c` -- `Boot provider:` em `print-boot-slot`.
- `src/shell/commands/system_control/jobs_updates.c` -- fix do `[ok]` de rollback
  e textos de ajuda.
- `src/net/services/http/url_request_builder.c`, `request_response.c`,
  `internal/http_internal.h` -- `http_parse_ipv4_literal`.

**Gates e tooling**

- `tools/scripts/smoke_x64_update_ab_contract.py` -- literais de runtime, geracao
  da chave de laboratorio, VMX oficial e manifesto de evidencia (24 campos,
  ordem canonica, 12 invariantes booleanos, recusa de recovery key).
- `tools/scripts/smoke_x64_update_ab_flow.py` -- fases de boot compartilhadas
  pelos dois drivers, servidor HTTP do host e asserts de token.
- `tools/scripts/smoke_x64_qemu_update_ab.py`,
  `tools/scripts/smoke_x64_vmware_update_ab.py`,
  `tools/scripts/update_ab_lab_config.py`.
- `tools/scripts/test_update_ab_contract.py` -- contrato host.
- `tools/scripts/update_manifest_common.py`, `build_update_manifest.py`,
  `verify_update_manifest.py` -- opt-in `allow_lab_http`.
- `Makefile` -- macros de laboratorio, guarda de banner em `iso-uefi` e
  `release-check`, alvos `update-ab-selftest`, `smoke-x64-qemu-update-ab` e
  `smoke-x64-vmware-update-ab`.
- `tests/net/test_http_url.c` -- 13 casos novos do parser de IP literal.

**Documentacao**

- `docs/operations/etapa-8-signed-update-playbook.md` (novo).
- `docs/plans/active/capyos-master-plan.md` -- Etapa 8 aberta; §21 aponta a
  Etapa 9, correcao do texto de slot `VALID`.
- `docs/plans/STATUS.md`, `docs/reference/integration/compatibility-matrix.md`,
  `docs/reference/integration/compatibility-audit-2026-07-28-alpha319.md`,
  `docs/reference/cli-reference.md`, `docs/testing/boot-and-cli-validation.md`,
  `docs/security/release-signing.md`.

## Postura fail-closed

`-60` continua significando exclusivamente transicao persistente nao
comprovavel, e agora com motivo legivel. `update-apply` sem digest permanece
recusado por definicao. A relaxacao de `payload_url` e de ancora existe apenas em
build de laboratorio, que nao e publicavel.

## Bloqueador: regressao de boot do kernel

A primeira execucao real de `make smoke-x64-qemu-update-ab` chegou ate o boot do
sistema instalado e parou ali. O loader completa `ExitBootServices` e entrega o
controle (ultimo marker debugcon `J` em `efi_main.c`), mas o kernel morre com
`#UD - Invalid Opcode` **antes do primeiro `dbgcon_putc('H')` de
`kernel_main64`**, com `RIP` dentro do segmento `.rodata` recem-copiado:

```
[UEFI] Copiando seg 0 dst=0x10000000 fsz=0x175B42 msz=0x175B42
[UEFI] Copiando seg 1 dst=0x10176000 fsz=0x103A10 msz=0x103A10
[UEFI] Copiando seg 2 dst=0x1027A000 fsz=0xC20   msz=0x262E000
[UEFI] Kernel A/B slot=0 generation=2 @ 0x10000000
[UEFI] Iniciando ExitBootServices (silencio ate o kernel)...
!!!! X64 Exception Type - 06(#UD - Invalid Opcode) !!!!
RIP - 00000000101761B7      <- .rodata + 0x1B7
```

Ou seja: `_start` transfere execucao para dados em vez de chamar
`kernel_main64`.

**Nao e efeito deste gate nem da ancora de laboratorio.** `make smoke-x64-cli`
com a build oficial (sem nenhum flag de laboratorio) falha exatamente igual, sem
nunca emitir marker de kernel. Isso e coerente com o registro do `alpha.318`
("nao reexecutados: gates de instalador QEMU/VMware"): aquela release tornou o
loader A/B autoritativo (handoff v10, token de tentativa, `boot_slot_store_arm`) e
nunca teve um boot de kernel validado em runtime. O `alpha.317` foi a ultima
release com QEMU/VMware verdes.

O que o gate **ja provou** antes de parar: build do kernel de laboratorio,
geracao e assinatura Ed25519 do manifesto, verificacao canonica do manifesto e do
payload, servidor HTTP do host, geracao da ISO e a instalacao completa da ISO
oficial num disco vazio (wipe, GPT, ESP, BOOT com `CAPYSLT0` + `CAPYAB00`
geracoes 1/2, recovery key redigida).

Roteiro de desbloqueio: bissectar `alpha.317` -> trabalho in-tree do `alpha.318`
com `make smoke-x64-cli`; resolver `0x101761B7` com `objdump -d`/`nm` na build
exata que falhou; corrigir; rodar `make smoke-x64-qemu-update-ab`, depois
`make smoke-x64-vmware-update-ab` e `make release-check`; so entao taggear.

## Validacao

Executado:

- `make update-ab-selftest` -- exit `0` (contrato do gate + self-test do
  manifesto).
- `make test` -- exit `0` ("Todos os testes passaram"), incluindo os casos novos
  de `http_parse_ipv4_literal`.
- `make test-http-url` -- exit `0` (`[PASS] http_url`).
- `make layout-audit` -- exit `0`, zero warnings.
- `make version-audit` -- exit `0` (`current=0.8.0-alpha.319`,
  `extended=0.8.0-alpha.319+20260728`).
- `make all64` + `make iso-uefi` com o toolchain host -- exit `0`, tanto na
  variante oficial quanto na de laboratorio.

Reprovado:

- `make smoke-x64-qemu-update-ab` -- **FAIL** no boot do sistema instalado
  (`#UD`, ver Bloqueador).
- `make smoke-x64-cli` -- **FAIL** identico na build oficial; isola a regressao
  do gate.

Nao executado:

- `make smoke-x64-vmware-update-ab` -- sem sentido antes de o boot voltar.
- `make release-check` -- nao se certifica um kernel que nao boota.
- `make modules-index`, `make smoke-x64-vmware-installer-wizard`.

## Limites conhecidos

- O gate prova o mecanismo sob a ancora de laboratorio e transporte HTTP
  hermetico. A publicacao assinada pela chave offline de producao permanece passo
  de operador.
- O indice de modulos pinado continua em `alpha.318` porque
  `src/config/first_boot/modules.c` nao mudou; `make smoke-x64-iso-modules-net`
  segue bloqueado por asset nao publicado (HTTP 404).
- `make modules-index` nao foi reexecutado: nenhuma saida de publisher nem o
  inventario externo mudaram.
- `make smoke-x64-vmware-installer-wizard` nao foi reexecutado; o caminho do
  instalador nao foi tocado.
- Discos anteriores ao `alpha.317` com GUIDs duplicados seguem em modo legado
  validado apenas para mount e nunca recebem capability de update.
- `tests/services/test_update_transact.c` continua em 887/900 linhas; a proxima
  fatia de testes do updater deve dividir a unidade antes de crescer.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.318+20260727` | `0.8.0-alpha.319+20260728` | `capyos-base` v3 e handoff interno v10 inalterados; nenhuma ABI externa mudou |

CapyUI `2.24.1`, CapyAI `0.2.1`, CapyBrowser `0.6.7`, CapyAgent `0.0.10`,
CapyCodecs `0.0.12`, CapyLang `0.1.12` e CapyBenchmark `0.0.11` permanecem
inalterados.

_Build: `0.8.0-alpha.319+20260728`_
