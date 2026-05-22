# Manual module deploy runbook

**Status:** autoritativo desde 2026-05-19 (atualizado em alpha.241
com wizard interativo e comando `capy`).
**Audiência:** operador que vai instalar o CapyOS core e, em seguida,
módulos remotos via os repositórios externos (`CapyAgent`,
`CapyBrowser`, `CapyCodecs`, `CapyUI`, `CapyLang`, `CapyBenchmark`).
**Plataforma alvo:** `VMware + UEFI + E1000` (trilha oficial).
**Etapa atual:** Etapa 4 (CapyDisplay 2D + scheduler); Etapas 8-9 (installer/package) ainda
bloqueadas — este runbook usa o adapter `services/capypkg`
**entrega antecipatória da Etapa 9**, em modo recebedor fail-closed,
estendida com:

- `/system/install/profile.ini` (BASIC | FULL | CUSTOM)
- comando `pkg-bootstrap [--force]` no capysh
- auto-bootstrap pelo `kernel_service_poll_capypkg` no primeiro boot
- aggregator `tools/scripts/build_modules_index.py` + `make modules-index`
- target `make package` em cada um dos 6 repos externos

A tela interativa de profile no first-boot wizard (escopo Etapa 8) **não**
foi adicionada nesta entrega; a escolha de profile é feita editando
`profile.ini` antes do primeiro boot ou via `pkg-bootstrap` manual.

Antes de seguir este runbook, leia:

- [`../reference/integration/compatibility-matrix.md`](../reference/integration/compatibility-matrix.md)
- [`../reference/integration/capypkg-publisher-manifest-format.md`](../reference/integration/capypkg-publisher-manifest-format.md)
- [`../reference/integration/compatibility-audit-2026-05-19.md`](../reference/integration/compatibility-audit-2026-05-19.md)
- [`../architecture/capypkg-adapter.md`](../architecture/capypkg-adapter.md)

## 0. Pré-requisitos

- VM VMware com firmware UEFI e NIC E1000.
- Acesso a repositório HTTPS com certificado válido contra os trust
  anchors em `src/security/tls_trust_anchors.c`.
- Para repositórios `signed`: verificador Ed25519 do `CapyAgent`
  publicado e plugado via `capypkg_set_signature_verifier`.
- Para repositórios `unsigned`: apenas SHA-256 protege contra
  corrupção; documentado abaixo como modo alpha de laboratório.

## 1. Build do core (em máquina de build, fora desta máquina)

```bash
python3 tools/scripts/check_deps.py --allow-fallback-toolchain
make test                     # host-side aggregate (inclui install_profile)
make test-capypkg             # iteração focada do adapter
make layout-audit
make version-audit
make all64                    # kernel x86_64 (inclui install_profile.o e capypkg_bootstrap.o)
make iso-uefi
make verify-release-checksums TOOLCHAIN64=host
```

Aceitação: todos os comandos saem com código 0; o ISO oficial é
gerado em `build/iso-uefi/`.

### 1.1 Empacotamento dos módulos (em cada repo externo)

Em cada repositório externo a ser disponibilizado para o ISO, na
máquina de build:

```bash
cd ../CapyAgent      && make package
cd ../CapyBrowser    && make package
cd ../CapyCodecs     && make package
cd ../CapyUI         && make package
cd ../CapyLang       && make package
cd ../CapyBenchmark  && make package
```

Cada `make package` produz, dentro do próprio repo:

- `build/capypkg/<canonical-name>-<version>.bin` — payload opaco (tar determinístico do `src/` + `docs/` + `VERSION`);
- `build/capypkg/<canonical-name>.manifest` — entrada line-oriented `key=value` com `name`, `version`, `summary`, `payload_url`, `payload_sha256`, `payload_size`, `install_root`, `depends` e o separador `---`.

CapyLang usa `target/capypkg/` em vez de `build/capypkg/` porque
cargo dono de `build/`. O aggregator detecta os dois caminhos.

Override do `payload_url` (default aponta para o GitHub Release):

```bash
make package PUBLISH_URL_BASE=https://meu.servidor.tld/capypkg
```

### 1.2 Agregar e publicar o índice (de volta no CapyOS)

```bash
cd ../CapyOS
make modules-index
# saída: build/capypkg/modules-index.txt
```

