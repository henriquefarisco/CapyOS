# CapyOS — Assinatura Ed25519 de release

## Objetivo

A trilha oficial de release deve publicar checksums verificaveis, uma assinatura
Ed25519 separada e um `latest.ini` assinado para o `update-agent`. O fluxo
protege o operador contra artefatos trocados depois da geração do checksum
autoritativo e autentica o catálogo remoto antes do download.

Este procedimento cobre a assinatura operacional de artefatos de release. O gate
local de manifestos do `update-agent` continua documentado nas release notes de
`0.8.0-alpha.12` e valida o campo `signature_ed25519=` antes de expor update,
stage ou import.

## Dois contratos de checksum

Existem dois arquivos homônimos em contextos diferentes:

- `make release-checksums` gera `build/release-artifacts.sha256` para cinco
  artefatos locais de build;
- **Release Artifacts** gera `build/release-stage/release-artifacts.sha256` para
  os seis payloads que serão assets do GitHub Release.

O segundo é o checksum autoritativo da publicação. Depois que a tag gera o
draft, o operador deve baixar e assinar exatamente esses bytes. Executar
`make sign-release-checksums` no handoff regenera o primeiro contrato e invalida
a cadeia pública.

## Arquivos gerados

- `build/release-artifacts.sha256`
  - no fluxo local, gerado por `release-checksums` para cinco artefatos;
  - no staging público, gerado pelo workflow para seis payloads finais.
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

## Chave dedicada do update-agent

O manifesto `latest.ini` usa uma chave dedicada, distinta do fluxo histórico de
assinatura dos checksums. A chave pública raw Ed25519 pinada no runtime é:

```text
9a98d2011ba954a3975c9f628e2f9255df87f1f429c9665d51ac6aaf91f474e0
```

Este pin foi provisionado em `0.9.1+20260825` como recuperação autenticada por
nova instalação oficial. O pin anterior
`be230bddb4144dfbcfbf0f24495ed2c8c9acf3866fb48633f4d29e49de69ae6d`
assinou os catálogos públicos `alpha.313` e `alpha.314`, mas a chave privada não
estava disponível para produzir uma release-ponte. Por isso, instalações que
confiam no pin anterior não aceitam catálogos assinados pelo novo pin e devem
ser reinstaladas a partir da ISO oficial `0.9.1+20260825` ou posterior. Essa é
a recuperação fora do updater prevista na política de rotação abaixo; não deve
ser descrita como rotação transparente.

`tools/scripts/build_update_manifest.py` extrai somente a parte pública da chave
privada fornecida pelo operador e recusa assinar quando ela não corresponde a
esse pin. `tools/scripts/verify_update_manifest.py` verifica a mesma chave, a
assinatura sobre os bytes canônicos e o SHA-256 do payload. A chave privada do
update-agent permanece offline e não deve ser configurada como secret do
workflow; a CI publica apenas `latest.unsigned.ini` como handoff não implantável.

## Fingerprint versionado da chave de checksums

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

Essa variável continua útil para comandos locais, mas não é a autoridade do
promoter e não deve ser criada como variável do repositório GitHub. A política
oficial é
`.github/release-policy/release-checksum-ed25519.sha256`, lida do commit da
própria tag. Antes da primeira tag assinada, o arquivo deve conter exatamente o
fingerprint real aprovado da chave dedicada de checksums: uma única linha
minúscula `hex64` terminada por LF. Placeholder, pin ausente, symlink, linha
extra, formato não canônico ou reutilização da chave distinta do update-agent
fazem a promoção falhar fechado.

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

## Assinar checksums locais

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

Esse alvo não deve ser usado para assinar o staging público. Para o draft do
GitHub, use diretamente:

```bash
python3 tools/scripts/sign_release.py \
  --input "$BUNDLE/release-artifacts.sha256" \
  --private-key "$OFFLINE_RELEASE_KEY" \
  --signature "$BUNDLE/release-artifacts.sha256.sig" \
  --public-key-out "$BUNDLE/release-ed25519.pub.pem"
```

