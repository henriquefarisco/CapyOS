# CapyOS 0.8.0-alpha.239+20260519

`0.8.0-alpha.239+20260519` consolida o adapter `services/capypkg` (entrega
antecipatoria da Etapa 9) e propaga o gate de hardening contra terminal
injection para os outros sites onde input externo entra no sistema.

## Entrega antecipatoria - adapter capypkg

Recepcao de pacotes Capy remotos in-tree, fail-closed, sem executar codigo
do payload. O fluxo continua bloqueado oficialmente ate a Etapa 9 abrir,
mas a fronteira esta verificada e auditavel.

- `src/services/capypkg/capypkg_state.c`, `capypkg_manifest.c`,
  `capypkg_repo.c`, `capypkg_install.c` (4 TUs runtime <900 LOC).
- `include/services/capypkg.h` documenta o contrato externo: SHA-256
  obrigatorio, signature Ed25519 sobre descritor canonico
  `name=<N>|version=<V>|payload_sha256=<H>|payload_url=<U>\n`, alfabeto
  restrito de `name`, escopo de `install_root` (`/var/capypkg` ou
  `/opt/`), fail-closed sem verifier plugado.
- Kernel binding em `src/arch/x86_64/kernel_services.c`
  (`kernel_capypkg_bind_runtime_adapters`).
- Servico supervisionado `SYSTEM_SERVICE_CAPYPKG` registrado em
  `service_manager` e incluido no target `FULL`.
- 9 comandos `capysh` tri-lingua em
  `src/shell/commands/system_control/capypkg_commands.c`:
  `pkg-list`, `pkg-info`, `pkg-fetch`, `pkg-install`, `pkg-remove`,
  `pkg-update`, `pkg-source-list`, `pkg-source-add`,
  `pkg-source-remove`.
- 28 testes host-side em `tests/services/test_capypkg.c`, rodaveis via
  `make test-capypkg` (foco) ou `make test` (agregado).
- Doc canonico em `docs/architecture/capypkg-adapter.md` consolida design,
  fronteiras de seguranca, formato de manifest, persistencia, seams
  pluggaveis, audit trail e roadmap futuro.

## Bugs fechados via code review (16 no capypkg)

1. **`any_repo_signed` stale** em transicao signed -> unsigned no update
   path de `capypkg_repo_add`.
2. **Falha de bytes_writer** silenciosa - agora emite WARN
   `payload write failed; install aborted`.
3. **Quota exhaustion** silenciosa - agora WARN
   `installed-table quota exhausted; install aborted` + estado BROKEN.
4. **db_save falhando emite success klog** - agora WARN distinto
   `package installed but db persistence failed`; INFO so quando
   persistencia confirmada.
5. **Remover falhando swallowed** - agora WARN
   `payload removal failed; db entry still dropped`.
6. **Mensagem de dependencia imprecisa** - agora distingue
   `dependency missing or cycle` de `dependency install failed`.
7. **`install_root` prefix bypass** (`/var/capypkgsneak` aceito) -
   agora exige boundary `'\0'` ou `'/'` apos prefix.
8. **`install_root` aceitava segmentos `..`** - agora bloqueado por
   `path_has_dotdot_segment`.
9. **`name` aceitava qualquer char** - agora restrito a
   `[a-zA-Z0-9._-]` e dot-only names sao rejeitados.
10. **`parse_uint32` overflow silencioso** poderia bypassar
    `CAPYPKG_PAYLOAD_MAX` - agora detecta antes do wrap.
11. **`capypkg_remove` emite "removed" mesmo com `db_save` falhando** -
    agora WARN `package removed but db persistence failed`.
12. **`repo_add` update sem audit** - agora INFO/WARN com variantes
    `repository added`, `repository updated`, `repository removed` e
    `... but db persistence failed`.
13. **Header publico documentava contrato incorreto** de signature -
    agora especifica byte-a-byte o descritor canonico e a condicao
    `require_signature` real.
14. **`depends` aceitava qualquer char** - agora valida com a mesma
    regra do `name`.
15. **Parser nao respeitava skip-malformed-entry** - agora avanca ate
    o proximo `---\n` em vez de halt, fechando DoS surface para
    remote indexes.
16. **Terminal escape injection** via `summary`/`version`/etc -
    `apply_kv` rejeita qualquer byte fora `[0x20, 0x7E]`. Mesma
    protecao em `capypkg_repo_parse` (load de `repos.cfg`) e em
    `capypkg_repo_add` (API/CLI).

## Hardening cross-module (3 bugs adicionais)

O mesmo gate de printable-ASCII / control-byte rejection foi propagado
para fechar a contraparte do Bug 16 nos outros sites onde input externo
entra no sistema via `shell_print` -> `vga_write` -> COM1.

17. **`update_agent_parse.c::parse_buffer_line`** - manifests,
    `state.ini` e `repository.ini` agora descartam silenciosamente
    qualquer linha cujo value carregue control bytes. `cmd_update_status`
    nao pode mais ecoar ANSI escapes plantados em `version`, `branch`,
    `source`, `payload_url`, `published_at`, `summary`, etc.
