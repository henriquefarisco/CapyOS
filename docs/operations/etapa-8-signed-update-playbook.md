# Playbook — gate do ciclo A/B assinado (Etapa 8)

> **Estado em `alpha.320`:** a regressão de boot foi corrigida no loader UEFI
> (pilha real sem red zone, ELF validado/copiado/comparado e GDT recarregada).
> Dois ciclos KVM focados passaram, com quatro boots e persistência; o smoke
> oficial ISO KVM também passou com instalação, boot do disco e
> `marker:persist-ok`. O smoke CLI TCG passou em 86,2 s, com dois boots e
> persistência. O smoke ISO TCG oficial, o update A/B e os gates VMware ainda
> não foram executados. A Etapa 8 permanece aberta em 7/16 até o ciclo A/B
> VMware produzir evidência completa.

Este playbook cobre o gate que fecha o critério de update da Etapa 8: um
manifesto Ed25519 é buscado por HTTP, o payload é verificado, gravado no slot
inativo e armado para **uma única** tentativa; o loader UEFI consome essa
tentativa; o boot resultante confirma saúde; e um segundo ciclo deixado sem
confirmação é revertido pelo loader e reportado pelo updater.

- Aceite oficial: `make smoke-x64-vmware-update-ab` (VMware + UEFI + E1000).
- Feedback de desenvolvimento: `make smoke-x64-qemu-update-ab`.
- Contrato host (roda em `release-check`): `make update-ab-selftest`.

## 1. Por que existe uma âncora de confiança de laboratório

A chave privada de release é offline-only e nunca entra em CI ou em runner
automatizado (ver [`../security/release-signing.md`](../security/release-signing.md)).
Sem uma segunda âncora, nenhum gate automatizado conseguiria produzir um
manifesto que o kernel aceite, e o critério ficaria dependente de uma execução
manual com material sensível.

O gate resolve isso gerando **um par Ed25519 descartável por execução** e
compilando o kernel com:

- `CAPYOS_UPDATE_LAB_TRUST_KEY_HEX=<hex64 da chave pública crua>` — substitui a
  âncora de confiança do manifesto;
- `CAPYOS_UPDATE_LAB_MANIFEST_URL=<url>` — pina o catálogo no endpoint hermético
  do host (a pilha TLS do kernel sempre verifica o peer, então um servidor local
  não consegue apresentar certificado publicamente confiável).

O mesmo flag libera `payload_url` com `http://`. Um único switch controla as três
relaxações, então não existe configuração intermediária.

### Postura fail-closed

- Hex malformado **não** cai de volta na chave de produção: a verificação falha.
- `iso-uefi` recusa um ELF que contenha o banner de laboratório fora do próprio
  target de smoke (`ISO_REUSE_X64_VARIANT=1`).
- `release-check` reprova qualquer kernel de release que carregue o banner.
- O fingerprint de variante em `prepare-x64-toolchain` invalida kernel e userland
  ao voltar para a build oficial, então não há reuso silencioso de objetos.
- O boot de um kernel de laboratório imprime
  `[lab] update trust anchor overridden; kernel not for production`.

O que o gate **não** prova: que a chave de produção específica está pinada e que
um asset assinado por ela está publicado. Isso permanece um passo de operador
(§4) porque é custódia de chave, não engenharia.

## 2. Sequência provada (quatro power cycles)

| Boot | Estado do loader | Ações | Evidência |
|---|---|---|---|
| 1 | slot 0 confirmado | `update-fetch`, `update-download-payload`, `update-prepare-explain`, `update-prepare`, `update-apply` | `Boot provider: ready=yes reason=ready`; `[ok] staged update verified and boot slot armed`; slot inativo em `state=valid` |
| 2 | slot 1, tentativa consumida | `update-rollback-check`, `update-confirm-health`, `update-download-payload`, `update-prepare`, `update-apply` | `[boot] A/B attempt slot=1 state=pending`; `persistent boot health confirmed`; `health=confirmed [ACTIVE]` |
| 3 | slot 0, tentativa consumida | `update-rollback-check` e **nada mais** | `[boot] A/B attempt slot=0 state=pending`; `boot attempt pending confirmation; rollback still armed` |
| 4 | slot 1 restaurado pelo loader | `update-rollback-check` | `[boot] A/B attempt slot=1 state=rollback`; `[ok] boot rollback completed`; `boot rolled back to the confirmed slot; staged update disarmed`; slot 0 em `state=failed` |

### Restrições de ordem que não são estilo

- `update-confirm-health` precisa ser a **primeira** mutação de boot slot do seu
  boot: a confirmação casa a geração que o loader gravou no token de tentativa;
  qualquer escrita durável anterior invalida o token (`-60`,
  `reason=token-mismatch`).
