# Playbook — gate do ciclo A/B assinado (Etapa 8)

> **Baseline atual: `0.9.0+20260821` (stable).** O lifecycle A/B e os gates de
> laboratório existem, mas a Etapa 8 permanece aberta até uma release com os
> materiais Ed25519 de produção executar apply, reboot, confirmação de saúde e
> rollback no VMware oficial. A release 0.9.0 publicada contém os sete
> assets-base verificados por SHA-256, mas não contém `.sig`, manifestos públicos
> ou `latest.ini`; portanto ela não é evidência desse ciclo de produção.

Este playbook cobre o gate que fecha o critério de update da Etapa 8: um
manifesto Ed25519 é buscado por HTTP, o payload é verificado, gravado no slot
inativo e armado para **uma única** tentativa; o loader UEFI consome essa
tentativa; o boot resultante confirma saúde; e um segundo ciclo deixado sem
confirmação é revertido pelo loader e reportado pelo updater.

- Aceite oficial: `make smoke-x64-vmware-update-ab` (VMware + UEFI + E1000).
- Feedback de desenvolvimento: `make smoke-x64-qemu-update-ab`.
- Contrato host (roda em `release-check`): `make update-ab-selftest`.

## 1. Por que existe uma âncora de confiança de laboratório

A chave privada dedicada ao `latest.ini`/update-agent é offline-only e nunca
entra em CI ou em runner automatizado (ver
[`../security/release-signing.md`](../security/release-signing.md)). Ela é
distinta da chave offline que assina `release-artifacts.sha256`. Sem uma âncora
de laboratório, nenhum gate automatizado conseguiria produzir um manifesto que
o kernel aceite, e o critério ficaria dependente de uma execução manual com
material sensível.

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

1. Deixe **Release Artifacts** criar o draft com os sete assets-base e siga o
   handoff de `release-artifacts.sha256` descrito em
   [`release-process.md`](release-process.md).
2. Assine o `latest.ini` offline com a chave dedicada do update-agent usando os
   comandos autoritativos de [`release-process.md`](release-process.md). Não
   escolha a data manualmente: derive `PUBLISHED_AT` de `+YYYYMMDD` da versão ou
   da data do commit da tag. A verificação deve exigir versão, canal `stable`,
   branch `main`, source, data, URL imutável da tag e os bytes exatos de
   `capyos64.bin`.
3. Confirme que o commit da tag contém
   `.github/release-policy/release-checksum-ed25519.sha256` como arquivo regular
   com uma única linha `hex64` minúscula + LF e que esse valor é o fingerprint
   real aprovado da chave que assina `release-artifacts.sha256`, não o pin do
   update-agent. Sem esse pin versionado a promoção permanece fail-closed.
4. Anexe os cinco materiais offline ao draft e execute **Promote Signed
   Release**. O repositório deve ter releases imutáveis habilitadas e o secret
   `CAPYOS_RELEASE_POLICY_AUDIT_TOKEN` deve conter PAT/App fine-grained de curta
   duração, limitado ao repositório e com **Administration: write**. A permissão
   elevada é necessária para a API expor `bypass_actors`, embora o workflow use
   o token somente em `GET` de `immutable-releases` e dos dois rulesets; resposta
   sem esse campo falha fechado. Revogue ou rotacione o token após a janela.
   `CAPYOS_RELEASE_TAG_RULESET_ID` deve apontar para o ruleset sem bypass que
   bloqueia update/delete exatamente em `refs/tags/v*`.
   `CAPYOS_RELEASE_MAIN_RULESET_ID` deve apontar para o ruleset sem bypass de
   `refs/heads/main`, com deletion e non-fast-forward bloqueados, pull request
   obrigatório, pelo menos uma aprovação, descarte de review obsoleto após push
   e aprovação do último push. A workflow valida esses controles com
   `verify_release_repository_policy.py`, verifica os 12 assets no draft
   autenticado, publica e marca Latest numa única mutação, exige o estado
   imutável e então repete os gates pelas URLs públicas da tag. Pela rota
   `releases/latest/download/`, revalida `latest.ini` e `capyos64.bin`.
   Esse ruleset prova proteção corrente; não prova historicamente que o commit da
   tag passou por PR/review. Entre o último upload e a conclusão terminal do
   workflow, mantenha uma janela exclusiva: não altere assets, a tag, `main`, a
   configuração de immutable releases nem qualquer dos dois rulesets. O promoter
   reconsulta as três políticas imediatamente antes do único `PATCH`; o lock
   global serializa workflows, mas não impede mutação manual externa.
5. Após a promoção, numa instalação oficial (sem flags de laboratório):
   `update-fetch` →
   `update-download-payload` → `update-prepare` → `update-apply`, reinicie,
   `update-confirm-health`.
6. Repita sem confirmar e observe `update-rollback-check` reportando o rollback.

Os passos 5–6 são aceite pós-promoção obrigatório: uma workflow criptográfica
verde não fecha sozinha a Etapa 8.

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