18. **`http_parse_url`** - URL parser rejeitava qualquer byte exceto
    `/` e `:` no host component, permitindo CRLF injection
    (`https://x\r\nGET /evil ...`) via `cmd_net_query` (que passava
    `argv[1]` direto ao `http_get`). Fecha HTTP request smuggling.
19. **`http_store_headers`** - response headers de servidores hostis
    (`Content-Type`, `Location`, etc.) podiam carregar ANSI escapes
    ecoados por `cmd_net_query`. Bytes nao-printaveis em nome ou valor
    sao agora substituidos por `?` em parse time, sem afetar
    Content-Length / chunked / Content-Encoding (que key by prefix
    tolerante a junk).

## Documentacao consolidada

- `docs/architecture/capypkg-adapter.md` (novo): design completo,
  fronteiras de seguranca, vocabulario audit klog (16 mensagens
  distintas), test discipline (28 cases listados).
- `docs/plans/STATUS.md` registra a entrega antecipatoria e o hardening
  cross-module com inventario das 4 trilhas de input externo
  protegidas.
- `docs/plans/active/capyos-master-plan.md` documenta o adapter dentro
  da secao da Etapa 9 (status: bloqueada por Etapas 3-8; fronteira de
  recepcao verificada).
- `docs/reference/cli-reference.md` lista os 9 comandos `pkg-*`.
- `docs/reference/integration/external-core-repositories.md`,
  `core-migration-quarantine.md` e
  `package-format-integration-contract.md` atualizados para refletir
  que a quarentena legada foi resolvida e o capypkg in-tree e o
  receptor oficial.
- `include/services/capypkg.h` (header publico) documenta o contrato
  externo que `CapyAgent` precisa cumprir para produzir manifests e
  assinaturas compativeis.

## NAO altera

- A Etapa 3 (USB HID + xHCI + storage estavel) continua sendo a
  oficialmente em andamento; este checkpoint NAO fecha nem libera
  outra Etapa.
- A Etapa 9 continua **bloqueada** ate Etapas 3-8 fecharem. O capypkg
  alpha apenas garante que a fronteira de recepcao estara estavel
  quando a Etapa 9 abrir.
- O verifier Ed25519 do `CapyAgent` ainda nao esta plugado. Repos
  `signed` (incluindo o seeded `stable`) continuam fail-closed
  conforme o contrato em `include/services/capypkg.h`.
- ABI publica preservada: todos os simbolos novos sao aditivos. O
  contrato `update_agent` nao foi alterado (so o gate de input do
  parser).

## Validacao esperada

- `make test`
- `make test-capypkg`
- `make layout-audit`
- `make all64`

## Inventario de arquivos tocados nesta release

### Adapter capypkg (novo)

- `include/services/capypkg.h`
- `src/services/capypkg/internal/capypkg_internal.h`
- `src/services/capypkg/capypkg_state.c`
- `src/services/capypkg/capypkg_manifest.c`
- `src/services/capypkg/capypkg_repo.c`
- `src/services/capypkg/capypkg_install.c`
- `src/shell/commands/system_control/capypkg_commands.c`
- `tests/services/test_capypkg.c`
- `docs/architecture/capypkg-adapter.md`

### Wiring no kernel

- `Makefile` (CAPYOS64_OBJS, SYSTEM_CONTROL_OBJS, TEST_SRCS,
  `make test-capypkg`).
- `include/services/service_manager.h`
  (`SYSTEM_SERVICE_CAPYPKG`).
- `src/services/service_manager.c` (seed do servico e inclusao no
  target `FULL`).
- `src/arch/x86_64/kernel_services.c` (adapter VFS+HTTPS).
- `src/arch/x86_64/kernel_main.c` (registro poll/control).
- `include/arch/x86_64/kernel_main_internal.h` (declaracoes do
  service hook).
- `src/shell/commands/system_control/power_runtime_registry.c`
  (registro dos 9 comandos `pkg-*`).
- `src/shell/commands/system_control/internal/system_control_internal.h`
  (declaracoes dos comandos).
- `tests/test_runner.c` (declaracao e chamada de
  `run_capypkg_tests`).

### Hardening cross-module

- `src/services/update_agent_parse.c` (`parse_buffer_line` +
  `update_value_is_printable_ascii`).
- `src/net/services/http/url_request_builder.c` (`http_parse_url`).
- `src/net/services/http/prelude_headers_encoding.c`
  (`http_store_headers`).

### Documentacao

- `docs/plans/STATUS.md`.
- `docs/plans/active/capyos-master-plan.md`.
- `docs/architecture/source-layout.md`
  (legacy quarantine resolvida).
- `docs/architecture/decoupled-development-contracts.md`.
- `docs/architecture/graphical-session-operational.md`
  (refs `installer_main.c` removidas).
- `docs/reference/integration/external-core-repositories.md`.
- `docs/reference/integration/core-migration-quarantine.md`.
- `docs/reference/integration/package-format-integration-contract.md`.
- `docs/reference/integration/README.md`.
- `docs/reference/cli-reference.md` (9 entradas `pkg-*`).
- `docs/README.md` (index).
- `README.md` (highlights).
- `src/config/first_boot/program.c`
  (`/docs/capyos-cli-reference.txt`).
- `src/arch/x86_64/kernel_shell_runtime.c` (early CLI doc).
