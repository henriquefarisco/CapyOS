# CapyOS — processo de release

Este runbook descreve a publicação stable/alpha em duas fases: o workflow da
tag produz um draft autenticado pela API do GitHub; um operador offline assina
os bytes exatos; uma segunda workflow valida o bundle autenticado, publica e
marca Latest numa única mutação e repete os gates pelas rotas públicas. A
imutabilidade do GitHub Release é pré-requisito dessa transição.

## 1. Política de batching e versão

Uma mudança de versão é um evento de release completo. Agrupe correções
relacionadas numa unidade de valor coerente e evite tags para passos mecânicos
isolados. `make bump-alpha` continua sendo o caminho mantido para incrementos do
canal alpha; stable segue o contrato de `VERSION.yaml`,
`include/core/version.h`, `README.md` e `docs/releases/` validado por
`make version-audit`.

O repositório de trabalho é `CapyOS`; a pasta pai é apenas o agregador dos
repositórios irmãos. Alterações cross-repo devem respeitar os pins de
`VERSION.yaml` e a matriz de compatibilidade.

## 2. Validação e criação do draft

1. Desenvolva em branch baseada na `main` atual e execute os gates proporcionais
   à mudança: testes focados, `make test`, `make layout-audit`,
   `make version-audit`, build real e os smokes QEMU/VMware aplicáveis.
2. Abra pull request para `main` e confirme o perfil permanente solo fail-closed
   exigido pelo ruleset: zero aprovação humana, branch atualizada, threads
   resolvidas, somente squash merge e os seis checks autenticados descritos
   abaixo. Somente
   após o merge, com o checkout local em fast-forward exato de `origin/main`,
   crie a tag `v<versão-estendida>`.
3. O workflow **Release Artifacts** recompila a tag, valida os irmãos pinados e
   cria ou atualiza somente um GitHub Release em estado `draft`.
4. O draft deve conter exatamente sete assets-base:

   - `CapyOS-Installer-UEFI.iso`;
   - `capyos64.bin`;
   - `manifest.bin`;
   - `modules-index.txt`;
   - `modules.sha256`;
   - um `org.capyos.ai.assistant-<versão>.bin`;
   - `release-artifacts.sha256`, cobrindo exatamente os seis payloads acima em
     ordem lexicográfica.

O workflow também preserva por 14 dias um artifact de handoff com esses bytes e
`latest.unsigned.ini`. Nesse ponto `modules-index.txt` é deliberadamente sem
assinatura: o workflow valida corpo canônico, hashes e payloads somente com o
opt-in explícito de pré-publicação. Ele não publica o draft nem o marca como
Latest.

## 3. Handoff e assinatura offline

Baixe os sete assets-base do draft para um diretório isolado. Primeiro substitua
o índice do handoff autenticado pela forma assinada offline com a chave dedicada
do publisher CapyPKG e recalcule os dois arquivos de checksum:

```sh
python3 tools/scripts/sign_modules_index.py \
  --workspace "$(dirname "$PWD")" \
  --private-key "$OFFLINE_CAPYPKG_PUBLISHER_KEY" \
  --release-tag "$TAG" \
  --output "$BUNDLE/modules-index.txt"
(
  cd "$BUNDLE"
  sha256sum modules-index.txt org.capyos.ai.assistant-*.bin > modules.sha256
  mapfile -t PAYLOADS < <(
    find . -maxdepth 1 -type f \
      ! -name release-artifacts.sha256 \
      ! -name 'release-*.sig' \
      ! -name 'release-*.pem' \
      ! -name 'release-*.manifest' \
      ! -name latest.ini \
      -printf '%f\n' | LC_ALL=C sort
  )
  test "${#PAYLOADS[@]}" -eq 6
  sha256sum "${PAYLOADS[@]}" > release-artifacts.sha256
)
```

A saída deve ser reconstruída dos tags imutáveis exatos pinados pelo tag do
CapyOS. O signer confirma que a chave privada corresponde à âncora pública
compilada no kernel taggeado. Não execute `make sign-release-checksums` nesse
diretório: esse alvo regenera o checksum
local de cinco artefatos e não representa o conjunto público de seis payloads.
Defina a identidade exata do handoff e derive `PUBLISHED_AT` pelo mesmo contrato
do workflow: metadata `+YYYYMMDD`, quando presente; caso contrário, data do
commit da tag.

