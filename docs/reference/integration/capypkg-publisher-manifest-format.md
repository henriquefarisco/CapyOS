# capypkg publisher manifest format

**Status:** autoritativo; formato v2 desde 2026-09-03.
**Audiência:** quem publica pacotes Capy remotos consumidos pelo
adapter in-tree `services/capypkg` do CapyOS core.
**Fonte da verdade do parser:** `CapyOS/include/services/capypkg.h` e
`CapyOS/src/services/capypkg/capypkg_manifest.c`.
**Design rationale:** `CapyOS/docs/architecture/capypkg-adapter.md`.

Este documento existe porque os exemplos de descritor JSON em outros
repos (`CapyAgent/docs/component-index-example.md`) descrevem um
**índice de alto nível** (registro org-style) que pode evoluir
livremente, enquanto o adapter in-tree consome um **manifest
line-oriented `key=value`** com regras estritas e fail-closed. Os dois
formatos não são equivalentes; este doc define o segundo.

## 1. Formato

Linha-oriented `key=value`. Cada par fica em uma única linha terminada
em `\n`. Linhas em branco e comentários (`#` na primeira coluna) são
toleradas. Chaves desconhecidas são ignoradas (forward-compat).

Para publicar vários pacotes em um único índice, separe descritores
com o separador literal `---` em linha própria:

```text
name=org.capyos.codecs.image-basic
version=0.0.1
summary=CapyCodecs Image Basic
payload_url=https://example.org/capycodecs-image-basic-v0.0.1.bin
payload_sha256=<64 hex>
payload_size=<bytes>
signature_ed25519=<128 hex>
install_root=/var/capypkg/org.capyos.codecs.image-basic
depends=
---
name=org.capyos.browser.core
version=0.0.1
summary=CapyBrowser Core
payload_url=https://example.org/capybrowser-core-v0.0.1.bin
payload_sha256=<64 hex>
payload_size=<bytes>
signature_ed25519=<128 hex>
install_root=/var/capypkg/org.capyos.browser.core
depends=org.capyos.codecs.image-basic
---
```

## 2. Campos obrigatórios

| Campo | Regra | Erro se violar |
|---|---|---|
| `name` | 1-63 chars do alfabeto `[a-zA-Z0-9._-]`; recusa nomes só-pontos (`.`, `..`, `...`) | `CAPYPKG_ERR_DENIED` |
| `version` | texto opaco; recomendado semver | `CAPYPKG_ERR_PARSE` se vazio |
| `payload_url` | obrigatoriamente começa com `https://` | `CAPYPKG_ERR_DENIED` |
| `payload_sha256` | exatamente 64 dígitos hex (case insensitive aceito; padronize lowercase) | `CAPYPKG_ERR_PARSE` |

## 3. Campos opcionais

| Campo | Regra | Erro se violar |
|---|---|---|
| `summary` | printable ASCII 0x20-0x7E | `CAPYPKG_ERR_DENIED` |
| `payload_size` | decimal `uint32_t`; ≤ `CAPYPKG_PAYLOAD_MAX` (8 MiB); overflow rejeitado | `CAPYPKG_ERR_PARSE` ou `CAPYPKG_ERR_QUOTA` |
| `signature_ed25519` | exatamente 128 dígitos hex; obrigatório se repo é `signed` | `CAPYPKG_ERR_PARSE` ou `CAPYPKG_ERR_SIGNATURE` |
| `install_root` | absoluto; sob `/var/capypkg` ou `/opt/`; sem segmento `..`; directory boundary respeitado | `CAPYPKG_ERR_DENIED` |
| `depends` | lista separada por vírgula; cada item segue alfabeto do `name`; ≤ 8 itens | `CAPYPKG_ERR_PARSE` ou `CAPYPKG_ERR_DENIED` |
| `repo` | não preencha; é setado pelo adapter no fetch | n/a |
| `provides_abi` | nome ASCII do ABI publicado | `CAPYPKG_ERR_PARSE` |
| `abi_version` | versão opaca não vazia do ABI | `CAPYPKG_ERR_PARSE` |
| `core_abi_min` / `core_abi_max` | intervalo decimal inclusivo; deve conter o ABI 3 | `CAPYPKG_ERR_INCOMPATIBLE` |
| `known_good` | `0` ou `1`; só `1` entra no índice oficial resolvido | `CAPYPKG_ERR_INCOMPATIBLE` |