`$BUNDLE` deve conter os sete assets-base baixados do draft e nenhuma chave
privada.

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

## Verificação local da pinagem

Os gates locais aceitam o fingerprint SHA-256 esperado por argumento/ambiente
para impedir que uma assinatura válida com uma chave pública errada passe:

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

A chave privada oficial continua fora do repositório. Na promoção GitHub, a
chave pública vem do asset `release-ed25519.pub.pem` e o fingerprint esperado
vem exclusivamente do arquivo de política versionado no commit da tag; uma
variável mutável de repositório não pode substituir esse pin.

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

Depois de conferir o pacote local, o alvo Make pode gerar o manifesto local:

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

No fluxo oficial, gere o manifesto sobre o bundle baixado do draft e vincule a
identidade histórica da release à versão estendida sem `v`:

```bash
python3 tools/scripts/release_publication_manifest.py \
  --checksums "$BUNDLE/release-artifacts.sha256" \
  --signature "$BUNDLE/release-artifacts.sha256.sig" \
  --artifact-root "$BUNDLE" \
  --public-key "$BUNDLE/release-ed25519.pub.pem" \
  --expected-public-key-sha256 "$RELEASE_KEY_SHA256" \
  --public-key-manifest "$BUNDLE/release-public-key.manifest" \
  --release-id "$VERSION" \
  --output "$BUNDLE/release-publication.manifest" --force
```

O ID numérico retornado pela API do GitHub não substitui `release_id`.

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

O conjunto público permitido tem exatamente 12 assets:

- ISO UEFI, `capyos64.bin`, `manifest.bin`, `modules-index.txt`,
  `modules.sha256` e exatamente um payload CapyAI;
- `release-artifacts.sha256` e sua `.sig` raw Ed25519;
- `release-ed25519.pub.pem`, `release-public-key.manifest` e
  `release-publication.manifest`;
- `latest.ini` assinado pela chave dedicada do update-agent.

O workflow da tag deixa os sete assets-base em draft. Depois do handoff offline,
**Promote Signed Release** exige o fingerprint aprovado no arquivo versionado
`.github/release-policy/release-checksum-ed25519.sha256` do commit da tag, a
consulta de `immutable-releases` e dos dois rulesets completos via
`CAPYOS_RELEASE_POLICY_AUDIT_TOKEN`, o ruleset de tag sem bypass indicado por
`CAPYOS_RELEASE_TAG_RULESET_ID`, o ruleset de branch sem bypass indicado por
`CAPYOS_RELEASE_MAIN_RULESET_ID` e, para o draft mutável, a tag no commit exato
de `main`. O token deve ser um PAT/App fine-grained de curta duração, limitado
ao repositório e com **Administration: write**: a permissão elevada faz a API
expor `bypass_actors`, mas o workflow executa somente `GET`. Resposta sem esse
campo falha fechado; revogue ou rotacione o token após a janela. O primeiro
bloqueia update/deletion exatamente em
`refs/tags/v*`; o segundo cobre exatamente `refs/heads/main`, bloqueia deletion
e non-fast-forward e exige pull request com o perfil permanente solo: exatamente
zero aprovação, squash-only, resolução de threads e os seis checks strict
vinculados aos apps esperados (`Lint`, `Release gates`, `QEMU ISO smoke`, dois
`Analyze` e `CodeQL`). Sem o pin real,
imutabilidade, leitura completa das políticas, qualquer ruleset ou os 12 assets
exatos, a promoção inicial falha fechado. Ela verifica o draft autenticado e,
numa única mutação, publica a release e a marca como Latest.
Depois exige que a release esteja imutável, baixa os 12 assets novamente sem
autenticação e repete os gates. Por
fim, verifica `latest.ini` e `capyos64.bin` pela rota
`releases/latest/download/` consumida pelo runtime. Um rerun após a mutação é
somente uma revalidação idempotente da mesma release imutável.