```sh
REPOSITORY=henriquefarisco/CapyOS
: "${TAG:?defina TAG como a nova tag ainda em draft}"
: "${BUNDLE:?defina BUNDLE como o diretorio isolado do handoff}"
RELEASE_POLICY_PATH=.github/release-policy/release-checksum-ed25519.sha256
VERSION="${TAG#v}"
RELEASE_KEY_SHA256="$(git show "${TAG}^{commit}:$RELEASE_POLICY_PATH")"
BUILD_DATE="${VERSION##*+}"
if printf '%s\n' "$BUILD_DATE" | grep -Eq '^[0-9]{8}$'; then
  PUBLISHED_AT="${BUILD_DATE:0:4}-${BUILD_DATE:4:2}-${BUILD_DATE:6:2}"
else
  PUBLISHED_AT="$(git show -s --format=%cs "${TAG}^{commit}")"
fi
```

`RELEASE_KEY_SHA256` não é escolhido no terminal nem lido de uma variável do
repositório GitHub. A autoridade é o arquivo versionado
`.github/release-policy/release-checksum-ed25519.sha256` do commit da própria
tag. Antes da primeira tag assinada, ele deve conter o fingerprint **real e
aprovado** da chave dedicada que assina `release-artifacts.sha256`: exatamente
uma linha `hex64` minúscula terminada por LF. Não use nesse arquivo o pin da
chave distinta do `latest.ini`/update-agent. Arquivo ausente, symlink, linha
extra ou formato divergente deixam a promoção fail-closed.

Assine diretamente o `release-artifacts.sha256` baixado:

```sh
python3 tools/scripts/sign_release.py \
  --input "$BUNDLE/release-artifacts.sha256" \
  --private-key "$OFFLINE_RELEASE_KEY" \
  --signature "$BUNDLE/release-artifacts.sha256.sig" \
  --public-key-out "$BUNDLE/release-ed25519.pub.pem"

python3 tools/scripts/release_public_key_manifest.py \
  --public-key "$BUNDLE/release-ed25519.pub.pem" \
  --expected-public-key-sha256 "$RELEASE_KEY_SHA256" \
  --output "$BUNDLE/release-public-key.manifest" \
  --force

python3 tools/scripts/build_update_manifest.py \
  --version "$VERSION" --channel stable --branch main \
  --source "github:$REPOSITORY" --published-at "$PUBLISHED_AT" \
  --payload "$BUNDLE/capyos64.bin" \
  --payload-url "https://github.com/$REPOSITORY/releases/download/$TAG/capyos64.bin" \
  --private-key "$OFFLINE_UPDATE_KEY" \
  --output "$BUNDLE/latest.ini" --force

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

`release_id` mantém a semântica histórica: versão estendida sem o prefixo `v`.
O ID numérico da API GitHub não entra nesse campo. As duas chaves privadas
permanecem fora do workspace, Actions e GitHub Release.

Antes do upload, valide localmente o conjunto final de 12 arquivos e todos os
campos vinculados à tag. A validação deve terminar verde sem regenerar nenhum
byte:

```sh
PAYLOAD_URL="https://github.com/$REPOSITORY/releases/download/$TAG/capyos64.bin"

python3 tools/scripts/verify_release_promotion_bundle.py \
  --bundle-dir "$BUNDLE"
(
  cd "$BUNDLE"
  sha256sum -c modules.sha256
)
python3 tools/scripts/release_publication_gate.py \
  --checksums "$BUNDLE/release-artifacts.sha256" \
  --signature "$BUNDLE/release-artifacts.sha256.sig" \
  --artifact-root "$BUNDLE" \
  --materials-root "$BUNDLE" \
  --public-key "$BUNDLE/release-ed25519.pub.pem" \
  --expected-public-key-sha256 "$RELEASE_KEY_SHA256" \
  --public-key-manifest "$BUNDLE/release-public-key.manifest" \
  --publication-manifest "$BUNDLE/release-publication.manifest" \
  --expected-release-id "$VERSION"
python3 tools/scripts/verify_update_manifest.py \
  --manifest "$BUNDLE/latest.ini" \
  --payload "$BUNDLE/capyos64.bin" \
  --expected-version "$VERSION" \
  --expected-channel stable \
  --expected-branch main \
  --expected-source "github:$REPOSITORY" \
  --expected-published-at "$PUBLISHED_AT" \
  --expected-payload-url "$PAYLOAD_URL"