## 4. Envelope oficial resolve-at-publish

O índice oficial começa, em ordem exata, com cinco linhas autenticadas:

```text
#capyos-modules-index-v2
#index_abi_token=capyos-base-v3
#index_epoch=1
#index_body_sha256=<64 hex>
#index_signature_ed25519=<128 hex>
```

O SHA-256 cobre todos os bytes após a quinta linha. A assinatura Ed25519 cobre
`format=capyos-modules-index-v2|abi_token=capyos-base-v3|epoch=1|body_sha256=<H>\n`.
O resolver do CapyAgent escolhe a versão SemVer mais nova compatível e
known-good antes da assinatura; o CapyOS apenas verifica e aplica esse plano.

## 5. Regras globais de valor

- **Printable ASCII obrigatório:** qualquer byte fora de 0x20-0x7E em
  qualquer valor causa `CAPYPKG_ERR_DENIED`. Isso fecha a vetor de
  ANSI escape injection via `vga_write` → COM1. Não use caracteres
  acentuados, tabs, CR/LF no meio do valor, nem qualquer escape.
- **HTTPS obrigatório:** `payload_url` deve começar com `https://`.
  HTTP plain é rejeitado pelo adapter.
- **Sem traversal:** `install_root` não pode conter `..` em nenhum
  segmento; o adapter recusa por directory boundary
  (`/var/capypkg` vs `/var/capypkgsneak` não passa).

## 6. Descriptor canônico por pacote (repos customizados)

A assinatura Ed25519 cobre **exatamente** este byte string sem espaços
adicionais, com `|` literais e `\n` final:

```text
name=<N>|version=<V>|payload_sha256=<H>|payload_url=<U>\n
```

Onde os quatro valores vêm verbatim do manifest. Diferenças
documentadas:

- separador entre campos: `|` literal (não caracteres de espaço);
- ordem fixa: `name` → `version` → `payload_sha256` → `payload_url`;
- terminação: um único `\n` (LF; não CRLF).

A chave pública do publisher oficial está pinada no CapyOS. Repositórios
customizados assinados usam o descritor por pacote abaixo; o índice oficial v2
usa o envelope da seção 4 e falha fechado com `CAPYPKG_ERR_INDEX_TRUST`.

### Exemplo (pseudo-código)

```text
descriptor =
    "name=org.capyos.codecs.image-basic|"
    "version=0.0.1|"
    "payload_sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef|"
    "payload_url=https://example.org/capycodecs-image-basic-v0.0.1.bin\n"

signature  = ed25519_sign(secret_key, descriptor)
manifest   = "...
signature_ed25519=" + hex_lower(signature) + "
..."
```

## 7. Onde publicar

| Recurso | Endpoint | Conteúdo |
|---|---|---|
| Índice do repositório | `index_url` registrado via `pkg-source-add` ou `bootstrap_repo_url` em `/system/install/profile.ini` | `modules-index.txt` com descritores separados por `---\n` no formato acima |
| Payload binário | `payload_url` declarado por cada entrada | bytes opacos; tamanho ≤ `CAPYPKG_PAYLOAD_MAX` (8 MiB) |

O adapter persiste o cache em `/system/capypkg/cache/index.txt` e o
DB de instalados em `/system/capypkg/db.idx` usando o mesmo formato.
Arquivos `.manifest` individuais são entradas de publisher/CI; publique
o índice agregado como asset operacional para o wizard e para `pkg-fetch`.
Os arquivos automáticos `Source code .zip` e `.tar.gz` do GitHub não são
índices nem payloads capypkg.

## 8. Tamanho máximo de payload

`CAPYPKG_PAYLOAD_MAX` é 8 MiB. O runtime dimensiona a alocação pelo tamanho
autenticado e recusa excesso antes da ativação.

