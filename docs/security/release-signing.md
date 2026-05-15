# CapyOS — Assinatura Ed25519 de release

## Objetivo

A trilha oficial de release deve publicar checksums verificaveis e uma assinatura
Ed25519 separada. O fluxo protege o operador contra artefatos trocados depois da
geração de `build/release-artifacts.sha256` e prepara a integracao futura com o
fetch remoto do `update-agent`.

Este procedimento cobre a assinatura operacional de artefatos de release. O gate
local de manifestos do `update-agent` continua documentado nas release notes de
`0.8.0-alpha.12` e valida o campo `signature_ed25519=` antes de expor update,
stage ou import.

## Arquivos gerados

- `build/release-artifacts.sha256`
  - gerado por `release-checksums`.
  - lista os hashes SHA-256 do kernel, loader UEFI, manifest de boot, boot config
    e ISO UEFI mais recente.
- `build/release-artifacts.sha256.sig`
  - assinatura Ed25519 raw do arquivo de checksums.
- chave publica Ed25519 PEM
  - pode ser exportada pelo mesmo passo de assinatura.
  - pode ser publicada junto da release ou fixada na infraestrutura de CI.
- `build/release-public-key.manifest`
  - manifesto publico com algoritmo, nome da chave publica e fingerprint SHA-256
    esperado.
  - nao contem chave privada.
- `build/release-publication.manifest`
  - manifesto publico deterministico com hashes dos materiais de publicacao.
  - nao contem chave privada nem timestamp.

## Chave offline

A chave privada de release deve ficar fora do repositorio e fora dos artefatos de
build. Permissões recomendadas no host do operador:

```bash
openssl genpkey -algorithm Ed25519 -out ~/.capyos/release-ed25519.pem
chmod 600 ~/.capyos/release-ed25519.pem
openssl pkey -in ~/.capyos/release-ed25519.pem -pubout -out ~/.capyos/release-ed25519.pub.pem
```

A chave publica pode ser versionada ou publicada; a chave privada nao deve ser
copiada para o workspace.

## Fingerprint da chave pública

Depois de exportar a chave pública, o operador pode emitir o fingerprint SHA-256
esperado para a CI sem tocar na chave privada:

```bash
make release-public-key-fingerprint \
  RELEASE_PUBLIC_KEY=$HOME/.capyos/release-ed25519.pub.pem
```

Por padrão, o alvo imprime uma linha pronta para o ambiente da CI:

```bash
RELEASE_PUBLIC_KEY_SHA256=<hex64>
```

Também é possível chamar o helper diretamente e escolher formato `hex`, `colon`
ou `env`:

```bash
python3 tools/scripts/release_public_key_fingerprint.py \
  --public-key $HOME/.capyos/release-ed25519.pub.pem \
  --format colon
```

## Manifesto da chave pública

Depois de obter o fingerprint esperado, o operador pode gerar o manifesto público
que amarra a chave pública ao fingerprint usado pela CI:

```bash
make release-public-key-manifest \
  RELEASE_PUBLIC_KEY=$HOME/.capyos/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...>
```

O manifesto gerado em `build/release-public-key.manifest` é determinístico e não
inclui chave privada. O preflight de CI valida que esse manifesto concorda com a
chave pública e o fingerprint esperados.

## Assinar checksums

Depois de gerar os artefatos de release e os checksums:

```bash
make sign-release-checksums \
  RELEASE_PRIVATE_KEY=$HOME/.capyos/release-ed25519.pem \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem
```

O alvo executa:

1. `release-checksums` para regenerar `build/release-artifacts.sha256`.
2. `tools/scripts/sign_release.py` para assinar com OpenSSL Ed25519.
3. Exportação opcional da chave publica quando `RELEASE_PUBLIC_KEY` e informado.

O script recusa chave privada legivel por grupo/outros, exceto se o operador usar
`--allow-insecure-key` diretamente por motivo documentado.

## Verificar assinatura

```bash
make verify-release-signature \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem
```

Ou diretamente:

```bash
python3 tools/scripts/verify_release_signature.py \
  --input build/release-artifacts.sha256 \
  --signature build/release-artifacts.sha256.sig \
  --public-key build/release-ed25519.pub.pem
```

A verificação valida que a chave publica e Ed25519/SPKI e que a assinatura cobre
exatamente os bytes do arquivo de checksums.

## Pinagem da chave pública

A CI pode fixar o fingerprint SHA-256 da chave pública esperada para impedir que
uma assinatura válida com uma chave pública errada passe pelo gate:

```bash
make verify-release-signature \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...>
```

O mesmo controle existe diretamente no verificador:

```bash
python3 tools/scripts/verify_release_signature.py \
  --input build/release-artifacts.sha256 \
  --signature build/release-artifacts.sha256.sig \
  --public-key build/release-ed25519.pub.pem \
  --expected-public-key-sha256 <hex64-ou-aa:bb:...>
```

A chave privada oficial continua fora do repositório; a CI deve armazenar apenas
a chave pública e/ou seu fingerprint esperado.

