# CapyOS — Conferência do pacote público de release

## Objetivo

`tools/scripts/release_public_materials_check.py` confere os materiais públicos
que devem acompanhar uma release antes da publicação externa, sem acessar chave
privada e sem gerar artefatos.

O gate valida que checksums, assinatura, chave pública, fingerprint e manifesto
público estão coerentes entre si.

## Entradas exigidas

- `build/release-artifacts.sha256`
  - lista pública de checksums SHA-256.
- `build/release-artifacts.sha256.sig`
  - assinatura Ed25519 raw de 64 bytes.
- `RELEASE_PUBLIC_KEY` ou `CAPYOS_RELEASE_PUBLIC_KEY`
  - chave pública Ed25519 PEM/SPKI.
- `RELEASE_PUBLIC_KEY_SHA256` ou `CAPYOS_RELEASE_PUBLIC_KEY_SHA256`
  - fingerprint SHA-256 esperado da chave pública.
- `RELEASE_PUBLIC_KEY_MANIFEST` ou `CAPYOS_RELEASE_PUBLIC_KEY_MANIFEST`
  - manifesto público gerado por `release-public-key-manifest`.

## Execução via Makefile

```bash
make release-public-materials-check \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest
```

## Validações

O gate falha fechado quando encontra:

- arquivo de checksums ausente, vazio, não UTF-8 ou malformado;
- artefato listado ausente ou com SHA-256 divergente;
- caminho de artefato absoluto, com `..`, backslash ou NUL;
- assinatura ausente, vazia ou com tamanho diferente de 64 bytes;
- chave pública ausente, vazia ou não Ed25519/SPKI;
- fingerprint esperado ausente, inválido ou divergente;
- manifesto ausente, vazio, não UTF-8, incompleto, duplicado ou com campo
  desconhecido;
- manifesto divergente da chave pública ou do fingerprint esperado;
- assinatura Ed25519 inválida sobre o arquivo de checksums.

## Posição no pipeline

Ordem recomendada para release pública:

```bash
make sign-release-checksums RELEASE_PRIVATE_KEY=... RELEASE_PUBLIC_KEY=...
make release-public-key-fingerprint RELEASE_PUBLIC_KEY=...
make release-public-key-manifest RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-public-materials-check RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-publication-manifest RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make verify-release-publication-manifest RELEASE_PUBLIC_KEY_SHA256=...
make release-ci-publication-contract RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-publication-gate RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-ci-tag-gate RELEASE_TAG=... RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
```

A chave privada continua offline e nunca é consumida por estes gates.
