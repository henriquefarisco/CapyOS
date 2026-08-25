# CapyOS — conferência do pacote público de release

## Objetivo

`release_public_materials_check.py` e `release_publication_gate.py` validam os
materiais públicos sem consumir uma chave privada. Na promoção oficial eles
operam sobre os bytes baixados do GitHub Release, não sobre os artefatos locais
regerados pelo Makefile.

## Entradas exigidas

- `release-artifacts.sha256`, com exatamente os seis payloads públicos;
- `release-artifacts.sha256.sig`, assinatura Ed25519 raw de 64 bytes;
- `release-ed25519.pub.pem`;
- fingerprint real aprovado em
  `.github/release-policy/release-checksum-ed25519.sha256` no commit da tag;
- `release-public-key.manifest`;
- `release-publication.manifest` com `release_id=<versão-estendida-sem-v>`;
- os seis payloads referenciados pelo checksum.

O `latest.ini` assinado é validado separadamente por
`verify_update_manifest.py`, pois usa a chave dedicada pinada pelo update-agent.
Essa chave é distinta do signer de checksums e seu pin não pode ser reutilizado
no arquivo de política. Para uma invocação local, `RELEASE_KEY_SHA256` abaixo
deve receber exatamente o valor versionado; a promoção oficial não lê
`CAPYOS_RELEASE_PUBLIC_KEY_SHA256` de uma variável do repositório GitHub.

## Execução sobre o bundle staged

```sh
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
```

Antes do gate criptográfico, a promoção executa:

```sh
python3 tools/scripts/verify_release_promotion_bundle.py --bundle-dir "$BUNDLE"
```

Esse verificador exige o conjunto exato de 12 assets e confirma que
`release-artifacts.sha256` cobre, em ordem lexicográfica, somente os seis
payloads. Ele também rejeita diretórios, links simbólicos, arquivos vazios,
nomes inseguros, múltiplos payloads CapyAI e assinatura com tamanho incorreto.

## Limite importante

`make sign-release-checksums` continua válido para o pacote local de cinco
artefatos descrito em `docs/security/release-signing.md`, mas não deve ser usado
no handoff do GitHub draft: ele regenera outro inventário. Para a release
pública, `sign_release.py` deve assinar diretamente o
`release-artifacts.sha256` baixado do draft.

A chave privada continua offline e nunca é consumida por esses gates.