Para falhar cedo quando a CI ainda nao foi provisionada, use tambem o preflight
F2 documentado em `docs/operations/release-ci-preflight.md`.

## Conferir pacote público

Antes de publicar a release, o operador pode conferir todos os materiais
públicos sem acessar a chave privada:

```bash
make release-public-materials-check \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest
```

O gate valida sintaxe de checksums, assinatura Ed25519 raw, fingerprint pinado,
manifesto público e assinatura sobre `release-artifacts.sha256`.

## Manifesto de publicação

Depois de conferir o pacote público, gere o manifesto de publicação para anexar à
release:

```bash
make release-publication-manifest \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest
```

O manifesto em `build/release-publication.manifest` resume checksums,
assinatura, chave pública, manifesto da chave e artefatos publicados. Para
conferir esse manifesto sem chave privada:

```bash
make verify-release-publication-manifest \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...>
```

Para validar o contrato público de CI antes do gate agregado:

```bash
make release-ci-publication-contract \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log'"
```

Para validar o provisionamento oficial de CI/release antes da tag:

```bash
make release-ci-official-provisioning-contract \
  RELEASE_TAG=0.8.0-alpha.63+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log' --serial-log build/ci/smoke_x64_vmware.serial.log"
```

Para validar a esteira pública completa de tag:

```bash
make release-ci-tag-gate \
  RELEASE_TAG=0.8.0-alpha.63+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log'"
```

Para gerar o manifesto oficial de handoff publico depois dos gates:

```bash
make release-official-handoff-manifest \
  RELEASE_TAG=0.8.0-alpha.63+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  RELEASE_PUBLICATION_MANIFEST=build/release-publication.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log' --serial-log build/ci/smoke_x64_vmware.serial.log"
```

Para executar assinatura, materiais públicos e manifesto em uma única etapa
pública:

```bash
make release-publication-gate \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest
```

## Self-test negativo

O verificador possui um self-test que gera material temporário, assina um arquivo
mínimo, confirma que a assinatura válida passa e confirma que uma assinatura
mutilada falha fechado:

```bash
make verify-release-signature-selftest
```

Esse alvo não usa a chave oficial e pode rodar em CI antes da chave pública
oficial estar provisionada.

## Publicacao

Para uma release publica, anexar no mesmo pacote:

- ISO UEFI.
- `release-artifacts.sha256`.
- `release-artifacts.sha256.sig`.
- chave publica ou referencia imutavel para a chave publica esperada.
- `release-public-key.manifest`.
- conferência `release-public-materials-check` antes da publicação externa.
- `release-publication.manifest`.
- verificação `verify-release-publication-manifest` antes/apos publicação.
- contrato `release-ci-publication-contract` antes do gate agregado.
- gate agregado `release-publication-gate` antes/apos publicação.
- gate `release-ci-tag-gate` para a esteira pública de tag.
- contrato `release-ci-official-provisioning-contract` para a CI oficial.
- manifesto `release-official-handoff-manifest` para o handoff auditável.
- release note em `docs/releases/`.

## Rotacao

Rotacionar a chave quando houver suspeita de exposicao, troca planejada de
operador ou migração de infraestrutura de CI.

Procedimento recomendado:

1. Gerar nova chave offline.
2. Publicar a nova chave publica em release anterior assinada pela chave antiga,
   quando possivel.
3. Atualizar o caminho `RELEASE_PUBLIC_KEY` usado pela CI/verificacao humana.
4. Revogar a chave antiga nos documentos operacionais.
5. Manter historico de qual chave assinou cada release.

## Compatibilidade com F5

O fetch remoto F5 ja possui o primeiro gate operacional via `update-fetch`: o
manifesto remoto configurado e baixado para uma area temporaria e so substitui o
catalogo local depois de reutilizar os invariantes ja exigidos pelo
`update-agent`:

- versao semanticamente mais nova;
- `payload_sha256` hex64;
- `payload_url` HTTPS ou local sob `/system/update/`, sem espaços ou `..`;
- `signature_ed25519` hex128 no manifesto;
- trilha `channel`/`branch`/`source` compativel, com `develop` em
  `refs/heads/<branch>` e `stable` em `refs/tags/v<major>.<minor>.<patch>`;
- download operacional via `update-download-payload`, que recalcula SHA-256
  real do payload baixado antes de persistir `/system/update/payload.bin`;
- diagnóstico operacional via `update-prepare-explain`, que mostra gates locais
  de catálogo, repositório, payload, assinatura e cache sem efeitos de update;
- preflight operacional via `update-prepare-dry-run`, que revisa catálogo
  local, `payload_url`, assinatura e cache verificado sem staging/arm/apply;
- preparo operacional via `update-prepare`, que encadeia fetch, download
  verificado, staging e arm sem aplicar boot slot;
- apply operacional via `update-apply`, que consome `payload_cache_sha256`
  verificado por padrão; `update-apply <payload_sha256>` segue disponível como
  fallback manual explícito para `update_agent_apply_boot_slot_verified()`;
- conclusão pós-apply via `update-confirm-health` ou rollback assistido via
  `update-rollback-check`.
