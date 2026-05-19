# Capy package/manifest — contrato de integração com CapyOS

**Status:** Etapa 9 alpha — adaptador in-tree ativo.
**Integração ativa:** `services/capypkg` recebe pacotes Capy remotos
emitidos pelo `CapyAgent`.
**Repositório externo atual:** `CapyAgent`.

## Estado atual

O package manager legado in-tree foi removido por completo. O CapyOS
expõe agora um pequeno adaptador in-tree, versionado e fail-closed,
sob `services/capypkg`. `CapyAgent` continua dono de:

- formato `.capypkg` e ferramentas de assinatura;
- modelos host-testáveis de pacote, resolver e índice de componentes;
- plano declarativo de instalação/remoção/rollback;
- assinatura Ed25519 do descritor canônico.

CapyOS base permanece dono de:

- fronteira HTTPS (`net/http.h` via BearSSL);
- verificação SHA-256 do payload em runtime;
- verificação da assinatura sobre o descritor canônico do pacote
  (binding name + version + payload_sha256 + payload_url);
- staging do payload em `/var/capypkg/<name>/` no VFS encriptado;
- catálogo persistente em `/system/capypkg/db.idx`;
- supervisão pelo `service_manager` via `SYSTEM_SERVICE_CAPYPKG`.

O `update_agent` continua independente e cobre release/update do
sistema base (boot slots, manifesto raiz, rollback).

A fronteira de instalação modular para todos os projetos apartados
fica em `modular-installation-architecture.md`.

## API runtime in-tree

A API pública está em `include/services/capypkg.h`:

- `capypkg_init` / `capypkg_reset`;
- `capypkg_repo_add` / `capypkg_repo_remove` / `capypkg_repo_list`;
- `capypkg_fetch_index`;
- `capypkg_install` / `capypkg_remove` / `capypkg_update` /
  `capypkg_update_all`;
- `capypkg_installed_get*` / `capypkg_available_get*`;
- `capypkg_stats_get`, `capypkg_state_label`, `capypkg_result_label`.

Hooks de adapter para tests/integração externa:

- `capypkg_set_reader` / `capypkg_set_writer` /
  `capypkg_set_bytes_writer` / `capypkg_set_remover` /
  `capypkg_set_mkdir` para o VFS;
- `capypkg_set_text_fetcher` / `capypkg_set_bytes_fetcher` para o
  transporte HTTPS;
- `capypkg_set_signature_verifier` para o verificador Ed25519 que
  `CapyAgent` deve plugar (até lá o adaptador rejeita repos com
  `require_signature` ativo).

## CLI in-tree (capysh)

| Comando | Função |
|---|---|
| `pkg-list [--installed|--available]` | lista catálogo e instalados |
| `pkg-info <nome>` | metadados do pacote |
| `pkg-fetch` | sincroniza o índice de repositórios |
| `pkg-install <nome>` | instala (verifica SHA-256 e assinatura) |
| `pkg-remove <nome>` | remove instalação e cache local |
| `pkg-update [<nome>]` | atualiza um pacote ou todos |
| `pkg-source-list` | lista repositórios configurados |
| `pkg-source-add <nome> <https-url> [--unsigned]` | adiciona repositório |
| `pkg-source-remove <nome>` | remove repositório (pinned é protegido) |

## Manifest v1 — descritor recebido pelo adaptador

O adaptador in-tree consome um descritor line-oriented `key=value`
para mirror do formato do `update_agent`. Múltiplos pacotes em um
índice são separados por `---` em linha própria. Campos obrigatórios:

- `name`
- `version`
- `payload_url` (HTTPS apenas)
- `payload_sha256` (64 hex)

Campos opcionais:

- `summary`
- `payload_size` (decimal bytes, ≤ `CAPYPKG_PAYLOAD_MAX`)
- `signature_ed25519` (128 hex — obrigatório se o repo de origem
  exigir assinatura)
- `install_root` (precisa ficar sob `/var/capypkg` ou `/opt/`)
- `depends` (lista separada por vírgula)
- `repo`

`CapyAgent` é dono do formato `.capypkg` completo (arquivos internos,
permissões, hooks). O descritor acima é apenas o subconjunto que o
adaptador in-tree precisa para baixar, verificar e indexar.

## Segurança

- HTTPS obrigatório para `payload_url` e `index_url`;
- SHA-256 obrigatório por payload, verificado pelo adaptador antes
  do staging;
- Assinatura Ed25519 obrigatória quando o repositório a exigir; a
  assinatura é sobre o descritor canônico
  `name=N|version=V|payload_sha256=H|payload_url=U\n`;
- Sem verificador Ed25519 plugado o adaptador rejeita instalações de
  repos `signed` com `CAPYPKG_ERR_SIGNATURE` (fail-closed);
- Pacotes nunca executam código a partir do adaptador; o staging
  apenas grava bytes verificados em `/var/capypkg/<name>/`;
- Apenas `install_root` sob `/var/capypkg` ou `/opt/` é aceito;
- Limites de quota (`CAPYPKG_PAYLOAD_MAX`, `CAPYPKG_MAX_INSTALLED`,
  `CAPYPKG_MAX_AVAILABLE`, `CAPYPKG_MAX_REPOS`) protegem o adaptador.

## Testes apartados obrigatórios

- parse de manifest válido (formato line-oriented + separador `---`);
- rejeição de manifest truncado, malformado, sem assinatura quando
  exigida, ou com URL não-HTTPS;
- comparação de versões;
- resolução de dependências simples e ciclo limitado;
- geração de descritor canônico determinístico;
- fixtures de pacote com hashes conhecidos.

## Gate de integração CapyOS

Etapa 9 alpha — execução externa recomendada:

- `make smoke-x64-vmware-pkg-install` (cobertura runtime do adaptador);
- `make release-check` quando o `CapyAgent` plugar o verificador
  Ed25519 e o fluxo de assinatura for promovido em conjunto.