- `update-confirm-health` limpa `staged.ini`, `payload.bin` e `state.ini`. Um
  segundo `update-apply` no mesmo boot exige repopular o stage (`-50` sem digest,
  `-30` com digest). O catálogo `latest.ini` sobrevive, então repetir
  `update-download-payload` + `update-prepare` é suficiente.
- O manifesto precisa declarar um **número de prerelease** estritamente maior. O
  comparador do runtime ignora build metadata (para em `+`), então
  `alpha.319+20260803` contra `alpha.319+20260728` compara igual e o download
  recusa com `-40`.

## 3. Como rodar

Os gates são autocontidos: geram a chave, recompilam o kernel de laboratório,
servem o material assinado e conduzem os quatro boots.

```sh
# Preflight local (WSL): instalação + dois ciclos sob QEMU/OVMF.
make smoke-x64-qemu-update-ab

# Aceite oficial (host Windows com VMware Workstation).
make smoke-x64-vmware-update-ab
```

Variáveis úteis:

- `SMOKE_X64_UPDATE_AB_ARGS` / `SMOKE_X64_VMWARE_UPDATE_AB_ARGS` — repasse de
  argumentos (`--verbose`, `--keep-disk`, `--keep-vm`, `--step-timeout`).
- `UPDATE_AB_PUBLISHED_AT` — `published_at` do manifesto (default: hoje UTC).

O endereço que o guest disca é resolvido por
`tools/scripts/update_ab_lab_config.py`: `10.0.2.2` no QEMU (gateway SLIRP) e o
endereço do host na rede NAT do VMware (`VMnet8`) no gate oficial. Use
`--host`/`--vmnet` quando o laboratório usar outra vmnet ou rede bridged.

O gate emite um manifesto de evidência em `build/ci/` e, quando promovido a
release, uma cópia sanitizada vai para
[`../releases/evidence/`](../releases/evidence/). O manifesto declara provider,
âncora, versão/URL/hash do payload, os dois slots usados, o número de boots
observados e os doze invariantes booleanos; `validate_evidence` recusa qualquer
`no`, qualquer provider desconhecido e qualquer texto que contenha uma recovery
key.

## 4. Passo de operador com a chave de produção

O gate acima prova o mecanismo. Para provar a cadeia de publicação real, com a
âncora pinada em `src/services/update_agent_parse.c`:

1. Publique `capyos64.bin` como asset da tag e assine o `latest.ini` offline:
   ```sh
   python3 tools/scripts/build_update_manifest.py \
     --version <proxima-alpha> --channel stable --branch main \
     --source github:henriquefarisco/CapyOS --published-at <YYYY-MM-DD> \
     --payload build/capyos64.bin \
     --payload-url https://github.com/henriquefarisco/CapyOS/releases/download/<tag>/capyos64.bin \
     --private-key <chave-offline> --output build/update/latest.ini --force
   python3 tools/scripts/verify_update_manifest.py \
     --manifest build/update/latest.ini --payload build/capyos64.bin
   ```
2. Publique `latest.ini` no mesmo release.
3. Numa instalação oficial (sem flags de laboratório): `update-fetch` →
   `update-download-payload` → `update-prepare` → `update-apply`, reinicie,
   `update-confirm-health`.
4. Repita sem confirmar e observe `update-rollback-check` reportando o rollback.

Nunca use `--allow-lab-http-payload-url` nem `--allow-insecure-key` com a chave
de produção.

## 5. Diagnóstico de `-60`

`-60` significa exatamente uma coisa: transição persistente não comprovável.
Desde o `alpha.319` o motivo é legível.

```
> print-boot-slot
Slot A: version=0.8.0-alpha.319 state=active boots=1 ok=1 fail=0 health=confirmed [ACTIVE]
Slot B: version= state=empty boots=0 ok=0 fail=0 health=pending
Boot provider: ready=yes reason=ready
```

| `reason` | Causa |
|---|---|
| `no-persistent-mount` | volume DATA não montado |
| `no-raw-device` | backend não-nativo (EFI BlockIO), bloco != 512 ou sem read/write |
| `no-data-binding` | faixa DATA divergente do handoff, ou identidade GPT não estrita (discos anteriores ao `alpha.317`) |
| `no-esp-binding` | faixas ESP/BOOT divergentes do handoff |
| `no-flush` | dispositivo sem flush durável, ou falha ao abrir o store |
| `no-control` | store aberto mas o control A/B não pôde ser vinculado |
| `control-unknown` | snapshot indisponível ou resultado de commit indeterminado |
| `token-mismatch` | lease/geração divergente da autorizada |

Quando a capability não registra, o boot também imprime
`[boot] provider A/B persistente indisponivel` seguido de `[boot] provider reason=<label>`.
