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
remote_manifest=https://raw.githubusercontent.com/henriquefarisco/CapyOS/refs/tags/v0.8.0/system/update/latest.ini
```

Se `remote_manifest=` não existir, o agente deriva a URL a partir de
`source=github:<owner>/<repo>` e da política de trilha: `develop` usa
`refs/heads/<branch>`; `stable` usa `refs/tags/v<major>.<minor>.<patch>`
derivado da versão corrente.

## Fluxo seguro

1. `update-fetch` chama `update_agent_fetch_remote_manifest()`.
2. O runtime baixa o manifesto configurado.
3. O conteúdo é salvo temporariamente em `/system/update/fetched.ini`.
4. O agente chama `update_agent_import_manifest_path()` sobre esse temporário.
5. A importação valida:
   - trilha `channel`/`branch`/`source`;
   - versão mais nova;
   - `payload_sha256` hex64;
   - `payload_url` HTTPS ou caminho local sob `/system/update/`, sem espaços ou `..`;
   - `signature_ed25519` hex128;
   - texto canônico assinado sem a própria linha de assinatura.
6. O temporário é removido.
7. Apenas manifesto aceito substitui `/system/update/latest.ini`.

## Comandos

```bash
update-channel show
update-prepare
update-apply
update-confirm-health
update-rollback-check
```

`update-prepare` é o fluxo preferencial: ele busca o manifesto remoto, valida a
trilha/assinatura, baixa o `payload_url`, calcula SHA-256 real no kernel,
persiste `/system/update/payload.bin`, promove o manifesto para staging e arma
a ativacao pendente sem aplicar o boot slot. O fluxo manual equivalente continua
disponível via `update-fetch`, `update-download-payload`, `update-stage` e
`update-arm on`; nesse caminho manual, `update-prepare-explain` mostra qual
gate local ainda bloqueia o preparo e `update-prepare-dry-run` valida o catálogo
local e o cache verificado antes de persistir staging ou armar ativacao.
`update-apply` aplica o staged update armado usando
`payload_cache_sha256` por padrão; `update-apply <payload_sha256>` permanece
disponível como fallback manual explícito. Após o boot no slot atualizado,
`update-confirm-health` conclui o update
quando o sistema estiver saudável. Se o boot permanecer sem confirmação,
`update-rollback-check` executa o rollback assistido e limpa o staging.

## Erros importantes

- `remote manifest URL unavailable` — repositório sem URL remota derivável.
- `remote manifest writer unavailable` — runtime não consegue persistir o
  temporário.
- `remote manifest fetch failed` — falha de transporte, HTTP não-200, corpo vazio
  ou manifesto maior que o limite do agente.
- `payload download buffer unavailable` — heap do kernel não conseguiu
  reservar o buffer temporário de download.
- `payload download failed` — transporte/local read falhou, corpo vazio ou payload
  excedeu `UPDATE_AGENT_PAYLOAD_MAX_BYTES` (16 MiB).
- `payload sha256 mismatch; cache refused` — payload baixado não bate com
  `payload_sha256` do manifesto assinado.
- `payload cache missing or unverified for staging` — `update-stage` foi chamado
  antes de um cache de payload verificado existir para o manifesto atual.
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
- `boot health confirm failed` — o boot slot atual não aceitou confirmação de
  saúde.
- `boot rollback failed` — havia rollback pendente, mas a troca para o slot
  anterior falhou.

## Auditoria

O shell registra `event=fetch`, `event=apply`, `event=confirm-health`,
`event=rollback-check` e `event=rollback` em `/var/log/update-history.log`
quando os comandos são aceitos. O histórico inclui `payload=` quando o catálogo ou o staged update possuem
`payload_url`, e `payload_sha=` quando há cache validado ou staged digest. Falhas também geram logs `[audit] [update]` no
`klog`.

## Limitações atuais

- HTTPS depende do estado de F4/TLS no runtime; até lá, `payload_url` HTTPS pode
  falhar no transporte real e deve continuar coberto por fetcher injetável em
  testes ou por caminho local `/system/update/...`.
- Staging/apply totalmente automático a partir de `/system/update/payload.bin`
  fica para incremento posterior.
