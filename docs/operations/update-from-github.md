# CapyOS — Update a partir de manifesto remoto

## Objetivo

`update-fetch` é o primeiro passo operacional de F5. Ele busca o manifesto remoto
configurado para a trilha atual e só atualiza o catálogo local se a validação de
segurança do `update-agent` aceitar o arquivo.

## Configuração

A trilha de update vive em `/system/update/repository.ini`:

```ini
channel=stable
branch=main
source=github:henriquefarisco/CapyOS
remote_manifest=https://github.com/henriquefarisco/CapyOS/releases/latest/download/latest.ini
```

Se `remote_manifest=` não existir, o agente deriva a URL a partir de
`source=github:<owner>/<repo>` e da política de trilha: `develop` usa
`refs/heads/<branch>`; `stable` usa o asset assinado
`releases/latest/download/latest.ini`. O agente migra automaticamente somente
o formato oficial legado do CapyOS; uma `remote_manifest=` customizada nunca é
reescrita.

O canal stable só fica operacional quando a release marcada como Latest contém
um `latest.ini` assinado pela chave pinada e validado contra o `capyos64.bin` da
mesma tag. A release 0.9.0 publicada em 2026-08-21 não contém esse asset e não é
uma fonte de update stable; a ausência deve falhar fechado.

## Fluxo seguro

1. `update-fetch` chama `update_agent_fetch_remote_manifest()`.
2. O runtime baixa o manifesto configurado.
3. O conteúdo é salvo temporariamente em `/system/update/fetched.ini`.
4. O agente chama `update_agent_import_manifest_path()` sobre esse temporário.
5. A importação valida:
   - trilha `channel`/`branch`/`source`;
   - versão mais nova;
   - `payload_url` HTTPS ou caminho local sob `/system/update/`, sem espaços ou `..`;
   - `payload_size` decimal positivo de até 8 MiB quando presente;
   - `payload_sha256` hex64;
   - `signature_ed25519` hex128;
   - texto canônico assinado sem a própria linha de assinatura.
6. O temporário é removido.
7. Apenas manifesto aceito substitui `/system/update/latest.ini`.

Novos manifestos canônicos usam a ordem `payload_url`, `payload_size`,
`payload_sha256`; a forma legada sem `payload_size` permanece verificável. Em
ambas, todos os campos anteriores a `signature_ed25519` pertencem aos bytes
assinados.

## Comandos

```bash
update-channel show
update-fetch
update-download-payload
update-prepare
update-apply
reboot
update-confirm-health
update-rollback-check
```

`update-fetch` importa o `latest.ini` assinado. `update-download-payload` limita
o buffer temporário próprio pelo `payload_size` assinado, confere tamanho e
SHA-256, grava o cache, relê os bytes persistidos e repete a validação antes de
salvar o estado. `update-prepare-dry-run` reabre e recalcula o cache em vez de
confiar apenas no hash textual de `state.ini`; `update-prepare-explain` reporta
`-` quando todos os gates passam.

`update-prepare` executa fetch + download + `update-stage` + `update-arm on`.
`update-stage` promove o catálogo verificado para `staged.ini` (`-49` quando o
cache não pode ser re-hasheado, `-5` sem catálogo mais novo) e `update-arm on`
arma a ativação (`-10` sem staged update). Nenhum dos dois toca boot slot.

`update-apply` é o único comando que grava disco. Ele reabre o payload em cache,
recalcula o SHA-256 contra o manifesto assinado e, com os mesmos bytes
verificados, grava o slot **inativo** (payload primeiro, flush, readback
SHA-256 + padding, header por último) e então arma exatamente uma tentativa de
boot vinculada à geração publicada pelo staging. Um slot gravado e não armado é
recusa, nunca sucesso.

No boot seguinte o UEFI seleciona o slot pendente, consome a tentativa com commit
durável, revalida header/payload/SHA-256/padding e publica o token de tentativa
no handoff v10. Já no sistema novo, `update-confirm-health` confirma a geração
exata desse token e só então limpa o staged catalog. Se o sistema novo não
confirmar e reiniciar de novo, o loader restaura o slot confirmado antes de
entregar o controle; `update-rollback-check` reporta esse rollback e desarma o
staged update. Sem reboot pendente ele reporta `pending` ou `nenhum rollback`.

`-60` (`UPDATE_AGENT_ERR_UNSUPPORTED`) deixou de ser recusa incondicional. Ele
significa exatamente que a transição persistente não pôde ser provada: nenhum
provider raw 512 registrado com read/write e flush durável, binding
ESP/BOOT/DATA não estrito, geração/lease divergente, payload não verificado ou
resultado de commit indeterminado. `update-apply` sem digest permanece recusado
por definição — o payload só alcança o slot inativo pelo caminho verificado.
Discos anteriores ao `alpha.317` com GUIDs duplicados entram por modo legado
validado apenas para mount e nunca recebem capability de update.