python3 tools/scripts/verify_modules_index_assets.py \
  --index "$BUNDLE/modules-index.txt" \
  --release-tag "$TAG" \
  --local-payload-dir "$BUNDLE"
```

Anexe ao draft somente os cinco materiais novos:

- `release-artifacts.sha256.sig`;
- `release-ed25519.pub.pem`;
- `release-public-key.manifest`;
- `release-publication.manifest`;
- `latest.ini`.

```sh
gh release upload "$TAG" \
  "$BUNDLE/release-artifacts.sha256.sig" \
  "$BUNDLE/release-ed25519.pub.pem" \
  "$BUNDLE/release-public-key.manifest" \
  "$BUNDLE/release-publication.manifest" \
  "$BUNDLE/latest.ini" \
  --repo "$REPOSITORY" --clobber
```

O resultado ainda é um draft, agora com o conjunto exato de 12 assets. Qualquer
asset extra, ausente, vazio, link simbólico ou checksum divergente bloqueia a
promoção.

## 4. Promoção assinada

O fingerprint aprovado do signer de checksums deve estar commitado no arquivo
de política acima **antes** da tag. Não configure
`CAPYOS_RELEASE_PUBLIC_KEY_SHA256` como variável de repositório: o promoter lê o
pin histórico do commit da própria tag, o que também mantém uma revalidação
idempotente correta depois de uma rotação futura.

Para a primeira promoção do draft, configure os identificadores dos dois
rulesets e a credencial de auditoria de políticas; depois confirme a proteção
de releases:

```sh
gh variable set CAPYOS_RELEASE_TAG_RULESET_ID \
  --body "$RULESET_ID" --repo "$REPOSITORY"

gh variable set CAPYOS_RELEASE_MAIN_RULESET_ID \
  --body "$MAIN_RULESET_ID" --repo "$REPOSITORY"

gh secret set CAPYOS_RELEASE_POLICY_AUDIT_TOKEN --repo "$REPOSITORY"

