# CapyOS — Contrato público de CI para publicação

## Objetivo

`tools/scripts/release_ci_publication_contract.py` valida o contrato público que a
CI deve satisfazer antes de executar os gates de publicação de uma tag.

O contrato não assina, não liga VM, não chama `make`, não chama `git` e não lê
chave privada. Ele verifica a estrutura pública esperada para a esteira F2.

## Execução via Makefile

```bash
make release-ci-publication-contract \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log'"
```

## Entradas conferidas

- `build/release-artifacts.sha256`
- `build/release-artifacts.sha256.sig`
- chave pública Ed25519 exportada
- `build/release-public-key.manifest`
- `build/release-publication.manifest`
- `RELEASE_PUBLIC_KEY_SHA256`
- `SMOKE_X64_VMWARE_ARGS`

## Regras de segurança

- Rejeita `RELEASE_PRIVATE_KEY` e `CAPYOS_RELEASE_PRIVATE_KEY` no ambiente.
- Exige fingerprint SHA-256 pinado.
- Rejeita manifesto público com chave privada indicada.
- Rejeita checksums e manifestos malformados.
- Rejeita `--dry-run`, `--no-artifact-check`, `--no-poweroff` e
  `--no-tool-check` nos argumentos VMware de CI.
- Para `govc`, exige `GOVC_URL`, `GOVC_USERNAME`, `GOVC_DATACENTER` e
  `GOVC_PASSWORD` ou `GOVC_PASSWORD_FILE`.

## Diferença para os gates criptográficos

Este contrato valida coerência estrutural e operacional para CI. A verificação
criptográfica completa continua nos gates públicos:

- `verify-release-signature`
- `release-public-materials-check`
- `verify-release-publication-manifest`
- `release-publication-gate`

O `RELEASE_PUBLIC_KEY_SHA256` passado aos alvos locais não é a autoridade da
promoção GitHub. O promoter obtém o fingerprint exclusivamente de
`.github/release-policy/release-checksum-ed25519.sha256` no commit da própria
tag; não existe fallback para uma variável mutável do repositório.

## Gate de tag e promoção GitHub

`release-ci-tag-gate` continua disponível para handoffs F2/CI privada que
possuam evidência VMware. O workflow público da tag não executa esse contrato:
ele não recebe chaves nem material offline e termina num draft. Depois que os
cinco materiais assinados são anexados, **Promote Signed Release** executa o
inventário exato, `release-publication-gate`, validação do `latest.ini` e dos
módulos no draft autenticado. Com releases imutáveis habilitadas, publica e
marca Latest numa única mutação e repete os mesmos gates pelas URLs públicas da
tag. Antes da mutação, o pin versionado deve existir como uma única linha
`hex64` minúscula + LF e corresponder à chave de checksums publicada; pin
ausente, placeholder ou chave do update-agent falha fechado. A configuração
remota é comprovada pelo secret `CAPYOS_RELEASE_POLICY_AUDIT_TOKEN`: PAT/App
fine-grained de curta duração, limitado ao repositório e com
**Administration: write**. Esse privilégio elevado é necessário para a API
expor `bypass_actors`, embora o workflow use o token somente em `GET` de
`immutable-releases` e dos dois rulesets; resposta sem o campo falha fechado e
a credencial deve ser revogada ou rotacionada após a janela. O ruleset de tag
sem bypass indicado por `CAPYOS_RELEASE_TAG_RULESET_ID` impede update/deletion
exatamente em `refs/tags/v*`. O ruleset de branch sem bypass indicado por
`CAPYOS_RELEASE_MAIN_RULESET_ID` cobre exatamente `refs/heads/main`, impede
deletion/non-fast-forward e exige pull request com o perfil permanente solo
fail-closed: exatamente zero aprovação, squash-only, threads resolvidas e os seis
checks strict vinculados às integrações oficiais. Immutable
releases também precisa estar habilitado; as três políticas são avaliadas por
`tools/scripts/verify_release_repository_policy.py`. O `GITHUB_TOKEN` permanece
responsável apenas pela release. Uma retomada após a mutação apenas revalida a
publicação imutável com o pin histórico da tag.

Builder e promoter usam o lock global `release-publication`, mas ações manuais
não participam desse lock. Do último upload até a conclusão terminal da
promoção, ninguém pode alterar assets, a tag, `main`, immutable releases ou
qualquer dos dois rulesets. O promoter reconsulta as três políticas no mesmo
passo, imediatamente antes do único `PATCH`. O ruleset de `main` prova a
proteção corrente; não prova que o commit da tag passou historicamente por
PR/review.

```bash
make release-ci-tag-gate \
  RELEASE_TAG=0.8.0-alpha.93+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log'"
```