O aggregator (`tools/scripts/build_modules_index.py`) busca cada
`build/capypkg/*.manifest` nos repos irmãos, valida campo a campo
(alfabeto do `name`, HTTPS no `payload_url`, hex no `payload_sha256`,
`install_root` dentro de `/var/capypkg` ou `/opt/`, etc.) e concatena
com separadores `---` no arquivo único.

Publicar então:

1. Cada `<repo>/build/capypkg/<name>.bin` no canal `latest` do
   GitHub Release do repo publicador. O `payload_url` gerado pelo
   Makefile do CapyUI já aponta para esse canal estável:
   `https://github.com/<owner>/<repo>/releases/latest/download/<name>.bin`.
2. `modules-index.txt` no mesmo canal `latest`:
   `https://github.com/<owner>/<repo>/releases/latest/download/modules-index.txt`.

O canal `latest` é mantido por `.github/workflows/release-artifacts.yml`
em cada repo publicador: todo push em `main` reconstroi os pacotes, move
a tag `latest` para o novo commit e republica a Release `latest` com
`make_latest: true`. O CapyOS não precisa saber qual semver é o atual;
o redirect `/releases/latest/download/` sempre serve o último artefato
validado.

A URL do índice será referenciada em `profile.ini`. Não use a URL de
um `.manifest` individual como `bootstrap_repo_url`: o adapter in-tree
baixa um índice agregado com um ou mais descritores separados por `---`.
Os assets automáticos do GitHub (`Source code .zip` / `.tar.gz`) também
não são consumidos pelo capypkg.

Smokes opcionais antes de promover a build:

```bash
make smoke-x64-iso TOOLCHAIN64=host
make boot-perf-baseline-selftest
make smoke-marker-policy-selftest
```

## 2. Instalação do core (na VM oficial)

1. Boot da ISO UEFI em VMware.
2. Loader UEFI (`BOOTX64.EFI`) carrega o kernel.
3. Instalação para disco GPT é executada pelo fluxo oficial.
4. Reboot para o disco provisionado.
5. Login na sessão gráfica com o usuário criado.

Aceitação:

- desktop CapyUI aparece com taskbar, ícones e cursor;
- `capysh` está acessível pelo terminal;
- a sessão gráfica emite `[smoke] gui-session ready` e
  `[smoke] mouse-events ready` no klog;
- `/var/capypkg/`, `/system/capypkg/repos.cfg` e
  `/system/capypkg/db.idx` existem (mesmo que vazios).

## 3. Validação do adapter antes de qualquer instalação remota

No `capysh` da VM oficial:

```text
pkg-source-list
```

Saída esperada:

```text
- stable (https://updates.capyos.org/index.txt) [signed, pinned]
```

```text
pkg-list --installed
```

Saída esperada: lista vazia.

```text
pkg-list --available
```

Saída esperada: vazia até `pkg-fetch` rodar.

Se algum desses comandos retornar `CAPYPKG_ERR_NOT_READY` ou
`CAPYPKG_ERR_NO_SOURCE`, o adapter não foi inicializado pelo binder
do kernel. Conferir `kernel_capypkg_bind_runtime_adapters` em
`src/arch/x86_64/kernel_services.c`.

## 4. Caminho A — repositório `signed` oficial (recomendado)

**Requer:** `CapyAgent` com signer Ed25519 publicado e adapter
externo que chama `capypkg_set_signature_verifier` em boot.

**Estado atual:** signer Ed25519 não publicado pelo `CapyAgent`;
adapter externo não plugado. Repos `signed` permanecem fail-closed.

Quando o `CapyAgent` publicar:

```text
pkg-fetch
pkg-list --available
pkg-info <nome>
pkg-install <nome>
```

Aceitação:

- klog mostra `[audit] [capypkg] payload-sha256 verified; package installed`;
- arquivo aparece em `/var/capypkg/<nome>/<nome>.bin`;
- `pkg-list --installed` mostra o pacote.

## 4.A. Caminho oficial recomendado: wizard interativo do primeiro boot (alpha.241+)

**Este é o fluxo padrão desde alpha.241**: o installer UEFI ficou minimalista
(só disco + chave de recovery + confirmação) e TODA configuração de usuário
+ idioma + teclado + tema + seleção de módulos acontece no primeiro boot do
sistema instalado, via TUI no framebuffer:

1. ISO faz install do core (passo 2) e reboota.
2. Primeiro boot: wizard interativo abre automaticamente.
   - Etapas do wizard: idioma, layout teclado, hostname, tema, splash,
     usuário admin, senha admin, **seleção de módulos** (BASIC | FULL | CUSTOM).
   - Se profile != BASIC: wizard pergunta URL do índice (Enter usa o
     `modules-index.txt` servido pelo canal rolante `latest` do CapyUI
     em `https://github.com/<owner>/CapyUI/releases/latest/download/`),
     grava `/system/install/profile.ini` e dispara
     `capypkg_bootstrap_run_with_progress` que mostra
     `[modules] [i/N] instalando org.capyos.ui.desktop-session...` na tela.
3. Após o sweep terminar com sucesso, o wizard chama `acpi_reboot` para
   ativar os módulos recém-instalados.
4. Segundo boot: gate de ativação encontra `org.capyos.ui.desktop-session/installed`,
   `desktop_runtime_start` dispara, usuário vê o desktop.

Sem necessidade de editar `profile.ini` manualmente ou rodar `pkg-*`.

Para re-rodar tudo depois (ex: trocar profile, instalar módulo novo):

```text
capy wizard           # re-executa o assistente completo
capy wizard --modules # re-executa SÓ a etapa de módulos
capy install <nome>   # instala um módulo específico
capy module list      # lista instalados
capy update           # refresh do índice
```

## 4.B. Caminho automatizado por install profile pré-existente (avançado)

Use quando você precisa **bypassar o wizard interativo** (por exemplo:
provisionamento de fleet via imagem dourada com `profile.ini`
pré-gravado no `/system/install/` antes do primeiro boot).

Se `/system/install/profile.ini` já existe ao boot, o wizard ainda
roda (não pula etapas do user), mas o auto-bootstrap em
`kernel_service_poll_capypkg` também pode disparar 60s depois sem
ação no terminal:

1. ISO instala o core (passo 2).
2. Primeiro boot até a sessão gráfica.
3. `kernel_service_poll_capypkg` (intervalo 60 s) detecta o adapter
   pronto e chama `capypkg_bootstrap_run(0, ...)`.
4. O bootstrap registra o repo declarado em `profile.ini`, faz
   `pkg-fetch` e instala cada pacote do catálogo (ou apenas os de
   `bootstrap_install` quando `profile=custom`).
5. Marker `/system/install/bootstrap.done` é gravado para que o
   próximo poll seja no-op.

Comandos para inspecionar:

```text
pkg-list --installed       # mostra o que entrou via bootstrap
pkg-source-list            # mostra o repo configurado por profile.ini
```

Para re-executar manualmente (por exemplo após editar o profile):

```text
pkg-bootstrap --force
```

Falhas transitórias (rede ainda em DHCP, HTTPS não respondendo)
**não** gravam o marker — o próximo poll (60 s depois) tenta de novo.
Falhas permanentes (profile.ini malformado, repo retornando 404)
gravam o marker para evitar retry infinito; o operador re-roda
`pkg-bootstrap --force` após corrigir.

Log auditável:

```
[audit] [capypkg] bootstrap idle (basic profile or marker present)
[audit] [capypkg] bootstrap reached install sweep
[audit] [capypkg] bootstrap: index fetch failed (will retry)
[audit] [capypkg] bootstrap: profile.ini rejected
[audit] [capypkg] payload-sha256 verified; package installed
```

## 5. Caminho B — repositório `unsigned` (laboratório alpha)

**Use somente para teste e em ambiente isolado.** Sem assinatura, a
única defesa contra repo-side swap é o SHA-256 declarado no manifest.

### 5.1 Publicar o manifest e o payload

Siga o doc [`../reference/integration/capypkg-publisher-manifest-format.md`](../reference/integration/capypkg-publisher-manifest-format.md):

1. Build do `payload.bin` (qualquer conteúdo opaco; o adapter não
   executa os bytes).
2. `sha256sum payload.bin` → guardar o hex lowercase.
3. `wc -c < payload.bin` → guardar o tamanho.
4. Compor `manifest.txt` line-oriented:

```text
name=lab.testpkg
version=0.0.1
summary=Test package for manual deploy runbook
payload_url=https://<repo-https>/lab.testpkg-v0.0.1.bin
payload_sha256=<64 hex>
payload_size=<bytes>
install_root=/var/capypkg/lab.testpkg
```

