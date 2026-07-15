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
update-prepare
update-apply
update-confirm-health
update-rollback-check
```

O fluxo seguro atualmente termina no download autenticado:
`update-fetch` importa o `latest.ini` assinado, `update-download-payload` limita
o buffer temporário próprio pelo `payload_size` assinado, confere tamanho e
SHA-256, grava o cache, relê os bytes persistidos e repete a validação antes de
salvar o estado.
`update-prepare-dry-run` reabre e recalcula o cache em vez de confiar apenas no
hash textual de `state.ini`. `update-prepare-explain` expõe o gate `persistence`
depois que todos os gates criptográficos e de readback passam.

`update-prepare`, `update-stage`, `update-arm on` e `update-apply` retornam
`UPDATE_AGENT_ERR_UNSUPPORTED` (`-60`). Isso é intencional: o payload ainda não
é gravado em um slot de boot persistente e o antigo `boot_slot` era apenas
metadado em RAM. Reportar sucesso nesse estado criaria uma falsa atualização.
`update-arm off` e `update-clear` continuam disponíveis para limpar estado
legado. A instalação da alpha atual deve ser feita pelo instalador oficial.

## Publicação do catálogo estável

O workflow publica `capyos64.bin`, `manifest.bin`, checksums e
`latest.unsigned.ini` como material de handoff. A chave privada permanece
offline e nunca entra em GitHub Actions. Depois de baixar exatamente o payload
da release, o operador gera e verifica o asset final:

```bash
python3 tools/scripts/build_update_manifest.py \
  --version 0.8.0-alpha.313+20260712 \
  --channel stable --branch main \
  --source github:henriquefarisco/CapyOS \
  --published-at 2026-07-12 \
  --payload build/publish/capyos64.bin \
  --payload-url https://github.com/henriquefarisco/CapyOS/releases/download/v0.8.0-alpha.313+20260712/capyos64.bin \
  --private-key /caminho/offline/update-ed25519.pem \
  --output build/publish/latest.ini

python3 tools/scripts/verify_update_manifest.py \
  --manifest build/publish/latest.ini \
  --payload build/publish/capyos64.bin \
  --expected-version 0.8.0-alpha.313+20260712 \
  --expected-channel stable --expected-branch main \
  --expected-source github:henriquefarisco/CapyOS
```

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
- `persistent update apply unsupported; verified download only` — staging,
  arm ou apply foi recusado porque ainda não existe escrita persistente e
  rollback real de boot slot.
- `prepare explain: catalog missing` — `update-prepare-explain` não encontrou
  catálogo local para diagnosticar.
- `prepare explain: verified payload cache missing` — o gate `cache` de
  `update-prepare-explain` falhou antes de staging/arm.
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
- `persistent health confirmation unsupported; no update committed` — nenhum
  boot-control A/B persistente existe; confirmar metadados apenas em RAM é
  recusado com `-60`.
- `boot rollback failed` — havia rollback pendente, mas a troca para o slot
  anterior falhou.

## Auditoria

O shell registra `event=fetch`, `event=apply`, `event=confirm-health`,
`event=rollback-check` e `event=rollback` em `/var/log/update-history.log`
quando os comandos são aceitos. O histórico inclui `payload=` quando o catálogo ou o staged update possuem
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
- Staging/apply e `update-confirm-health` permanecem fail-closed até existir
  bundle versionado, slot inativo persistente, readback do slot, troca atômica,
  confirmação pós-reboot e rollback comprovado.
- Manifestos legados sem `payload_size` continuam aceitos para leitura, mas usam
  o teto de 8 MiB; novos manifestos gerados pelas ferramentas oficiais sempre
  assinam o tamanho exato.