gh api "repos/$REPOSITORY/immutable-releases" --jq '.enabled'
```

O secret deve conter um PAT ou token de GitHub App fine-grained, limitado a esse
repositório e com **Administration: write**. Esse privilégio é elevado, porém
necessário para a API expor `bypass_actors` nas respostas dos rulesets; o
workflow usa a credencial somente em requisições `GET` a `immutable-releases` e
aos dois rulesets. Use o menor escopo de repositório e a menor expiração que
cubram a janela de promoção, depois revogue ou rotacione a credencial. Cada
resposta de ruleset precisa expor `bypass_actors` explicitamente; campo omitido é
recusado para não confundir uma resposta parcial com ausência de bypass.
`RULESET_ID` deve
identificar um ruleset de tag ativo, sem bypass ou exclusões,
aplicado exatamente a `refs/tags/v*` com **Restrict updates** e **Restrict
deletions**. Ele permite criar a tag, mas impede que sua identidade mude durante
o handoff. `MAIN_RULESET_ID` deve identificar outro ruleset ativo, também sem
bypass ou exclusões, aplicado exatamente a `refs/heads/main`; ele deve restringir
deletion e non-fast-forward e exigir pull request. O perfil permanente solo
exige exatamente zero aprovação, somente squash merge, resolução de threads e
checks strict, sem isenção na criação, vinculados às
integrações esperadas: `Lint`, `Release gates`, `QEMU ISO smoke`,
`Analyze (c-cpp)`, `Analyze (python)` e `CodeQL`. O último comando deve retornar
`true`. Um administrador deve habilitar **immutable
releases** nas configurações do repositório antes da promoção. O workflow baixa
as três políticas e as valida com
`tools/scripts/verify_release_repository_policy.py`.

O ruleset de `main` comprova a proteção **vigente no momento da promoção**. Ele
não é evidência histórica de que o commit já apontado pela tag entrou por pull
request ou recebeu review; qualquer afirmação de procedência revisada precisa de
evidência separada.

Antes do dispatch inicial, confirme em conjunto: tag no commit exato de `main`
selecionado; pin versionado válido e correspondente a
`release-ed25519.pub.pem`; draft com exatamente os 12 assets já validados;
imutabilidade habilitada; credencial de auditoria que exponha as três políticas
completas; e ruleset de tag sem bypass **mais** ruleset protegido de `main`. A promoção
inicial falha fechado se qualquer pré-requisito faltar. O token de auditoria de
política é usado apenas para consultar imutabilidade e rulesets; a
mutação da release usa o `GITHUB_TOKEN` com `contents: write`. Numa retomada da
mesma release já publicada, imutável e Latest, não há nova mutação: o workflow
usa o pin histórico da tag e repete somente as verificações.

Depois do último `gh release upload`, encerre a fase de anexação e abra uma
**janela exclusiva de promoção**. Do último upload — inclusive no intervalo
antes do dispatch — até a conclusão terminal do workflow, ninguém deve: alterar
assets; mover, excluir ou recriar a tag ou `refs/heads/main`; mudar o estado de
immutable releases; nem editar, desabilitar ou adicionar bypass a qualquer dos
dois rulesets. O promoter reconsulta as três políticas no mesmo passo,
imediatamente antes do único `PATCH`, e também relê o release/inventário e a tag
remota. Isso reduz a janela de corrida, mas não substitui o congelamento humano.
O lock global `release-publication` serializa builder e promoter entre todas as
tags, mas não bloqueia ações humanas fora de Actions. Se houver intervenção
manual, deixe a execução falhar, restaure e revalide o bundle e as políticas
fora da janela e só então faça um novo dispatch.

Dispare **Promote Signed Release** a partir de `main`, informando a tag e a
confirmação `promote-signed-draft`.

A workflow:

1. exige que a tag válida pertença ao histórico de `main` e tenha versão
   idêntica; para promover um draft mutável, exige também que ela aponte para o
   commit exato selecionado;
2. exige imutabilidade habilitada, o ruleset sem bypass que impede
   atualização/exclusão de `refs/tags/v*` e o ruleset sem bypass de
   `refs/heads/main` com deletion/non-fast-forward bloqueados, PR obrigatório e
   perfil permanente solo com checks autenticados;
3. aceita o draft stable exato ou, numa retomada idempotente, a mesma release já
   publicada, imutável e Latest;
4. captura IDs, nomes, tamanhos, digests e timestamps dos 12 assets;
5. lê o fingerprint versionado do commit da tag e valida inventário, checksums,
   Ed25519, manifestos, `latest.ini` e índice de módulos;
6. imediatamente antes da mutação, baixa e valida novamente immutable releases
   e ambos os rulesets, relê o release/inventário e compara o commit da tag
   remota;
7. recusa qualquer divergência contra os snapshots validados;
8. num único `PATCH`, publica o draft e o marca como Latest;
9. exige que a release tenha se tornado imutável, confirma novamente o commit
   da tag, baixa os 12 assets pelas URLs públicas sem token e repete todos os
   gates;
10. confirma `/releases/latest` e revalida somente `latest.ini` e
   `capyos64.bin` por
   `releases/latest/download/latest.ini`, rota consumida pelo runtime stable.

O builder e o promoter usam um único lock global de publicação, inclusive entre
tags diferentes. Esse lock não substitui a janela humana exclusiva descrita
acima. Um rerun do builder recusa tocar num draft que já contenha material
offline assinado. Se uma falha de propagação ocorrer depois do `PATCH`, o rerun
do promoter não publica de novo:
ele reconhece a release imutável que já é Latest e repete as verificações
públicas. Essa retomada somente leitura continua possível após novos commits em
`main`, desde que a tag permaneça ancestral do commit selecionado.

## 5. Encerramento

Confirme workflows verdes, título/notas da release, 12 assets públicos e o tag
retornado por `/releases/latest`. Depois da promoção, execute o ciclo A/B de
produção no VMware oficial com
`smoke-x64-vmware-update-ab-production-existing-iso`; esse é um gate de aceite
da Etapa 8 e não é substituído pelas verificações criptográficas de publicação.
A ISO predecessora precisa ser um release público estritamente anterior que já
contenha a mesma âncora de produção. O primeiro release após uma rotação de chave
é somente o bootstrap desse requisito e o aceite fica, de forma explícita,
pendente até o release seguinte — nunca use uma ISO retroativa, override de
versão ou o gate com chave de laboratório como substituto. Preserve a evidência
e remova apenas temporários e VMs descartáveis cuja identidade tenha sido
confirmada. A existência do artifact ou da tag, isoladamente, não conclui a
publicação nem fecha a Etapa 8.