5. Publicar `manifest.txt` em `https://<repo-https>/index.txt` e
   `payload.bin` em `https://<repo-https>/lab.testpkg-v0.0.1.bin`.
   Confirmar que ambos respondem 200 com cert válido.

### 5.2 Adicionar o repositório no CapyOS

No `capysh`:

```text
pkg-source-add testing https://<repo-https>/index.txt --unsigned
```

Aceitação:

- `pkg-source-list` mostra `testing` como `unsigned`;
- nenhum erro `CAPYPKG_ERR_INVALID_ARG` ou `CAPYPKG_ERR_DENIED`
  (HTTPS sem ANSI escapes no name e na URL).

### 5.3 Fetch e install

```text
pkg-fetch
pkg-list --available
pkg-info lab.testpkg
pkg-install lab.testpkg
```

Aceitação:

- klog mostra `[audit] [capypkg] payload-sha256 verified; package installed`;
- `pkg-list --installed` inclui `lab.testpkg`;
- arquivo aparece em `/var/capypkg/lab.testpkg/lab.testpkg.bin`.

### 5.4 Equivalente via install profile (auto-bootstrap)

Em vez dos passos 5.2 e 5.3, antes de gravar a ISO ou no primeiro
boot via recovery shell, edite `/system/install/profile.ini`
seguindo `system/install/profile.ini.sample`:

```ini
profile=full
bootstrap_repo_name=modules
bootstrap_repo_url=https://<repo-https>/modules-index.txt
bootstrap_repo_signed=0
```

Ou, para instalar apenas um subconjunto (validação seletiva):

```ini
profile=custom
bootstrap_repo_name=modules
bootstrap_repo_url=https://<repo-https>/modules-index.txt
bootstrap_repo_signed=0
bootstrap_install=org.capyos.codecs.image-basic,org.capyos.ui.widget-core
```

No primeiro boot, o auto-bootstrap (`kernel_service_poll_capypkg`)
detecta o profile e roda o equivalente a `pkg-bootstrap`. O marker
`/system/install/bootstrap.done` é gravado quando o sweep termina.

Para iterar localmente: `pkg-bootstrap --force` re-aplica o profile
sem precisar reinstalar a ISO.

### 5.5 Falhas esperadas

- payload alterado depois do `payload_sha256` declarado →
  `[audit] [capypkg] payload-sha256 mismatch; install aborted`;
- `payload_url` não-HTTPS → `CAPYPKG_ERR_DENIED` no parse;
- manifest com byte 0x1B (ANSI escape) em qualquer campo →
  `CAPYPKG_ERR_DENIED`;
- `install_root` fora de `/var/capypkg` ou `/opt/` →
  `CAPYPKG_ERR_DENIED`;
- repositório `signed` sem verifier plugado →
  `CAPYPKG_ERR_SIGNATURE`.

## 6. Remoção e atualização

```text
pkg-update lab.testpkg
pkg-update                          # atualiza todos
pkg-remove lab.testpkg
```

Aceitação:

- `pkg-update` emite `[audit] [capypkg] payload-sha256 verified; package installed`
  ao re-instalar;
- `pkg-remove` emite `[audit] [capypkg] package removed`;
- arquivo é removido de `/var/capypkg/<nome>/`;
- `db.idx` atualizado.

## 7. Verificação de estado

```text
pkg-stats
```

Mostra `installed_count`, `available_count`, `updates_pending`,
`repo_count`, `any_repo_signed`, `catalog_fresh`, `initialized`.

```text
pkg-source-remove testing
```

Aceitação: repositório removido se não-pinned;
`CAPYPKG_ERR_DENIED` se `pinned=1` (caso do `stable`).

## 8. Pontos de falha conhecidos