## 9. Workflow recomendado para publishers

1. **Build do artefato** — produzir o `.bin` (sem nenhuma execução
   esperada pelo adapter; o conteúdo é opaco até a etapa de loader
   sandboxed abrir).
2. **Calcular SHA-256** — `sha256sum payload.bin | awk '{print $1}'`.
3. **Anotar `payload_size`** — `wc -c < payload.bin`.
4. **Compor o descritor canônico** — concatenar `name=...|version=...|payload_sha256=...|payload_url=...\n`.
5. **Assinar Ed25519** — com a chave privada do publisher; converter
   assinatura para 128 hex lowercase.
6. **Compor o manifest** — produzir as linhas `key=value` com todos os
   campos obrigatórios + `signature_ed25519` + opcionais aplicáveis.
7. **Publicar** — `index.txt` no `index_url` e `payload.bin` no
   `payload_url`, ambos via HTTPS com certificado válido contra os
   trust anchors em `CapyOS/src/security/tls_trust_anchors.c`.
8. **Atualizar** [`compatibility-matrix.md`](compatibility-matrix.md)
   com a nova versão, ABI e canal.

## 10. Validação antes de publicar

Sem rodar comandos nesta máquina (review/edit only):

- conferir que o `payload_url` resolve por HTTPS com cert válido;
- conferir que `payload_sha256` bate com o artefato real;
- conferir que `signature_ed25519` foi gerada sobre o descritor canônico
  exato (ordem e separadores);
- conferir que `install_root` está sob `/var/capypkg` ou `/opt/`;
- conferir que `name` não aparece em outro repo configurado com
  versão conflitante;
- conferir que `depends` lista pacotes presentes no índice (o adapter
  resolve recursivamente até budget de 8).

Validação rodada em outra máquina (recomendada):

```bash
make test-capypkg                         # parser + verifier host-side
make smoke-x64-vmware-pkg-install         # quando Etapa 9 abrir
```

## 11. Mapeamento para o descritor JSON de alto nível do CapyAgent

`CapyAgent/docs/component-index-example.md` descreve um **registro
org-style** (`id`, `tag`, `artifact`, `sha256`, `activation_class`,
`required_abis`, `permissions`). Esse é um modelo de descoberta de
componente que ainda não está conectado ao adapter in-tree.

A correspondência desejada quando CapyAgent integrar:

| Campo CapyAgent (JSON) | Campo manifest line-oriented | Observação |
|---|---|---|
| `id` | `name` | use IDs org-style se aplicável; respeite o alfabeto `[a-zA-Z0-9._-]` |
| `tag` | `version` | sem o prefixo `v` para `version`; mantenha `v...` no `tag` quando publicar no GitHub |
| `artifact` + URL base | `payload_url` | concatene base + artifact para HTTPS absoluto |
| `sha256` | `payload_sha256` | mesmo valor em lowercase hex |
| `permissions` | não tem correspondente direto | adapter alpha ignora; usar para UI/UX futura |
| `dependencies` | `depends` | mesma semântica; ≤ 8 itens |
| `required_abis` | `provides_abi`, `abi_version`, `core_abi_min`, `core_abi_max` | resolver publica apenas candidatos compatíveis |

CapyAgent deve emitir tanto o registro JSON (alto nível, para
descoberta UI) quanto um manifest line-oriented por release (baixo
nível, para o adapter consumir). Documente o algoritmo de tradução
no próprio CapyAgent quando o gerador for implementado.

## 12. Referência cruzada

- [`compatibility-matrix.md`](compatibility-matrix.md)
- [`compatibility-audit-2026-05-19.md`](compatibility-audit-2026-05-19.md)
- [`modular-installation-architecture.md`](modular-installation-architecture.md)
- [`package-format-integration-contract.md`](package-format-integration-contract.md)
- [`../../architecture/capypkg-adapter.md`](../../architecture/capypkg-adapter.md)
- [`../../operations/manual-module-deploy-runbook.md`](../../operations/manual-module-deploy-runbook.md)