`update-arm off` e `update-clear` continuam disponíveis para limpar estado.
A instalação inicial da stable atual deve ser feita pelo instalador oficial.

## Publicação do catálogo estável

O workflow **Release Artifacts** deixa no GitHub Release draft exatamente os
sete assets-base, incluindo `capyos64.bin`, `manifest.bin` e checksums. Um
artifact interno do Actions, retido por 14 dias, contém esses bytes e
`latest.unsigned.ini` como handoff; o arquivo unsigned não é asset do draft e
não é implantável. A chave privada permanece offline e nunca entra em GitHub
Actions. Ainda com a release em draft, o operador assina o payload exato, gera
`latest.ini`, valida todos os campos esperados e o anexa junto dos outros quatro
materiais offline. `PUBLISHED_AT` é derivado de `+YYYYMMDD` da versão ou, sem
essa metadata, da data do commit da tag. O procedimento autoritativo, incluindo
os comandos de assinatura, verificação dos 12 assets e promoção atômica, está em
[`release-process.md`](release-process.md). Não gere `latest.ini` depois que a
release já estiver publicada e imutável.

O fingerprint do signer de checksums vem exclusivamente de
`.github/release-policy/release-checksum-ed25519.sha256` no commit da tag; ele é
distinto do pin do update-agent e não pode ser substituído por variável mutável
do repositório. A promoção inicial também exige releases imutáveis, os rulesets
sem bypass de `refs/tags/v*` e `refs/heads/main` e o secret
`CAPYOS_RELEASE_POLICY_AUDIT_TOKEN`. Esse PAT/App fine-grained deve ser
temporário, limitado ao repositório e ter `Administration: write`: a API precisa
dessa permissão para expor `bypass_actors`, embora o workflow faça somente `GET`.
Resposta parcial mantém o gate fail-closed; revogue ou rotacione o token depois.
O ruleset de `main` comprova proteção corrente, não review histórico do commit.
Do último upload até a conclusão terminal do promoter, não altere assets, a tag,
`main`, immutable releases nem qualquer dos dois rulesets. As três políticas são
reconsultadas imediatamente antes do único `PATCH`; o lock global cobre apenas
workflows do Actions.

O builder recusa uma chave privada cuja chave pública não seja exatamente a
chave raw Ed25519 pinada pelo runtime. O verificador reproduz a remoção da linha
`signature_ed25519=` byte a byte e também exige a forma canônica LF, ordem fixa,
assinatura na última linha e ausência de campos duplicados.

## Erros importantes

- `remote manifest URL unavailable` — repositório sem URL remota derivável.
- `remote manifest writer unavailable` — runtime não consegue persistir o
  temporário.
- `remote manifest fetch failed` — falha de transporte, HTTP não-200, corpo vazio
  ou manifesto maior que o limite do agente.
- `payload download buffer unavailable` — heap do kernel não conseguiu
  reservar o buffer temporário de download.
- `payload download failed` — transporte/local read falhou, corpo vazio ou payload
  excedeu o limite efetivo do HTTP/runtime de 8 MiB.
- `payload size mismatch; cache refused` — os bytes recebidos não têm o
  `payload_size` assinado pelo manifesto.
- `persisted payload cache verification failed` — a releitura do cache falhou ou
  tamanho/SHA-256 mudaram após a gravação; o cache é invalidado e o estado de
  staging é preservado sem o digest quando possível.
- `payload sha256 mismatch; cache refused` — payload baixado não bate com
  `payload_sha256` do manifesto assinado.
- `persistent slot staging refused; inactive slot not written` — `update-apply`
  não conseguiu provar a gravação do slot inativo: sem provider registrado com
  flush durável, geração/lease divergente ou plano de slot inválido.
- `inactive slot staged but boot attempt not armed` — o payload chegou ao slot
  inativo, mas a tentativa de boot não foi publicada; o loader continua bootando
  o slot confirmado e o comando é recusado.
- `inactive slot written and armed for one boot attempt` — apply concluído; o
  próximo boot usa o slot novo e tem exatamente uma tentativa.
- `persistent update apply unsupported; verified download only` — apply sem
  digest (`update_agent_apply_boot_slot`), recusado por definição.
- `prepare explain: catalog missing` — `update-prepare-explain` não encontrou
  catálogo local para diagnosticar.
- `prepare explain: verified payload cache missing` — o gate `cache` de
  `update-prepare-explain` falhou antes de staging/arm.