| Sintoma | Causa provável | Resolução |
|---|---|---|
| `pkg-fetch` retorna `NO_SOURCE` | nenhum repo configurado | `pkg-source-add` antes |
| `pkg-fetch` retorna `FETCH` | HTTPS falhou no transporte ou cert inválido | verificar conectividade e trust anchor |
| `pkg-install` retorna `SIGNATURE` em repo signed | verifier não plugado | aguardar adapter externo do `CapyAgent` ou usar repo `--unsigned` em lab |
| `pkg-install` retorna `DIGEST` | payload no servidor mudou desde o `pkg-fetch` | re-publicar manifest com novo SHA-256 ou re-fetch |
| `pkg-install` retorna `DEPENDENCY` | dependência ausente no catálogo | publicar a dep no mesmo índice antes |
| `pkg-install` retorna `QUOTA` | catálogo cheio ou payload > 1 MiB | reduzir payload (alpha limit) ou esperar streaming writer |
| `pkg-install` retorna `STORAGE` | gravação no VFS falhou | verificar montagem do volume cifrado |
| `pkg-install` retorna `DENIED` em parse | byte não-printable, URL não-HTTPS, alfabeto inválido em `name`/`depends`, `..` em `install_root` ou `install_root` fora de `/var/capypkg`/`/opt/` | corrigir manifest no servidor |
| `pkg-install` retorna `PARSE` | hex inválido em `payload_sha256` ou `signature_ed25519`, `payload_size` com overflow, campo obrigatório vazio | corrigir manifest no servidor |

## 9. Auditoria

Todas as mutações são registradas como
`[audit] [capypkg] <mensagem>` no klog ring com nível `INFO` para
sucesso e `WARN` para cada branch de falha. A reconstrução forense é
explicitamente possível por mensagem distinta:

- INFO: `payload-sha256 verified; package installed`;
- INFO: `package removed`;
- INFO: `repository added`, `repository updated`, `repository removed`;
- WARN: `dependency missing or cycle`, `dependency install failed`,
  `payload fetch failed`, `payload-sha256 mismatch`,
  `signature verification failed`, `payload write failed`,
  `installed-table quota exhausted`, `package installed but db persistence failed`,
  `payload removal failed; db entry still dropped`,
  `package removed but db persistence failed`,
  `repository added but db persistence failed`,
  `repository updated but db persistence failed`,
  `repository removed but db persistence failed`.

## 10. Critério de aceitação do deploy manual

Considere o deploy manual completo quando:

- core boota até a sessão gráfica em VMware UEFI E1000;
- adapter `capypkg` reporta `initialized=1` em `pkg-stats`;
- pelo menos um pacote de laboratório instala via `pkg-install` com
  `payload-sha256 verified; package installed` no klog;
- pacote aparece em `/var/capypkg/<nome>/<nome>.bin`;
- remoção funciona e arquivo desaparece;
- nenhuma instalação `signed` em produção até o verifier Ed25519
  externo ser plugado;
- a matriz [`../reference/integration/compatibility-matrix.md`](../reference/integration/compatibility-matrix.md)
  foi conferida antes de cada combinação de versão.

## 11. Próximos passos para a Etapa 9 oficial

Quando a Etapa 9 abrir (após Etapas 3-8 fecharem):

```bash
make smoke-x64-vmware-pkg-install
make release-check
```

E publicar o smoke `pkg-install` como gate obrigatório no
[`release-ci-smoke-readiness.md`](release-ci-smoke-readiness.md).

## 12. Limites do runbook atual

- Sem app gráfico Software Center; toda a operação é via `capysh`.
- Sem streaming download; payloads acima de 1 MiB falham mesmo com
  `payload_size` válido.
- Sem ativação automática de binários; `/var/capypkg/<nome>/<nome>.bin`
  é só staging.
- Sem sandbox de execução; loader sandboxed entra em etapa futura.
- Sem rollback transacional além do DB save/restore.

## 13. Referência cruzada

- [`../architecture/capypkg-adapter.md`](../architecture/capypkg-adapter.md)
- [`../reference/integration/compatibility-matrix.md`](../reference/integration/compatibility-matrix.md)
- [`../reference/integration/capypkg-publisher-manifest-format.md`](../reference/integration/capypkg-publisher-manifest-format.md)
- [`../reference/integration/compatibility-audit-2026-05-19.md`](../reference/integration/compatibility-audit-2026-05-19.md)
- [`../reference/integration/modular-installation-architecture.md`](../reference/integration/modular-installation-architecture.md)
- [`../reference/integration/package-format-integration-contract.md`](../reference/integration/package-format-integration-contract.md)
- [`../reference/integration/external-core-repositories.md`](../reference/integration/external-core-repositories.md)
- [`etapa-2-external-validation-playbook.md`](etapa-2-external-validation-playbook.md)
- [`update-from-github.md`](update-from-github.md)
