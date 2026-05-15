# CapyOS — Manifesto público de publicação da release

## Objetivo

`tools/scripts/release_publication_manifest.py` gera um manifesto determinístico
para auditoria dos materiais públicos de uma release, sem acessar chave privada.

O manifesto resume checksums, assinatura, chave pública, fingerprint, manifesto
da chave e artefatos listados em `release-artifacts.sha256`.

## Saída padrão

- `build/release-publication.manifest`
  - formato `capyos-release-publication-manifest-v1`;
  - não contém timestamps;
  - não contém chave privada;
  - pode ser publicado junto da release.

## Execução via Makefile

```bash
make release-publication-manifest \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest
```

## Campos principais

- `format`
- `signature_algorithm`
- `checksum_algorithm`
- `checksums_file`
- `checksums_sha256`
- `signature_file`
- `signature_sha256`
- `public_key_file`
- `public_key_sha256`
- `expected_public_key_sha256`
- `public_key_manifest_file`
- `public_key_manifest_sha256`
- `private_key_included`
- `artifact_count`
- `artifact.N.path`
- `artifact.N.sha256`

## Validações antes da escrita

O gerador reusa os mesmos invariantes do pacote público:

- checksums públicos bem formados;
- caminhos relativos seguros para os artefatos;
- SHA-256 real dos artefatos listados;
- assinatura Ed25519 raw de 64 bytes;
- chave pública Ed25519 PEM/SPKI;
- fingerprint pinado;
- manifesto público da chave coerente;
- assinatura válida sobre `release-artifacts.sha256`.


## Verificação

Depois de gerar ou baixar os materiais públicos, confira o manifesto contra os
arquivos reais:

```bash
make verify-release-publication-manifest \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...>
```

O verificador falha fechado se o manifesto divergir dos checksums, da assinatura,
da chave pública, do manifesto da chave ou dos artefatos listados.


## Gate agregado

Quando todos os materiais públicos estiverem presentes, o operador pode executar
o gate agregado:

```bash
make release-publication-gate \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest
```

## Gate público de CI/tag

A esteira pública de tag pode agregar o preflight, o contrato de publicação e o gate de publicação com:

```bash
make release-ci-tag-gate \
  RELEASE_TAG=0.8.0-alpha.63+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log'"
```