- `prepare explain: all prepare gates passed` — todos os gates passaram e o
  staging é seguro.
- `payload cache missing or unverified for staging` — `update-stage` recusou
  porque o cache não pôde ser re-hasheado contra o manifesto assinado.
- `no cached update available to stage` — `update-stage` sem catálogo mais novo.
- `no staged update available to arm` — `update-arm on` sem `staged.ini`.
- `no cached update available for prepare dry-run` — `update-prepare-dry-run`
  não encontrou catálogo local mais novo para revisar.
- `prepare dry-run catalog invalid` — o catálogo local revisado pelo dry-run não
  preserva `payload_sha256`, `payload_url`, assinatura Ed25519 ou origem
  coerente com o status atual.
- `prepare dry-run requires verified payload cache` — `update-prepare-dry-run`
  foi chamado antes de existir `payload_cache_sha256` verificado para o catálogo
  local.
- `payload cache sha256 missing; refusing cached apply` — `update-apply` foi
  chamado sem argumento manual e sem cache verificado disponível.
- `failed to persist payload cache` — payload validado não pôde ser gravado em
  `/system/update/payload.bin`.
- `failed to persist payload cache state` — cache foi gravado, mas o digest
  verificado não pôde ser persistido em `/system/update/state.ini`.
- `imported manifest payload size invalid` / `catalog cache payload size invalid`
  — `payload_size` presente não é decimal canônico entre 1 e 8 MiB.
- `imported manifest missing or malformed payload url` — manifesto importado
  não declara origem HTTPS/local aceitável para o payload.
- `catalog cache missing or malformed payload url` — catálogo local mais novo
  declara origem de payload ausente ou inválida.
- `staged update missing or malformed payload url` — staging persistente não
  preserva uma origem de payload aceitável.
- `imported manifest missing or invalid ed25519 signature` — manifesto baixado
  não passou pelo gate de assinatura.
- `payload sha256 declared but verifier supplied no digest` — `update-apply` foi
  chamado sem digest real.
- `payload sha256 supplied is not a 64-char hex digest` — digest real malformado.
- `payload sha256 mismatch; refusing to apply update` — payload local não bate
  com o manifesto assinado.
- `persistent health confirmation unavailable or token mismatch` — não há
  provider persistente registrado ou o token de tentativa do handoff não
  corresponde à geração comprometida; recusado com `-60`.
- `persistent boot health confirmed` — a tentativa foi confirmada de forma
  durável e o staged catalog foi limpo.
- `persistent rollback unsupported; no boot control committed` — o check de
  rollback é recusado com `-60` quando não há provider persistente registrado.
- `boot rolled back to the confirmed slot; staged update disarmed` — o boot atual
  é o rollback aplicado pelo loader depois de uma tentativa não confirmada.
- `boot attempt pending confirmation; rollback still armed` — a tentativa foi
  consumida e ainda espera `update-confirm-health`.
- `no boot rollback pending` — nada pendente no metadata durável.

## Auditoria

O shell registra eventos aceitos em `/var/log/update-history.log`. Comandos
recusados com `-60` nunca geram um falso evento de sucesso. O histórico inclui `payload=` quando o catálogo ou o staged update possuem
`payload_url`, e `payload_sha=` quando há cache validado ou staged digest. Falhas também geram logs `[audit] [update]` no
`klog`.

## Limitações atuais

- HTTPS usa o TLS BearSSL real do kernel; ainda falta um smoke externo dedicado
  do updater cobrindo DNS/TCP/TLS, redirects, manifesto e payload ponta a ponta.
- `payload_size` limita o buffer do agente, mas `http_get` ainda materializa a
  resposta internamente até o teto de 8 MiB; abort/streaming no tamanho assinado
  permanece pendente.
- O cache ainda não usa temp + rename + flush transacional nem lock entre
  comandos concorrentes; o readback detecta divergência imediata, mas não
  substitui tolerância a queda de energia.
- O lifecycle A/B (stage autorizado por geração, arm de tentativa única,
  confirmação e rollback) está implementado e provado em host sobre um harness
  provider-backed com GPT espelhado real. O que falta é a evidência externa: um
  payload assinado publicado, aplicado e reiniciado em VMware UEFI/E1000, com
  confirmação e rollback observados. Até esse gate rodar, o critério de update da
  Etapa 8 permanece aberto.
- Discos anteriores ao `alpha.317` com GUIDs duplicados precisam de migração de
  identidade GPT antes de receberem capability de update.
- Manifestos legados sem `payload_size` continuam aceitos para leitura, mas usam
  o teto de 8 MiB; novos manifestos gerados pelas ferramentas oficiais sempre
  assinam o tamanho exato.
