# Playbook — gate do ciclo A/B assinado (Etapa 8)

> **Baseline atual: `0.9.2+20260826` (stable candidate).** A `0.9.1` publicou os
> materiais Ed25519 e estabeleceu a ancora de producao corrente, mas e o
> bootstrap dessa ancora: a `0.9.0` usa o pin anterior e a `0.9.1` nao pode
> atualizar para si mesma. A Etapa 8 permanece aberta ate a candidata `0.9.2`
> executar apply, reboot, rollback, reaplicacao e confirmacao de saude no VMware
> oficial a partir da ISO `0.9.1` publicada.

Este playbook cobre o gate que fecha o critério de update da Etapa 8: um
manifesto Ed25519 é buscado por HTTP, o payload é verificado, gravado no slot
inativo e armado para **uma única** tentativa; o loader UEFI consome essa
tentativa; o boot resultante confirma saúde; e um segundo ciclo deixado sem
confirmação é revertido pelo loader e reportado pelo updater.

- Gate VMware do mecanismo: `make smoke-x64-vmware-update-ab` (chave lab).
- Aceite pós-promoção: `make
  smoke-x64-vmware-update-ab-production-existing-iso` (assets públicos, sem
  chave privada nem flags lab).
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

## 2. Sequência provada no laboratório (quatro power cycles)

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

Essa ordem é válida apenas no gate de laboratório: o payload descartável ainda
reporta a versão-base compilada, embora o manifesto efêmero declare a próxima
prerelease. Num release real, o payload passa a reportar a própria versão do
`latest.ini`; depois de confirmado, reaplicar o mesmo catálogo é corretamente
recusado pelo anti-downgrade. O gate de produção usa a ordem inversa descrita na
§4: primeiro rollback, depois nova aplicação a partir do predecessor restaurado
e confirmação de saúde.

## 3. Como rodar

Os gates são autocontidos: geram a chave, recompilam o kernel de laboratório,
servem o material assinado e conduzem os quatro boots.

```sh
# Preflight local (WSL): instalação + dois ciclos sob QEMU/OVMF.
make smoke-x64-qemu-update-ab

# Gate VMware do mecanismo, ainda com chave descartável de laboratório.
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
   obrigatório e o perfil permanente solo fail-closed: exatamente zero
   aprovação, squash-only, threads resolvidas e os seis checks strict
   autenticados (`Lint`, `Release gates`,
   `QEMU ISO smoke`, dois `Analyze` e `CodeQL`). A workflow valida esses controles com
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
5. Baixe anonimamente do release público o `latest.ini` e o `capyos64.bin` e
   selecione como `EXISTING_ISO` a ISO **publicada** do predecessor imediato. O
   predecessor precisa reportar versão estritamente menor e já conter a mesma
   âncora Ed25519 de produção. Execute, sem chave privada nem flags de
   laboratório:

   ```sh
   make smoke-x64-vmware-update-ab-production-existing-iso \
     EXISTING_ISO=/caminho/CapyOS-Installer-UEFI-predecessor.iso \
     PRODUCTION_PREDECESSOR_VERSION=0.9.1+20260825 \
     PRODUCTION_PREDECESSOR_ISO_SHA256=<sha256 publicado da ISO> \
     PRODUCTION_MANIFEST=/caminho/latest.ini \
     PRODUCTION_PAYLOAD=/caminho/capyos64.bin
   ```

6. O driver fixa a ISO ao SHA-256 publicado e verifica assinatura, versão, URL
   imutável, tamanho e SHA-256 do update antes de criar a VM descartável. No
   guest ele exige a rota pública
   `releases/latest/download/latest.ini`, ausência do banner de laboratório e
   consulta `print-version` após cada login para vincular o runtime ao release
   estável esperado (a identidade estendida exata permanece presa aos hashes da
   ISO predecessora e do payload assinado). Ele então
   executa dois ciclos na única ordem compatível com o anti-downgrade:
   aplica e **não confirma** o primeiro ciclo, observa o rollback ao predecessor,
   reaplica o mesmo release e então confirma saúde. A evidência declara
   `trust_anchor=production-ed25519`, `cycle_order=rollback-then-confirm`, hash da
   ISO predecessora e todos os invariantes de loader/updater.

### Invariante de bootstrap da âncora

O primeiro release após uma rotação da âncora não pode satisfazer este gate se
nenhum release oficial anterior já trouxer a nova chave: o predecessor não
aceita a assinatura nova, enquanto o release recém-publicado não pode atualizar
para si mesmo. Nesse caso, preserve o release como bootstrap, registre o gate
como pendente e execute o aceite com o release seguinte. Não crie uma ISO
retroativa, não use override de versão e não converta o gate de laboratório em
evidência de produção.

Para `0.9.1+20260825`, `0.9.0+20260821` contém a âncora anterior; portanto a
`0.9.1` é o bootstrap da âncora corrente. O primeiro ciclo de produção
executável é `0.9.1` → release estável seguinte.

Os passos 5–6 são aceite pós-promoção obrigatório: uma workflow criptográfica
verde não fecha sozinha a Etapa 8.

Nunca use `--allow-lab-http-payload-url`, `--allow-insecure-key`,
`CAPYOS_UPDATE_LAB_TRUST_KEY_HEX` ou uma chave privada no gate de produção.

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