Essa verificação do ruleset de `main` prova proteção corrente, não a procedência
histórica do commit por PR/review. O builder e o promoter compartilham o lock
global `release-publication`, mas esse lock cobre apenas workflows. Depois de
anexar os cinco materiais e até a conclusão terminal da promoção, nenhum
operador pode alterar assets, a tag, `main`, immutable releases ou qualquer dos
dois rulesets. O promoter reconsulta as três políticas imediatamente antes do
único `PATCH`. Uma intervenção manual exige deixar o run falhar e revalidar o
bundle e as políticas fora dessa janela antes de novo dispatch.

Consulte `docs/operations/release-process.md` para os comandos completos. Uma
release sem `.sig`, manifestos públicos ou `latest.ini` pode ter integridade
SHA-256 conferida, mas não satisfaz a política de publicação Ed25519.

## Rotacao das duas chaves

As chaves não são intercambiáveis e têm procedimentos diferentes.

Para a chave que assina `release-artifacts.sha256`:

1. Gere a nova chave offline e publique a transição autenticada pela chave
   anterior, quando possível.
2. Atualize `.github/release-policy/release-checksum-ed25519.sha256`, o material
   de verificação humana e os documentos de fingerprint **antes** de criar a
   primeira tag assinada pela chave nova. Nunca commite placeholder.
3. Revogue a chave antiga e mantenha o histórico de qual chave assinou cada
   release.

Para a chave dedicada do `latest.ini`/update-agent, trocar apenas um secret ou
variável não funciona: a chave pública raw está pinada em
`src/services/update_agent_parse.c` e em
`tools/scripts/update_manifest_common.py`. Uma rotação planejada exige uma
release-ponte cujo manifesto ainda seja assinado pela chave antiga, mas cujo
payload instale o novo pin; só depois da janela de migração os manifestos passam
a usar a chave nova. Se a chave antiga estiver comprometida, esse encadeamento
não é confiável e a recuperação exige um canal autenticado fora do updater
(por exemplo, nova instalação oficial). Registre a janela, os fingerprints e a
política de revogação em ambos os casos.

## Compatibilidade com F5

O fetch remoto F5 ja possui o primeiro gate operacional via `update-fetch`: o
manifesto remoto configurado e baixado para uma area temporaria e so substitui o
catalogo local depois de reutilizar os invariantes ja exigidos pelo
`update-agent`:

- versao semanticamente mais nova;
- `payload_sha256` hex64;
- `payload_url` HTTPS ou local sob `/system/update/`, sem espaços ou `..`;
- `payload_size` decimal positivo de até 8 MiB nos manifestos novos;
- `signature_ed25519` hex128 no manifesto;
- trilha `channel`/`branch`/`source` compativel, com `develop` em
  `refs/heads/<branch>` e `stable` no asset mutável autenticado
  `releases/latest/download/latest.ini`;
- download operacional via `update-download-payload`, que usa o
  `payload_size` assinado para limitar seu buffer temporário, valida
  tamanho/SHA-256, persiste `/system/update/payload.bin` e repete a validação
  após readback;
- diagnóstico operacional via `update-prepare-explain`, que mostra gates locais
  de catálogo, repositório, payload, assinatura e cache sem efeitos de update;
- preflight operacional via `update-prepare-dry-run`, que revisa catálogo
  local, `payload_url`, assinatura e recalcula o cache persistido sem
  staging/arm/apply;
- `update-prepare-explain` termina no gate `persistence` quando todos os gates
  criptográficos e de cache passam;
- `update-prepare`, `update-stage`, `update-arm on` e `update-apply` usam o
  provider persistente entregue em `alpha.318`. Eles retornam
  `UPDATE_AGENT_ERR_UNSUPPORTED` (`-60`) somente quando a transição durável não
  pode ser provada — por exemplo, provider/binding inválido, lease ou geração
  divergente, payload não verificado, ausência de flush ou commit
  indeterminado. A recusa continua fail-closed e impede que metadata apenas em
  RAM seja reportada como atualização aplicada.
