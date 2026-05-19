# CapyOS — Status executivo

**Data:** 2026-05-18 · **Versão:** `0.8.0-alpha.237+20260514` · **Plataforma:** VMware + UEFI + E1000 · **Público alvo:** usuário desktop comum

> **Fonte de verdade:** [`active/capyos-master-plan.md`](active/capyos-master-plan.md).
> **Implementação finalizada:**
> [`historical/implementation-delivered-through-alpha93.md`](historical/implementation-delivered-through-alpha93.md).
> **Snapshot da sequência antiga (pré-reordenação ROI):**
> [`historical/capyos-master-plan-pre-roi-reorder.md`](historical/capyos-master-plan-pre-roi-reorder.md).
> Este documento mostra apenas o plano ativo sequencial. Itens concluídos foram
> removidos daqui e preservados nos documentos históricos.
>
> **Reorganização 2026-05-15:** Etapas 3-15 foram reordenadas por ROI ao usuário desktop comum e expandidas para 14 etapas (3-16) sem violar a regra sequencial estrita. Etapas 1-2 não foram afetadas.

---

## Progresso global

- **Base histórica:** 100% consolidada até `alpha.93`; Etapa 1 fechada em `alpha.100`.
- **Plano sequencial novo (pós-reordenação ROI):** Etapas 1-2 oficialmente fechadas; 2/16 etapas concluídas.
- **Etapa atual:** Etapa 3 — Driver framework + entrada USB HID + storage estável.
- **Etapa 2:** aceita operacionalmente após execução externa dos gates oficiais em VMware + UEFI + E1000 informada pelo operador em 2026-05-18.
- **Próximo bloco da Etapa 3:** validação externa do Slice 3D — USB HID keyboard/mouse real em VMware, após 3D entregar SET_CONFIGURATION, HID boot protocol, Configure Endpoint e polling interrupt por código/testes host-side.
- **Etapas bloqueadas:** Etapas 4-16 dependem do fechamento integral da etapa anterior.
- **Projetos desacoplados:** CapyLang, browser core, package format, widget model,
  codecs e benchmark harness podem evoluir fora do repositório, mas só contam
  como progresso oficial quando integrados por contrato versionado e gate
  externo na etapa correspondente.
- **Migração externa inicial:** snapshots seguros foram registrados em
  `docs/reference/integration/external-core-repositories.md`. A higiene total
  do core foi concluída (`docs/reference/integration/core-migration-quarantine.md`):
  os fontes e headers legados sem callers ativos foram **removidos do tree**
  e o flag `CAPYOS_ENABLE_LEGACY_MIGRATED` foi aposentado. O adaptador
  in-tree `services/capypkg` recebe pacotes Capy remotos via `capysh`.
- **Distribuição modular alpha:** o fluxo inicial usa tags de release GitHub,
  sha256 e índice de ABI mínima conforme
  `docs/reference/integration/tag-release-component-index.md`; assinatura e
  certificados ficam diferidos para hardening antes de qualquer release oficial.
- **Entrega antecipatória da Etapa 9 (capypkg adapter alpha):** infra de
  recepção de pacotes Capy publicada in-tree como `services/capypkg`
  (4 TUs runtime + 1 header público + 1 header interno, todas < 900 LOC),
  com 9 comandos CLI tri-língua (`pkg-list`, `pkg-info`, `pkg-fetch`,
  `pkg-install`, `pkg-remove`, `pkg-update`, `pkg-source-list`,
  `pkg-source-add`, `pkg-source-remove`), supervisor de serviço
  `SYSTEM_SERVICE_CAPYPKG` integrado ao target `FULL`, 28 testes
  host-side passando (`make test-capypkg` para iteração focada,
  incluindo regressões para prefix-bypass de install_root, segmentos
  `..`, alfabeto restrito do `name` e dos `depends`, overflow de
  `payload_size`, reset de `any_repo_signed` em transição
  signed→unsigned, skip de entry malformada em manifests
  multi-entry, rejeição de ANSI escape em fields de manifest e em
  `pkg-source-add`, e cobertura direta dos klog audit messages) e
  trilha auditável via klog (`[audit] [capypkg] …`) em todas as mutações
  de pacote/repo, com variantes WARN distintas para falhas de
  digest/signature/dependency/fetch/write/quota/persistence
  (forensicamente reconstruíveis). Política de segurança
  documentada em `docs/architecture/capypkg-adapter.md`: HTTPS-only no
  transporte, SHA-256 obrigatório, signature gate fail-closed (Ed25519
  só é aceito quando `CapyAgent` plugar o verificador externamente),
  escopo de filesystem restrito a `/var/capypkg` ou `/opt/`, e zero
  execução de payload pelo adapter. **Não fecha a Etapa 9:** o gate
  oficial continua bloqueado por Etapas 3-8 conforme tabela vigente
  abaixo; este entregável apenas garante que, quando a Etapa 9 abrir,
  a fronteira de recepção já estará verificada e estável.
- **Hardening cross-module:** o mesmo gate de printable-ASCII
  aplicado ao parser do `capypkg` foi propagado a outros dois
  módulos críticos que também ecoam dados externos via
  `shell_print` → `vga_write` → COM1, fechando a contraparte do
  Bug 16 do capypkg em todas as trilhas atuais de input externo:
  - **`src/services/update_agent_parse.c::parse_buffer_line`** —
    manifests, `state.ini` e `repository.ini` que carreguem control
    bytes em qualquer value agora são silenciosamente descartados
    na ingestão, antes de o `cmd_update_status` ecoar os campos
    (`version`, `branch`, `source`, `payload_url`, `published_at`,
    `summary`, etc.) ao usuário. Sem alterar contrato externo
    (linhas malformadas eram já previamente ignoradas pelo
    dispatcher).
  - **`src/net/services/http/url_request_builder.c::http_parse_url`**
    — fechado vetor de **HTTP request smuggling**: o parser de URL
    rejeitava qualquer byte exceto `/` e `:` no host component, o
    que permitiria CRLF injection (`https://x\r\nGET /evil ...`)
    via `cmd_net_query` (que passa `argv[1]` direto ao
    `http_get`). Agora rejeita 0x00-0x20 e 0x7F antes de qualquer
    parsing.
  - **`src/net/services/http/prelude_headers_encoding.c::http_store_headers`**
    — response headers de servidores hostis (`Content-Type`,
    `Location`, etc.) podiam carregar ANSI escapes ecoados por
    `cmd_net_query` para o usuário. Bytes não-printáveis em nome
    ou valor de header são agora substituídos por `?` em parse
    time, sem afetar Content-Length / chunked / Content-Encoding
    (que key by prefix tolerante a junk).

## Visão executiva da Etapa 2

`alpha.237` entregou o gate externo final `mouse-events` e completou o escopo
técnico da Etapa 2. Em 2026-05-18, o operador informou que os gates oficiais
foram executados fora desta máquina na plataforma oficial e passaram.

**Runbook único para o operador externo / CI privada:**
[`docs/operations/etapa-2-external-validation-playbook.md`](../operations/etapa-2-external-validation-playbook.md)
orquestra build gates (Fase A) + provisionamento e handoff (Fase B) +
smoke real `mouse-events` (Fase C) + evidência/aceitação (Fase D) +
promoção pública final (Fase E), com pass/fail explícito por gate e
referência aos 5 documentos autoritativos.

| Bloco fechado | Peso | Slice alvo | Evidência de aceite |
|---|---:|---|---|
| Validação externa final | aceite operacional | `alpha.237` | Operador informou execução bem-sucedida de `make test`, `make layout-audit`, `make all64`, `make release-check`, `make smoke-x64-vmware-mouse-events` e gates de readiness/evidência/aceitação/promoção em VMware + UEFI + E1000. |

Critério de aceite operacional cumprido: a Etapa 2 libera a Etapa 3 após
`make test`, `make layout-audit`, `make all64`, `make release-check` e o smoke
`mouse-events` oficial terem sido executados fora desta máquina com evidência
informada pelo operador. Novas regressões futuras entram como bugs da etapa
ativa correspondente, salvo mudança explícita deste plano.

## Sequência ativa

> **Histórico do bloqueio atualizado em `alpha.223`:** o caminho de volumes cifrados avançou
> de header-managed em produção (`alpha.222`) para preflight seguro/read-only de
> re-key/migração legacy. O bloqueio até `alpha.232` era o motor transacional que deve
> deslocar CAPYFS de LBA0 para LBA1, recriptografar sob chaves bound ao header,
> atualizar geometria quando necessário e abortar/rollback sem corromper dados.
> Loginwindow GUI final e smokes reais `gui-session`/`mouse-events` foram
> entregues por contrato até `alpha.237`; os gates externos foram informados
> como aprovados pelo operador em 2026-05-18.
>
> **Avanço em `alpha.224`:** o motor ainda não aplica writes, mas agora existe
> planner transacional read-only que bloqueia shrink/scratch ausente e só marca
> uma migração como `READY` quando há bloco scratch após o range alvo, cópia
> reversa segura e ranges source/target determinísticos.
>
> **Avanço em `alpha.225`:** o executor transacional agora existe como contrato
> guardado/dry-run: valida o plano, reporta fases de checkpoint/cópia
> reversa/commit/verify e recusa writes reais ou flags desconhecidas com
> `WRITES_DISABLED` até existir executor write-enabled com checkpoint
> persistente auditado.
>
> **Avanço em `alpha.226`:** o checkpoint persistente da migração agora tem
> contrato próprio: record little-endian de 128 bytes, CRC32 contra corrupção
> acidental, reserved-zero e validação semântica de progresso para
> resume/rollback/abort do executor write-enabled futuro.
>
> **Avanço em `alpha.227`:** a primeira escrita guardada do executor agora grava
> somente o checkpoint no bloco scratch com flag explícita, verifica por
> read/parse e ainda bloqueia cópia/recriptografia destrutiva e commit de header.
>
> **Avanço em `alpha.228`:** o executor agora prepara no scratch a identidade
> criptográfica do destino: checkpoint + header Argon2id com salt CSPRNG +
> manifest com CRCs, verificados por read-back antes de qualquer cópia
> destrutiva ou commit de LBA0.
>
> **Avanço em `alpha.229`:** o executor agora aplica um passo real de
> copy/re-encrypt reverso: copia exatamente um bloco legacy para o domínio
> header-managed Argon2id, verifica plaintext no destino e atualiza
> checkpoint+manifest no scratch. Commit de LBA0 e abertura final verificada
> entraram em `alpha.230`; rollback/abort e limpeza entram em `alpha.231`.
>
> **Avanço em `alpha.230`:** o executor agora comita LBA0 por último:
> exige cópia completa, grava o header staged, verifica read-back, abre pelo
> caminho header-managed e valida o superbloco CAPYFS antes de marcar o
> checkpoint como `COMPLETED`. Rollback/abort operacional e limpeza do scratch
> entram em `alpha.231`.
>
> **Avanço em `alpha.231`:** o executor agora tem recovery operacional:
> rollback/abort antes do commit restaura um bloco por chamada e zera o scratch
> ao completar; cleanup pós-commit abre header-managed, valida CAPYFS, localiza
> o scratch por geometria, rejeita estado estranho e zera/verifica o bloco.
>
> **Avanço em `alpha.232`:** a migração cifrada agora tem orquestrador automático
> de passo único: detecta legacy/header-managed, lê scratch checkpoint+manifest,
> delega stage/copy/commit/cleanup para o próximo passo seguro e executa rollback
> quando `ORCHESTRATE_ABORT` é solicitado antes do commit.
>
> **Avanço em `alpha.233`:** o loginwindow ganhou submit/autenticação real como
> contrato seguro: a política explícita habilita submit apenas em runtime pronto,
> a ponte `login_window_credential_auth_submit_userdb_consume()` chama
> `userdb_authenticate_with_policy`, preserva `FAILED` versus `LOCKED`, zera o
> buffer de credencial e mantém fallback textual autoritativo.
>
> **Avanço em `alpha.234`:** a recuperação segura ganhou contrato final de
> decisão redigida: `login_window_credential_recovery_decision_build()` consolida
> controller GUI + submit autenticado, aceita apenas rotas
> stay/recovery/resume/text-login seguras, bloqueia bypass de lockout, bloqueia
> recovery após autenticação e exige reset+rerender no resume.
>
> **Avanço em `alpha.235`:** o handoff loginwindow -> sessão gráfica ganhou
> contrato redigido: `login_window_credential_session_handoff_build()` só libera
> `session_begin`, ativação de sessão, init do shell context e desktop autostart
> após submit autenticado, recovery decision segura e usuário desktop elegível;
> falha, lockout, recovery ativo, usuário de recuperação ou diagnóstico sem
> redaction caem para fallback seguro.
>
> **Avanço em `alpha.236`:** o gate externo `gui-session` agora tem contrato
> determinístico: `desktop_gui_session_smoke_gate_from_readiness()` separa
> prontidão de sessão gráfica de `mouse-events`, o runtime emite uma única vez
> `[smoke] gui-session ready`, `smoke-x64-vmware-gui-session` exige DHCP + esse
> marker e a esteira de handoff/readiness/evidência/aceitação/promoção usa o
> novo gate oficial.
>
> **Avanço em `alpha.237`:** o gate externo final `mouse-events` agora tem
> contrato determinístico: `desktop_mouse_events_smoke_gate_from_readiness()`
> exige `gui-session`, mouse, cursor e rotas de mouse prontas, o runtime emite
> uma única vez `[smoke] mouse-events ready`, `smoke-x64-vmware-mouse-events`
> exige DHCP + `gui-session` + `mouse-events`, e a esteira de release usa esse
> gate como contrato oficial final.
>
> **Aceite externo:** execução fora desta máquina dos gates finais informada
> como concluída com sucesso pelo operador em 2026-05-18; Etapa 3 liberada.

> **Nota sobre a tabela abaixo:** após a reordenação por ROI em 2026-05-15, a numeração das Etapas 3-16 mudou. A linha longa da Etapa 2 preserva o histórico técnico acumulado; o status executivo autoritativo atual é Etapa 2 concluída e Etapa 3 em andamento. A sequência antiga está em [`historical/capyos-master-plan-pre-roi-reorder.md`](historical/capyos-master-plan-pre-roi-reorder.md).

Resumo executivo vigente:

| Etapa | Tema | Status | Bloqueio |
|---|---|---|---|
| 1 | CapyUI Shell Polish v1 | Concluída | fechada em alpha.100 |
| 2 | Sessão gráfica operacional | Concluída | gates externos aprovados em 2026-05-18 |
| 3 | Driver framework + entrada USB HID + storage estável | Em andamento | etapa atual |
| 4 | CapyDisplay 2D + scheduler/multithread runtime | Bloqueada | depende da Etapa 3; define contrato widget/display-list |
| 5 | TLS userland real | Bloqueada | depende da Etapa 4 |
| 6 | Apps básicos do desktop maduros | Bloqueada | depende da Etapa 5; inclui `CapyBrowse Text` para sites de texto e diagnóstico de rede |
| 7 | Browser usável com web estática moderna | Bloqueada | depende da Etapa 6 |
| 8 | Release/update gate oficial + instalador polido | Bloqueada | depende da Etapa 7 |
| 9 | Package manager + SDK + ABI estável | Bloqueada | depende da Etapa 8; integra package format desacoplado |
| 10 | Áudio + multimídia básica | Bloqueada | depende da Etapa 9; codecs entram por contrato |
| 11 | WiFi + power management + suspend/resume | Bloqueada | depende da Etapa 10 |
| 12 | JS engine sandboxed | Bloqueada | depende da Etapa 11; engine sem syscalls diretas |
| 13 | CapyLX L0-L5 unificado | Bloqueada | depende da Etapa 12; base futura para ports Linux de browsers grandes |
| 14 | Wayland bridge + apps Linux GUI | Bloqueada | depende da Etapa 13 |
| 15 | Mesa/Vulkan path + CapyLang | Bloqueada | depende da Etapa 14; inclui benchmarks/demo `Snake` e `Asteroids` em CapyLang |
| 16 | Plataforma 1.0 hardening | Bloqueada | depende da Etapa 15; inclui hardening do navegador, baseline regressivo dos benchmarks CapyLang e compatibilidade oficial Hyper-V planejada |

Registro histórico acumulado preservado abaixo:

| Etapa | Tema | Status | Bloqueio |
|---|---|---|---|
| 1 | CapyUI Shell Polish v1 | Concluída | fechada em alpha.100 |
| 2 | Sessão gráfica operacional | Em andamento | Header-managed encrypted volumes em producao em alpha.222 conecta a primitiva on-disk de alpha.221 ao installer + boot path via novo modulo `volume_provider` (`include/security/volume_provider.h` + `src/security/volume_provider.c` ~145 + ~280 LOC) + 9 funcoes de teste host-side com ram-backed block device (~430 LOC). `volume_provider_install` (write-side, called pelo `initialize_encrypted_data_volume` no primeiro boot pos-install) gera salt 16-byte CSPRNG per-install, popula header v1 com Argon2id (t_cost=3, m_cost=8192 KiB), deriva keys via `crypt_derive_xts_keys_argon2id`, computa kdf_check_tag HMAC-SHA256 + finaliza CRC32, escreve buffer 4 KiB no LBA 0 do chunked device, cria offset wrapper a partir do LBA 1, `crypt_init` retorna crypt_dev pronto para mount. `volume_provider_open` (read-side, called pelo `open_crypt_volume_with_password` em boots subsequentes) le LBA 0, executa `capyos_volume_header_looks_valid` como quick gate, se passa entra no header path AUTORITARIO sem fallback legacy em falha de auth (downgrade protection), se nao passa entra no legacy PBKDF2 + g_disk_salt + crypt_init sobre full device preservando 100% dos volumes pre-alpha.222. Modificacoes minimas em 3 arquivos kernel-side (`key_storage_probe.c::open_crypt_volume_with_password` substitui crypt_derive_xts_keys + crypt_init direto por volume_provider_open; `mount_initialize.c::initialize_encrypted_data_volume` substitui open_crypt_volume_with_password por volume_provider_install no fresh install; `Makefile` adiciona volume_provider.o em CAPYOS64_OBJS). Tests 9 funcoes ~50 assertions cobrem install grava magic CAPYVHDR + padding zero + parse field-by-field, install+open round-trip plaintext via AES-XTS, wrong password falha clean com out_crypt nullified, legacy volume mount sem header via PBKDF2 fallback round-trips, downgrade attack rejeitado (header presente forca header path), fail-closed em NULL/block_size errado/device tiny, I/O failure no LBA 0 refusa mount, dois installs produzem salts distintos. Caminho legacy preservado integralmente — nenhum volume CapyOS existente quebra. **`alpha.221`** entregou: On-disk volume header module — primitiva canonica para AES-XTS volume keys com algorithm marker (PBKDF2 ou Argon2id), per-volume random salt, HMAC-SHA256 check tag e CRC32 fast bit-rot gate (`include/security/volume_header.h` + `src/security/volume_header.c` ~290 + ~620 LOC). Struct on-disk fixa de 512 bytes (magic 'CAPYVHDR', version, kdf_algo_id, t_cost, m_cost, salt_len, kdf_salt[64], data_offset_lba>=1, reserved_lba_count, kdf_check_tag=HMAC-SHA256(K1||K2, context || prefix[0..104]), creation_timestamp_ns, creator_version, reserved, header_crc32 IEEE 802.3 reflected) com vh_serialize_prefix como autoridade unica de layout (impede drift entre disco e HMAC input), endianness little-endian explicito via byte stores, CRC32 no-table branchless, vh_validate_params strict (PBKDF2 t>=1000 e m==0; Argon2id t>=1 e m>=8 RFC 9106 §3.1; salt em [8,64]; reserved<=data_offset). Parse fail-safe wipe-first + CRC->magic->version->flags->params->reserved-all-zero. Threat model two-tier documentado inline: header_crc32 e bit-rot gate fast (NAO seguranca, atacante recomputa trivialmente); kdf_check_tag e binding criptografico (atacante que altera salt/algo/t/m forca user a derivar chave diferente, HMAC nao bate, mount recusa). derive_keys dispatcher fail-closed first (wipe key1/key2 antes de check) + dispatch PBKDF2/Argon2id por algo_id + verify_check_tag + wipe em falha. NAO distingue 'wrong password' de 'tampered header' no retorno publico (ambos ERR_CHECK_TAG) para nao gerar oracle de tampering. Tests host-side em tests/test_volume_header.c: 13 funcoes ~70 assertions cobrindo CRC32 KAT, init happy paths PBKDF2 e Argon2id, init fail-closed 13 vetores, serialize/parse roundtrip com endianness explicit (bytes 0..7 -> 'CAPYVHDR'), parse fail-closed magic/version/CRC/algo/flags/reserved tampered, looks_valid quick gate, derive success em ambos KDFs com k1!=k2 anti-split, wrong password com sentinela 0xA5 wipe, tampered salt rejeitado com password correto, algo downgrade attempt rejeitado, fail-closed NULL inputs. Wiring: Makefile CAPYOS64_OBJS + TEST_SRCS; test_runner.c. NAO altera installer/boot path (alpha.222 fara o write-side wiring; legacy volumes pre-alpha.222 continuam funcionando porque nada le o header ainda). Mapa de entrega linear documentado: alpha.221 primitiva entregue (DONE), alpha.222 wiring installer + boot, alpha.223 ferramenta de re-keying in-place. Composicao com alpha.220 (crypt_derive_xts_keys_argon2id backend), alpha.218 (argon2id_hash), alpha.214 (CSPRNG futuro), alpha.209 (sha256_clear hygiene). Implicit re-hash on successful auth em alpha.220 fecha o timing leak transicional de alpha.219 (PBKDF2 ~50ms vs Argon2id ~200ms); src/auth/user.c refatorado — userdb_replace_password_hash extraido como helper privado (read-modify-write do /etc/users.db com salt fresco csprng_get_bytes(16) + Argon2id derivation via user_password_hash_derive USER_ARGON2ID_T_COST=3/M_COST=8192 KiB), userdb_set_password vira wrapper fino que aplica auth_policy_validate_password e delega, userdb_authenticate depois de auth_ok=1 com rec.algo_id != USER_PASSWORD_ALGO_ARGON2ID chama (void)userdb_replace_password_hash(username, password) — fail-silent (allocation/FS error nao bloqueia auth ja bem-sucedida; record stays on PBKDF2 e retry no proximo login). Threat model self-heals: population de PBKDF2 records shrinks monotonically toward zero conforme contas autenticam; residual leak restrito a "contas que nunca logaram desde alpha.220 deployment". Timing primary path inalterado (~200ms Argon2id); contas legacy pagam ~250ms apenas no primeiro login pos-upgrade (50ms verify + 200ms rehash + ~5ms FS write) e convergem para ~200ms nos subsequentes. Argon2id volume-key primitive paralelo: include/security/crypt.h adiciona crypt_derive_xts_keys_argon2id(password, salt, salt_len, t_cost, m_cost, key1[32], key2[32]) + CRYPT_VOLUME_ARGON2ID_T_COST=3 / CRYPT_VOLUME_ARGON2ID_M_COST=8192 (reaproveita budget de 8 MiB do kernel heap; mesma tuning que userdb). src/security/crypt.c:174-253 implementa caller-allocates via kalloc(m_cost*1024) -> argon2id_hash com out_len=64 -> split key1[0..32]+key2[32..64], wipe volatile-safe do work memory antes de kfree, wipe scratch derived[64], fail-closed first (key1/key2 zerados no inicio antes de parameter checks — sentinela "no key here" inequivoca para caller que esqueca de checar return; rejeita t_cost<1, m_cost<8, salt_len<8 per RFC 9106 §3.1). Callers em producao (installer_main.c:464, key_storage_probe.c:75, kernel.c:392) ficam em PBKDF2 ate o slice futuro de header de volume com algorithm marker landar — primitiva entregue mas nao consumida nesta release. Testes novos: test_crypt_derive_xts_keys_argon2id em tests/test_crypt_vectors.c com 11 assertions (determinismo, key1!=key2 split, salt sensitivity, fail-closed em NULL password/salt/key1/key2/t_cost=0/m_cost=7/salt_len=7, wipe forensics sentinela 0xA5 -> 0x00 em failure path, non-collision com PBKDF2 4 iter). ABI publica preservada (nenhuma quebra; struct user_record format integral; userdb_authenticate/userdb_set_password signatures identicas; crypt_derive_xts_keys signature identical). Composicao com TODOS os slices anteriores integral: alpha.219 (mesmo dispatcher user_password_hash_derive), alpha.218 (argon2id_hash direto), alpha.214 (CSPRNG), alpha.212 (timing-equalised lockout — wrapper userdb_authenticate_with_policy preserva ordem), alpha.207, alpha.206 (k_userdb_dummy_salt preservado). Impacto user-final: contas legadas migram para memory-hard hashing transparentemente sem acao do usuario/admin; ataque offline contra /etc/users.db agora paga 8 MiB por candidate (speedup GPU/ASIC sobre CPU cai de >10000x para <10x per RFC 9106 §1.2). Impacto estrutural: codigo do userdb mais limpo (set_password e wrapper de replace_password_hash + policy check); primitive de volume key Argon2id pronta para installer/volume-creation tools futuros sem rebuild do kernel; alpha.218 e alpha.219 Limites residuais ambos endereçados. Novos limites: implicit re-hash exige login (contas dormentes ficam PBKDF2 indefinidamente — slice futuro tera ferramenta admin de batch re-hash); volume key primitive sem callers em producao (slice futuro definira header de volume); disk salt continua hardcoded em installer_main.c:39-41 (slice futuro per-install random salt). Antes em alpha.219: Argon2id (RFC 9106) em producao no userdb — primeira caller real da primitiva memory-hard de alpha.218; dispatcher novo em src/auth/user_password_hash.{c,h} (~190 LOC + 105 LOC) decoupla user.c das primitivas crypto e isola Argon2id work memory + wipe hygiene em modulo testavel host-side; APIs publicas user_password_hash_derive/verify/algo_to_string/algo_from_string; user_record_init e userdb_set_password sempre emitem Argon2id (USER_ARGON2ID_T_COST=3, USER_ARGON2ID_M_COST=8192 KiB) — toda conta criada ou que troca senha no alpha.219+ ja sai memory-hard, sem migracao de DB necessaria (primeira troca de senha promove conta legacy); userdb_authenticate dispatcha conforme rec.algo_id (PBKDF2 legacy continua aceito ~50ms; Argon2id record ~200ms); usuario desconhecido roda Argon2id com k_userdb_dummy_salt + USER_ARGON2ID_T_COST/M_COST para equalizacao com baseline nova (preserva mitigacao user enumeration timing de alpha.206); schema /etc/users.db: 7 campos (PBKDF2 legacy) ou 10 campos com trailer :argon2id:t_cost:m_cost — parser aceita ambos, serializer escolhe por algo_id (downgrade transparente para binarios pre-alpha.219 sem migracao reversa); Argon2id work memory 8 MiB alocado via kalloc, wipeado volatile-safe antes de kfree; fail-closed em alocacao/parametros/NULL — hash_out wipeado a zero em todos os caminhos de erro; struct user_record cresce 9 bytes append-only (algo_id+t_cost+m_cost no FINAL) — 27 callsites existentes compilam unchanged; buffer da linha aumentado de +64 para +128 bytes para trailer Argon2id; tests host-side novos em tests/test_user_password_hash.c (run_user_password_hash_tests): 30 assertions em 6 functions cobrindo algo string canonical (8), PBKDF2 legacy roundtrip com t_cost=0 mapping para USER_ITERATIONS (4), Argon2id roundtrip + nao-colisao com PBKDF2 (5), sensibilidade parameter threading a salt/t_cost/m_cost anti-regressao (3), fail-closed derive NULL password/salt/out/t=0/m<8/algo desconhecido (7), fail-closed verify NULL stored/zero-len/oversize (3); leak transicional PBKDF2 vs Argon2id vazia apenas idade aproximada da ultima troca de senha — sera eliminado por slice futuro de implicit re-hash on successful auth; volume key derivation (crypt_derive_xts_keys para AES-XTS) continua PBKDF2-SHA256 (threat model diferente, migracao futura); composicao com alpha.218/214/212/211/207/206/208/209 preservada integral; PBKDF2-SHA256 continua disponivel em crypt.c. Em alpha.218: Argon2id (RFC 9106) + BLAKE2b (RFC 7693) memory-hard password hashing em alpha.218 — fundacao cripto CapyOS completa (11 primitivas) fechando o gap de defesa contra brute-force GPU/ASIC que afeta PBKDF2-SHA256; src/security/argon2.c (~600 LOC) + src/security/blake2b.c (~270 LOC) auditavel independente da PHC reference; APIs publicas blake2b_init/update/final/wipe + one-shot keyed mode HMAC-like; argon2id_hash com caller-provided memory buffer (sem malloc kernel), parallelism=1 fixo, sem secret/AD; pre-hash H0 + variable-length H' + G compression com fBlaMka mul 32x32->64 (anti-ASIC cost-per-op) + address block gen data-indep para slice 0-1 pass 0 + index alpha com J1^2 mapping nao-uniforme; wipe volatile-safe em todos os intermediarios; tests RFC 7693 Appendix A vector + streaming vs oneshot + keyed mode + variable output + Argon2id smoke (determinismo, sensibilidade password/salt/t/m/out_len, fail-closed em todos os parametros); PBKDF2-SHA256 preservado para backward compat. Em alpha.217: Ed25519 (RFC 8032) implementacao REAL em src/security/ed25519.c substituindo esqueleto fail-closed desde alpha.210; update verifier (src/services/update_agent.c) oficialmente OPERACIONAL pela primeira vez — manifests assinados com chave canonica em producao agora aceitos quando criptograficamente validos; field arithmetic GF(2^255-19) refatorado em modulo compartilhado include/security/fe25519.h + src/security/fe25519.c (extraido de x25519.c, +fe_pow22523 para sqrt em decode + fe_neg + fe_cmov + fe_isnegative + fe_iszero + fe_notequal), agora consumido por X25519 + Ed25519; APIs publicas ed25519_create_keypair (seed -> SHA-512 -> clamp scalar + scalar mult base point) + ed25519_sign (RFC §5.1.6 deterministico) + ed25519_verify (RFC §5.1.7 com cofator 8); group ops twisted Edwards extended (ge_dbl dbl-2008-hwcd, ge_add add-2008-hwcd-3, ge_neg_p, ge_cmov constant-time); scalar mult double-and-add constant-time 256 doubles + cmov; encoding/decoding compressed com canonicality check + x==0 reject + sqrt candidate via fe_pow22523 + ED_SQRTM1; scalar arithmetic mod L via sc_reduce64 (porte ref10 signed 21-bit limbs) + sc_muladd + sc_is_canonical; constants ED_D/ED_D2/ED_SQRTM1/ED_B_X/ED_B_Y/ED_B_T verificadas vs dalek-cryptography; wipe volatile-safe em todos os fe/ge/scalars/SHA-512 contexts intermediarios; threat model documentado (cofator 8 anti-torsion, S<L anti-malleability); tests reformulados com 3 vetores oficiais RFC 8032 §7.1 + tampering rejection + wrong-pk + non-canonical S + NULL + round-trip + determinism + tamper-message; fundacao cripto canonica CapyOS completa (10 primitivas modernas em src/security/, todas auditaveis). Em alpha.216: X25519 ECDH canonico (Montgomery ladder constant-time, small-subgroup gate, scalar clamping). Em alpha.215: ChaCha20-Poly1305 AEAD. Em alpha.214: CSPRNG hardening profundo. Em alpha.213: HKDF-SHA256 |
| 3 | Driver framework + entrada USB HID + storage estável | Em andamento | etapa atual |
| 4 | CapyDisplay 2D + scheduler/multithread runtime | Bloqueada | depende da Etapa 3 |
| 5 | TLS userland real | Bloqueada | depende da Etapa 4 |
| 6 | Apps básicos do desktop maduros | Bloqueada | depende da Etapa 5 |
| 7 | Browser usável com web moderna | Bloqueada | depende da Etapa 6 |
| 8 | Release/update gate oficial + instalador polido | Bloqueada | depende da Etapa 7 |
| 9 | Package manager + SDK + ABI estável | Bloqueada | depende da Etapa 8 |
| 10 | Áudio + multimídia básica | Bloqueada | depende da Etapa 9 |
| 11 | WiFi + power management + suspend/resume | Bloqueada | depende da Etapa 10 |
| 12 | JS engine sandboxed | Bloqueada | depende da Etapa 11 |
| 13 | CapyLX L0-L5 unificado | Bloqueada | depende da Etapa 12 |
| 14 | Wayland bridge + apps Linux GUI | Bloqueada | depende da Etapa 13 |
| 15 | Mesa/Vulkan path + CapyLang | Bloqueada | depende da Etapa 14 |
| 16 | Plataforma 1.0 hardening | Bloqueada | depende da Etapa 15 |

---

## Etapa 1 fechada

### CapyUI Shell Polish v1

Objetivo: melhorar layout, UI e UX de forma fácil e incremental, aproximando o
visual de uma mistura entre Ubuntu e Windows 7 sem exigir GPU 3D.

Entregáveis planejados:

- [x] Tema `classic-modern` com paleta Ubuntu/Windows 7-like integrada ao
  compositor, Settings, `config-theme` e fallback framebuffer.
- [x] Taskbar inferior com botão Capy, realce visual de foco e relógio em pill.
- [x] Toasts/notificações simples alinhadas à paleta ativa.
- [x] Apps fixados/recentes e lista de apps abertos com foco/restauração.
- [x] Launcher com busca textual, categorias visuais e ações de sessão básicas.
- [x] Decoração de janelas com polish final de estados ativo/inativo,
  minimizar, maximizar e fechar.
- [x] Wallpaper padrão e desktop com grid de ícones refinado.
- [x] System tray inicial para rede e usuário.
- [x] Tray avançado com som/estado adicionais.

Critério para liberar a Etapa 2:

- [x] A Etapa 1 fechou código, documentação, validação estática e
  release note/versionamento do incremento final.

---

## Etapa fechada

### Etapa 2 — Sessão gráfica operacional

Status atual: concluída em `alpha.237`; gates oficiais externos informados como aprovados pelo operador em 2026-05-18. Etapa 3 liberada.

Registro histórico preservado:

Status: em andamento em `alpha.206` com hardening real de autenticação por senha (constant-time compare em `userdb_authenticate`, wipe de locais sensíveis e log de bootstrap sem salt/hash em hex) sobre a base alpha.205 (window input plan seguro de credenciais do loginwindow) sem expor segredo, armar teclado real, submeter teclado, armar pointer real, submeter pointer, armar foco real, submeter foco, carregar keymap real, submeter keymap, decodificar input real, rotear input real, armar callback de input, submeter callback de input, permitir grab real, submeter grab, armar handler real, armar fila real, despachar evento real, armar callback real, submeter callback, capturar timestamp real, submeter timestamp, completar frame real, submeter frame, armar evento de vblank real, armar callback de vblank real, submeter callback de vblank, capturar timestamp real, submeter timestamp, completar frame real, submeter frame, anexar buffer real, armar vblank real, armar evento real, submeter flip async, anexar estado real, armar atomic commit real, armar callback de frame real, submeter callback real, mapear buffer real, copiar pixels reais, armar DMA real, submeter blit real, anexar conector real, armar modo real, armar sinal real, submeter output real, submeter sinal real, anexar controlador real, submeter display real, executar output real, submeter pipeline real, anexar buffer real, submeter scanout real, executar display flip real, aguardar vsync real, armar fence real, submeter vsync real, submeter wait real, agendar frame real, armar timer real, acordar compositor, executar page flip, submeter schedule real, apresentar frame real, submeter present real, enviar damage real, submeter compositor real, submeter surface real, vincular surface real, mapear memória, escrever pixels, criar janela real, submeter window/GUI, submeter release, podar storage, liberar recursos, persistir release, persistir reclaim, persistir compaction, persistir tombstone, persistir purge, persistir expiry, persistir retention, persistir archive, persistir journal, persistir ledger, persistir recibo, persistir registro, apagar purge, armar timer de expiry, apagar expiry, anexar log, escrever estado, limpar/liberar recursos, submeter reclaim/compaction/tombstone/purge/expiry/retention/archive/journal/ledger/receipt/record/audit/seal/cleanup/retire/ack, reportar completion, sincronizar CPU/GPU, submeter output/display, fazer flip nem autenticar pela GUI.

Entregáveis concluídos:

- [x] Frame pacing ocioso reduz spin quando o desktop não tem atividade.
- [x] Tick gráfico evita composição/cursor quando cena e cursor estão estáveis.
- [x] Contrato fail-closed do loginwindow GUI preparado no runtime de login.
- [x] Launcher, tray de rede, desktop icons e File Manager receberam slice de UX operacional.
- [x] Terminal gráfico consome shell real com prompt/cwd/logout pelo contrato revisado.
- [x] Fallback `CTRL+ALT+F1` retorna da sessão gráfica ao TTY nos backends nativos.
- [x] Loginwindow GUI ganhou view model fail-closed sem ativar autenticação gráfica.
- [x] Loginwindow GUI exibe preview passivo sem entrada/autenticação gráfica.
- [x] Loginwindow GUI expõe política de credenciais fail-closed sem submit gráfico.
- [x] Recuperação textual só retorna ao login normal por política auditável.
- [x] Buffer efêmero de credenciais do loginwindow impõe máscara, limite e wipe.
- [x] Gate de submit de credenciais do loginwindow falha fechado sem autenticação gráfica.
- [x] Tentativa de submit do loginwindow executa wipe obrigatório do buffer.
- [x] Redutor puro de input do loginwindow aplica append/backspace/submit/cancel sem autenticação gráfica.
- [x] Snapshot mascarado do campo de credenciais expõe estado sem segredo bruto.
- [x] Painel seguro de credenciais combina máscara, input e estado fail-closed.
- [x] Pipeline seguro de interação aplica input e reconstrói painel mascarado.
- [x] Snapshot de prontidão resume render/input/wipe sem autenticação gráfica.
- [x] Evento auditável redigido registra credenciais sem expor segredo.
- [x] View model seguro de credenciais compõe prontidão e auditoria redigida.
- [x] Pipeline composto de UI aplica input e recompõe credenciais com auditoria.
- [x] Sessão segura de UI inicializa storage e limpa credenciais antes de retornar.
- [x] View model de recuperação de credenciais une fallback textual e sessão segura.
- [x] Snapshot seguro de tela compõe login, credenciais e recuperação textual.
- [x] Sessão one-shot segura compõe runtime, tela e wipe de IO.
- [x] Plano de renderização seguro traduz sessão em ações UI redigidas.
- [x] Plano de ações seguro valida intenções UI sem submit gráfico.
- [x] Evento UI seguro audita intenções de credenciais já validadas.
- [x] Plano de rotas seguro traduz eventos UI em navegação sem auth gráfica.
- [x] Controller seguro traduz rotas em decisões finais de UI sem auth gráfica.
- [x] Presenter seguro traduz decisões em apresentação final sem auth gráfica.
- [x] Binding seguro traduz apresentação em montagem GUI sem auth gráfica.
- [x] Mount plan seguro traduz binding em transação GUI sem auth gráfica.
- [x] Commit plan seguro traduz mount plan em decisão GUI declarativa sem auth gráfica.
- [x] Handoff plan seguro traduz commit plan em envelope GUI declarativo sem auth gráfica.
- [x] Dispatch plan seguro traduz handoff plan em ticket GUI declarativo sem auth gráfica.
- [x] Queue plan seguro traduz dispatch plan em ticket GUI declarativo sem auth gráfica.
- [x] Activation plan seguro traduz queue plan em ticket GUI declarativo sem auth gráfica.
- [x] Frame plan seguro traduz activation plan em ticket GUI declarativo sem auth gráfica.
- [x] Surface plan seguro traduz frame plan em superfície GUI declarativa sem auth gráfica.
- [x] Compositor plan seguro traduz surface plan em ticket GUI/compositor declarativo sem auth gráfica.
- [x] Damage plan seguro traduz compositor plan em ticket declarativo de damage/cache sem auth gráfica.
- [x] Present plan seguro traduz damage plan em ticket declarativo de apresentação sem auth gráfica.
- [x] Schedule plan seguro traduz present plan em ticket declarativo de agendamento sem auth gráfica.
- [x] Vsync plan seguro traduz schedule plan em ticket declarativo de sincronização sem auth gráfica.
- [x] Scanout plan seguro traduz vsync plan em ticket declarativo de display sem auth gráfica.
- [x] Display plan seguro traduz scanout plan em ticket declarativo final sem auth gráfica.
- [x] Output plan seguro traduz display plan em ticket declarativo de saída visual sem auth gráfica.
- [x] Blit plan seguro traduz output plan em ticket declarativo de cópia visual sem auth gráfica.
- [x] Framebuffer plan seguro traduz blit plan em ticket declarativo de framebuffer sem auth gráfica.
- [x] Flush plan seguro traduz framebuffer plan em ticket declarativo de flush/cache sem auth gráfica.
- [x] Barrier plan seguro traduz flush plan em ticket declarativo de barreira/visibilidade sem auth gráfica.
- [x] Fence plan seguro traduz barrier plan em ticket declarativo de fence/sync sem auth gráfica.
- [x] Timeline plan seguro traduz fence plan em ticket declarativo de timeline/semaphore sem auth gráfica.
- [x] Sync plan seguro traduz timeline plan em ticket declarativo de sincronização sem auth gráfica.
- [x] Deadline plan seguro traduz sync plan em ticket declarativo de deadline sem auth gráfica.
- [x] Completion plan seguro traduz deadline plan em ticket declarativo de completion sem auth gráfica.
- [x] Ack plan seguro traduz completion plan em ticket declarativo de acknowledge sem auth gráfica.
- [x] Retire plan seguro traduz ack plan em ticket declarativo de retire sem auth gráfica.
- [x] Cleanup plan seguro traduz retire plan em ticket declarativo de cleanup sem auth gráfica.
- [x] Seal plan seguro traduz cleanup plan em ticket declarativo de seal sem auth gráfica.
- [x] Audit plan seguro traduz seal plan em ticket declarativo de auditoria sem auth gráfica.
- [x] Record plan seguro traduz audit plan em ticket declarativo de registro sem auth gráfica.
- [x] Receipt plan seguro traduz record plan em ticket declarativo de recibo sem auth gráfica.
- [x] Ledger plan seguro traduz receipt plan em ticket declarativo de ledger sem auth gráfica.
- [x] Journal plan seguro traduz ledger plan em ticket declarativo de journal sem auth gráfica.
- [x] Archive plan seguro traduz journal plan em ticket declarativo de archive sem auth gráfica.
- [x] Retention plan seguro traduz archive plan em ticket declarativo de retention sem auth gráfica.
- [x] Expiry plan seguro traduz retention plan em ticket declarativo de expiry sem auth gráfica.
- [x] Purge plan seguro traduz expiry plan em ticket declarativo de purge sem auth gráfica.
- [x] Tombstone plan seguro traduz purge plan em ticket declarativo de tombstone sem auth gráfica.
- [x] Compaction plan seguro traduz tombstone plan em ticket declarativo de compaction sem auth gráfica.
- [x] Reclaim plan seguro traduz compaction plan em ticket declarativo de reclaim sem auth gráfica.
- [x] Release plan seguro traduz reclaim plan em ticket declarativo de release sem auth gráfica.
- [x] GUI plan seguro traduz release plan em ticket declarativo de GUI sem auth gráfica.
- [x] Window plan seguro traduz GUI plan em ticket declarativo de janela sem auth gráfica.
- [x] Window surface plan seguro traduz window plan em ticket declarativo de surface de janela sem auth gráfica.
- [x] Window compositor plan seguro traduz window surface plan em ticket declarativo de compositor de janela sem auth gráfica.
- [x] Window present plan seguro traduz window damage plan em ticket declarativo de apresentação de janela sem auth gráfica.
- [x] Window schedule plan seguro traduz window present plan em ticket declarativo de agendamento de janela sem auth gráfica.
- [x] Window vsync plan seguro traduz window schedule plan em ticket declarativo de sincronização de janela sem auth gráfica.
- [x] Window scanout plan seguro traduz window vsync plan em ticket declarativo de scanout de janela sem auth gráfica.
- [x] Window display plan seguro traduz window scanout plan em ticket declarativo de display de janela sem auth gráfica.
- [x] Window output plan seguro traduz window display plan em ticket declarativo de saida visual de janela sem auth gráfica.
- [x] Window blit plan seguro traduz window output plan em ticket declarativo de blit de janela sem auth gráfica.
- [x] Window commit plan seguro traduz window blit plan em ticket declarativo de commit atômico de janela sem auth gráfica.
- [x] Window flip plan seguro traduz window commit plan em ticket declarativo de page flip de janela sem auth gráfica.
- [x] Window vblank plan seguro traduz window flip plan em ticket declarativo de sincronização de vblank de janela sem auth gráfica.
- [x] Window event plan seguro traduz window vblank plan em ticket declarativo de eventos de janela sem auth gráfica.
- [x] Window input plan seguro traduz window event plan em ticket declarativo de input de janela sem auth gráfica.
- [x] `userdb_authenticate` compara hash PBKDF2 em tempo constante, zera buffers sensíveis e log de bootstrap não expõe salt/hash.
- [x] `userdb_authenticate_with_policy` integra lockout + auth + record em um único ponto público com códigos `USERDB_AUTH_OK/FAILED/LOCKED`; `system_setup.c` deduplica check/auth/record; Makefile com typo `src/driverslogin_window_gui_layout.c src/auth//net/net_probe.c` corrigido para desbloquear build host-side.
- [x] CSPRNG `csprng_get_bytes` invoca `sha256_clear` no snapshot `temp_ctx` por iteração (fecha vazamento de digest emitido em stack); `sha256_clear` virou API pública com semântica volatile-safe; `auth_policy` separa `find_existing` (read-only) de `find_or_alloc` (com LRU eviction de slots não-bloqueados), fechando exaustão da tabela por probing read-only; `userdb_set_password` wipa `source`/`line`/`rec`/`out` em todos os retornos; `memory_zero` em `user.c` virou volatile-safe contra dead-store elimination.
- [x] SHA-256 context wipe hygiene propagado: `crypt.c::hmac_sha256` (PBKDF2 inner) e `crypt.c::crypt_hmac_sha256` (público) zeram `ctx`/`key_ctx` após uso; `sha256.c::sha256_hash` wrapper zera `ctx`; `key_storage_probe.c::compute_volume_key_hash` (digest da senha do volume cifrado) zera `ctx`. Fecha o último resíduo de stack após `sha256_final` nos consumidores reais de SHA-256 que processam segredos.
- [x] Update verifier fail-closed gate: `ed25519_verify` (antes matematicamente quebrado, aceitava forjas triviais sem private key) retorna -1 incondicionalmente; `ed25519_sign` e `ed25519_create_keypair` zeram saída em vez de produzir bytes determinísticos sem propriedade criptográfica; `manifest_signature_ed25519_valid` em `update_agent.c` passa a rejeitar 100% dos manifests em produção por design; header `security/ed25519.h` ganha SECURITY WARNING explícito; testes contratuais novos em `test_crypt_vectors.c::test_ed25519_failclosed_contract` bloqueiam regressão. Tests UNIT_TEST via `g_update_manifest_verifier` hook continuam funcionais.
- [x] Privacy hardening do auth: `auth_policy.c::auth_policy_status` (alcançado pelo comando shell `auth-status` sem check de privilégio) substituido — antes listava todos os usernames trackados + failure_count + estado de lockout (enumeração de usuários, sinais de strategy, lockout escape), agora emite apenas counts agregados (`Tracked: N (locked: M)`). `privilege.c::priv_log_emit` (callsites em settings add-user, user-manage, set-pass:other) substitui `actor=<username>` por `actor_role=<role>` em `[priv] denied:`/`granted:` — antes o username vazava no klog ring, crash buffers e dumps de panic; agora apenas o role classifica o ator. Novas APIs públicas `auth_policy_tracked_count` e `auth_policy_locked_count` (counts não-identificantes); `auth_policy_is_tracked` exposta apenas sob `UNIT_TEST` preserva regressão tests sem reabrir vetor de enumeração em produção. Tests contratuais novos em `test_privilege.c::test_privilege_log_omits_username` e em `test_auth_policy.c`.
- [x] Timing-equalised lockout: `userdb_authenticate_with_policy` (API pública de login) antes retornava `USERDB_AUTH_LOCKED` imediatamente quando `auth_policy_check_allowed` indicava lockout, sem executar PBKDF2 — atacante remoto distinguia accounts locked (~1µs) de not-locked (~50ms) com um único probe via wall-clock, reabrindo por timing as informações que alpha.211 removeu de `auth_policy_status` e alpha.206 removeu de `userdb_authenticate`. Agora o wrapper sempre executa `userdb_authenticate` ANTES de verificar lockout; se locked, descarta resultado e zera `out` via `user_record_clear`. Composição com alpha.206 (dummy salt para non-existent users) e alpha.211 (status/logs sem PII) mantida. Comentário SECURITY de ~40 linhas em `src/auth/user.c` documenta threat model; teste contratual em `test_auth_policy.c` lock o lado policy do contrato.
- [x] HKDF-SHA256 (RFC 5869) primitiva fundacional: antes o sistema tinha apenas PBKDF2-SHA256 (senhas) e HMAC-SHA256 (publico) — faltava um KDF context-aware para derivar subkeys de segredos uniformes. Agora `crypt_hkdf_sha256_extract` (PRK = HMAC(salt, IKM)), `crypt_hkdf_sha256_expand` (OKM = T(1)||T(2)||...) e `crypt_hkdf_sha256` (wrapper) expostos em `include/security/crypt.h`. HMAC-SHA256 streaming interno (`hkdf_hmac_begin/update/end`) suporta `info_len` arbitrario sem alocacao dinamica. Fail-closed em L > 255*HashLen (8160 bytes), PRK < HashLen, NULL prk/out, com L=0 no-op success e substituicao zero-octet para salt vazio per §2.2. Wipe hygiene de zero_salt, PRK, T(i), HMAC scratch (kipad/kopad/key_hash) e contextos SHA-256 em todos os exits. 3 test vectors do RFC 5869 Appendix A (TC1 small/TC2 long 80/80/80/82 multi-iter/TC3 salt+info empty) + 6 contract checks em `test_crypt_vectors.c`. Habilita slices futuros: TLS userland (Etapa 5) handshake/traffic keys, key wrapping para AES-XTS, secure boot verification keys versionadas, update_agent signing context-bound.
- [x] CSPRNG hardening profundo: corrige bug `rdtsc` com constraint `"=A"` mal-formed em x86_64 (constraint ambigua entre 32/64-bit; usa `"=a"`/`"=d"` separados); adiciona boot-time entropy diversa (TSC jitter loop com 16 deltas micro de cache/predictor/scheduler effects — entropia mesmo em VM sem RDRAND; pool address mixing KASLR-aware); adiciona reseed proativo automatico a cada 64 KiB emitidos com `mix_hardware_entropy()` (RDRAND + TSC frescos); RDRAND com retry-loop de 10 attempts per Intel SDM (probabilidade de sucesso sobe para ~1 - 2^-2000); nova API `csprng_feed_entropy_buffer(data, len)` para fontes de buffer arbitrario; nova API publica `csprng_reseed()` para callers criticos antes de operacoes longas; caller real em `src/drivers/input/mouse.c::mouse_ps2_irq_handler` (cada byte de pacote PS/2 contem timing humano residual); 3 novos testes contratuais em `test_csprng.c` (feed_buffer, reseed, auto-reseed-after-interval); threat model documentado inline no header. ABI publica preservada (`csprng_init`/`feed_entropy`/`get_bytes`/`next_u32`/`fill` mantem assinatura). Wipe hygiene de alpha.208 preservada (`sha256_clear` em snapshot por iteracao).
- [x] ChaCha20-Poly1305 AEAD (RFC 8439) canonico em `src/security/chacha20_poly1305.c`: primeira AEAD nativa do CapyOS (AES-XTS existente e so confidencialidade sem autenticacao; ChaCha20-Poly1305 do BearSSL ficava preso ao contexto TLS handshake). Quatro APIs publicas em `include/security/chacha20_poly1305.h`: `chacha20_block` (RFC §2.3, 20-round permutation), `chacha20_encrypt` (RFC §2.4, stream cipher in-place, counter overflow fail-closed previne reuso catastrofico de keystream), `poly1305_mac` (RFC §2.5, radix-26 com clamping correto, multiplicacao `h*r mod (2^130-5)` reduzida via `*5`, reducao final via dual representation em tempo constante), `chacha20_poly1305_encrypt`/`decrypt` (RFC §2.8, OTK derivada do bloco 0, tag verification em tempo constante via `crypt_constant_time_compare` ANTES do decrypt para nao revelar plaintext parcial se tag invalido). Wipe hygiene volatile-safe em OTK/state/keystream/Poly1305 internal em todos os exits (sucesso e erro). Threat model documentado inline no header (confidencialidade, integridade autenticada, indistinguibilidade polynomial, replay caller-responsibility, limites 256 GiB por key/nonce, nao constant-time em tamanho de input). 4 novas funcoes de teste com 30+ assertions em `tests/test_crypt_vectors.c`: `test_chacha20_block_vectors` (RFC §A.1 TC1/TC2), `test_chacha20_encrypt_round_trip` (round-trip + in-place + counter overflow rejection), `test_poly1305_vectors` (§A.3 TC1 zero key/msg → zero tag, TC2 r=0 → tag=s, avalanche), `test_chacha20_poly1305_aead` (round-trip + 5 categorias de tampering rejection ciphertext/AAD/tag/key/nonce, empty pt/AAD support, fail-closed em NULL key). Composicao com alpha.213 (HKDF deriva chaves ChaCha20 a partir de master secret + context label) e alpha.214 (CSPRNG fornece key 256-bit e nonce 96-bit uniformes) integral. ABI publica nova, aditiva. Build: `chacha20_poly1305.o` em `CAPYOS64_OBJS`, `chacha20_poly1305.c` em `TEST_SRCS`. Sem callers reais ainda — esta entrega e a primitiva; callers chegarao em slices subsequentes (IPC autenticado kernel-userland, container cifrado userland, secure messaging local, future TLS 1.3 userland).
- [x] X25519 (RFC 7748) ECDH canonico em `src/security/x25519.c`: primeira primitiva de key exchange nativa do CapyOS, independente do TLS stack BearSSL e usavel fora de TLS (TLS 1.3 userland Etapa 5, channel binding, WireGuard-like channels com forward secrecy, secure boot key exchange, secure messaging local com forward secrecy). Duas APIs publicas em `include/security/x25519.h`: `x25519(scalar, u_coord, shared)` com clamping interno per RFC 7748 §5 (zera bits 0,1,2,255; seta bit 254), u-coord top-bit masking per §5, e small-subgroup detection per §6.1 (rejeita shared=0 fail-closed para resistir a small-order point attacks); `x25519_base(scalar, public_key)` usando u=9 per §4.1. Implementacao constant-time via Montgomery ladder de 255 iteracoes (bit 254 down to bit 0), sem branches sobre bits secretos do scalar. Field arithmetic GF(2^255-19) em representacao 5x51-bit limbs com __uint128_t multiplication: `fe_zero`, `fe_one`, `fe_copy`, `fe_add` (sem carry imediato), `fe_sub` (soma 2p para evitar underflow), `fe_mul`/`fe_sq` (schoolbook com reducao mod p via *19 nos termos i+j>=5), `fe_mul_small` (para a24=121665), `fe_carry` (propagacao de carries com reducao mod p), `fe_invert` (cadeia ref10: 255 squarings + 11 multiplications computando a^(2^255-21) = a^(p-2)), `fe_tobytes` (canonicalizacao para [0,p) via detection (t+19)>>255 e subtracao condicional constant-time de p), `fe_frombytes` (top-bit masking per §5), `fe_cswap` (constant-time via mask aritmetico mask = -(uint64_t)swap). Wipe hygiene volatile-safe em todos os intermediarios (ladder x_1/x_2/z_2/x_3/z_3/A/AA/B/BB/E/C/D/DA/CB/tmp1/tmp2, invert t0..t3, internal e/u/x_res/z_res/z_inv/out_fe) em todos os exits. Threat model documentado inline no header (confidencialidade sob CDH, fail-closed em small-order via shared=0 gate, indistinguibilidade polynomial modulo top bit, limites: nao autentica public keys — caller responsibility). 6 novas funcoes de teste com 25+ assertions em `tests/test_crypt_vectors.c`: `test_x25519_rfc7748_scalarmult` (vetores §5.2: scalar `a546e3...` × u `e6db68...` → expected `c3da55...` e segundo vetor `4b66e9...` × `e52102...` → `95cbde...`), `test_x25519_rfc7748_dh` (§6.1 Alice sk `770760...` → pk `8520f0...`, Bob sk `5dab08...` → pk `de9edb...`, shared `4a5d9d...` com convergence assertion Alice/Bob), `test_x25519_small_order_rejection` (u=0 e u=1 ambos retornam -1), `test_x25519_fail_closed` (5 NULL combinations: NULL scalar/u/shared em x25519, NULL scalar/public_key em x25519_base), `test_x25519_high_bit_masked` (flip do bit 255 do u nao altera output), `test_x25519_scalar_clamping` (flip dos bits clamped no scalar nao altera output). Composicao com alpha.214 (CSPRNG fornece scalar uniforme 32-byte) e alpha.213 (HKDF deriva session_key de shared+context label) e alpha.215 (ChaCha20-Poly1305 consome session_key para canal autenticado com forward secrecy) integral. **Triplet canonico ECDH→HKDF→AEAD agora completo.** ABI publica nova, aditiva. Build: `x25519.o` em `CAPYOS64_OBJS`, `x25519.c` em `TEST_SRCS`. Sem callers reais ainda — esta entrega e a primitiva fundacional; callers chegarao em slices futuros (TLS 1.3 userland Etapa 5, secure messaging local com forward secrecy, future WireGuard-like channel).
- [x] Ed25519 (RFC 8032) implementacao REAL em `src/security/ed25519.c` substituindo o esqueleto fail-closed que vinha desde alpha.210; **update verifier (`src/services/update_agent.c`) oficialmente OPERACIONAL pela primeira vez** — manifests assinados com chave canonica em producao agora aceitos quando criptograficamente validos (gate criptografico oficial de updates ativo). Field arithmetic GF(2^255-19) refatorado em modulo compartilhado `include/security/fe25519.h` + `src/security/fe25519.c` (extraido de `x25519.c`, adiciona `fe_pow22523` para sqrt em Ed25519 decode + `fe_neg` + `fe_cmov` + `fe_isnegative` + `fe_iszero` + `fe_notequal`), agora consumido por X25519 + Ed25519 (eliminando duplicacao). `src/security/x25519.c` reduzida para Montgomery ladder + APIs publicas consumindo `fe25519`. Tres APIs publicas em `include/security/ed25519.h`: `ed25519_create_keypair` (seed -> SHA-512 -> clamp scalar s + prefix + A=s*B + encode), `ed25519_sign` (RFC §5.1.6 PureEd25519 deterministico: r=SHA-512(prefix||M) mod L, R=r*B, k=SHA-512(R||A||M) mod L, S=(r+k*s) mod L, signature=R||S), `ed25519_verify` (RFC §5.1.7 com cofator 8: check S<L, decode R+A, k=SHA-512(R||A||M) mod L, check [8]SB == [8]R + [8](kA) via projective equality X1*Z2==X2*Z1 && Y1*Z2==Y2*Z1). Group ops twisted Edwards em coordenadas extended (X:Y:Z:T) com a=-1: `ge_dbl` (dbl-2008-hwcd 4sq+4mul), `ge_add` (add-2008-hwcd-3 9mul+7add com `T1*2d*T2`), `ge_neg_p` (-X, Y, Z, -T), `ge_cmov` constant-time. Scalar multiplication double-and-add constant-time (256 doubles + 256 cond-adds, cmov mascarado sem branch sobre bit secreto): `ge_scalarmult`, `ge_scalarmult_base` usando `ED_B`, `ge_double_scalarmult` para verify (`k*A + S*B`). Encoding/decoding compressed (32 bytes Y || sign(x)): `ge_encode` (Y/Z em LE + bit 7 do byte 31 = sign(x) via `fe_isnegative`), `ge_decode` (parse y + canonicality check via re-encode comparison, candidate `x = u*v^3*(u*v^7)^((p-5)/8)` via `fe_pow22523`, verify `v*x^2 == ±u`, multiply por `ED_SQRTM1` se -u, set sign correto, rejeita x==0 && x_0==1 per RFC §5.1.3 step 5). Scalar arithmetic mod L (L = 2^252 + 27742317777372353535851937790883648493): `sc_reduce64` (reduz 64-byte SHA-512 output via signed 21-bit limbs com cascading multiply-and-add usando `l_lo = [666643, 470296, 654183, -997805, 136657, -683901]`), `sc_muladd` (a*b+c mod L), `sc_is_canonical` (S < L gate constant-time). Constants verificadas contra dalek-cryptography (5x51-bit limbs): `ED_D = -121665/121666`, `ED_D2 = 2*ED_D`, `ED_SQRTM1 = 2^((p-1)/4)`, `ED_B_X/ED_B_Y/ED_B_T` (base point), `ED_L_BYTES`. SHA-512 helpers `sha512_two_block` e `sha512_three_block` com wipe de `struct sha512_ctx`. Wipe hygiene volatile-safe em todos os fe/ge/scalars/SHA-512 contexts intermediarios em todos os exits. Threat model documentado inline: cofator 8 em verify per RFC §5.1.7 step 4 mandatory (elimina componentes torsao, resistente a strongbinding attacks); S<L gate previne signature malleability; canonicality check em decode previne non-canonical encodings; x==0 && x_0==1 rejeita ponto ambiguo. update_agent.c `manifest_signature_ed25519_valid` substitui comentario fail-closed por documentacao do gate criptografico ativo. Tests: `test_ed25519_failclosed_contract` reformulado com 3 vetores oficiais RFC 8032 §7.1 (empty/1-byte/2-byte messages, verify pk derivation + signature derivation + verify acceptance), tampering rejection (sig[0] e sig[32] flip), wrong-pk rejection, non-canonical S rejection (S=L, S>L), NULL inputs fail-closed, tolerancia a NULL outputs em sign/create_keypair, round-trip com 64-byte message + tamper-message rejection, deterministic signing. Build: `fe25519.o` em `CAPYOS64_OBJS`, `fe25519.c` + `sha512.c` em `TEST_SRCS`. Composicao: alpha.210 (Ed25519 esqueleto fail-closed) -> agora real; alpha.216 (X25519) usa mesma fe25519 compartilhada. Fundacao cripto canonica CapyOS encerra o ultimo gap de primitivas modernas (10 primitivas em `src/security/`, todas auditaveis: SHA-256, SHA-512, HMAC, PBKDF2, HKDF, CSPRNG, AES-XTS, ChaCha20-Poly1305 AEAD, X25519 ECDH, Ed25519 signatures, constant-time compare).
- [x] Argon2id (RFC 9106) + BLAKE2b (RFC 7693) memory-hard password hashing nativo em `src/security/argon2.c` (~600 LOC) e `src/security/blake2b.c` (~270 LOC) — fundacao cripto CapyOS completa (**11 primitivas modernas**); fecha o gap fundamental contra **brute-force massivo em GPU/ASIC** que afetava PBKDF2-SHA256 (atual default em `crypt.c`) com speedup tipico de 1000-10000x sobre CPU comum; Argon2id e vencedor do **Password Hashing Competition (2015)**, recomendado por **OWASP** e **NIST SP 800-63B**; APIs publicas `blake2b_init`/`update`/`final`/`wipe` + one-shot `blake2b(out, outlen, key, keylen, in, inlen)` com keyed mode HMAC-like ate 64-byte key, lazy compression para streaming correto, param block per RFC §2.5 codificado em `h[0]` inicial, IV/sigma/rotations identicas a SHA-512, fail-closed em NULL/comprimento invalido, wipe volatile-safe; `argon2id_hash(password, password_len, salt, salt_len, t_cost, m_cost, memory, memory_len, out, out_len)` com **caller-provided memory buffer** (sem `malloc` kernel — caller fornece m_cost*1024 bytes via stack/static/heap, flexivel para uso embedded), `parallelism = 1` fixo (RFC 9106 permite explicitamente p=1; multi-lane fora de escopo desta entrega), sem secret K nem associated data X. Pre-hash H0 per §3.2 com `LE32(p) || LE32(T) || LE32(m_cost) || LE32(t_cost) || LE32(version=0x13) || LE32(type=2) || LE32(pwd_len) || pwd || LE32(salt_len) || salt || LE32(0) || LE32(0)`. Variable-length H' per §3.3: T<=64 single BLAKE2b call, T>64 chain de r+1 BLAKE2b(64) com cada V_i alimentando proximo + tail V_{r+1} com (T-32r) bytes. G compression function 1024-byte (128 uint64): R = X XOR Y, view como 8x8 matriz de registers 16-byte, apply P row-wise (8 P calls cada uma sobre 16 uint64 = uma row) + apply P column-wise (8 P calls interleaved), Z = (result) XOR R. P aplica BLAKE2-round-NOMSG com **fBlaMka**(x,y) = `x + y + 2 * (x_lo * y_lo)` substituindo a soma simples — acrescenta multiplicacao 32x32->64 que aumenta cost-per-op em ASIC dedicado. Address block generation per §3.4.1.1 com input_block `{pass, lane=0, slice, m_prime, t_cost, type=2, counter, zero(968)}`, address_block = G(zero_block, G(zero_block, input_block)); counter incrementado por bloco de 128 enderecos; pre-geracao manual para slice 0 pass 0 com start_index=2. Index alpha per §3.4.1.2 com `rel_pos = J1^2 / 2^32` mapeado em ref_area_size via formula nao-uniforme (concentra ref no inicio do reference set), `start_pos = (slice+1)*segment_length` em pass > 0 (exclui slice corrente). Modo Argon2id seleciona data-independent para `pass=0 && slice<2`, data-dependent para o resto. Block computation pass 0: `B[i] = G(B[i-1], B[ref])`; pass > 0: `B[i] ^= G(B[i-1], B[ref])` (semantica v1.3 overwrite-XOR). Wipe volatile-safe em todos os intermediarios (H0, V chains H', blocos prev/ref/new/existing, input_block, address_block, zero_block, final_block) antes do retorno em sucesso e erro. Compatibility: PBKDF2-SHA256 em `crypt.c` preservado (userdb existente nao quebra); migracao incremental userdb para Argon2id sera slice futuro com algorithm prefix `$argon2id$v=19$m=...,t=...,p=1$salt$hash`. Tests: 7 funcoes novas em `tests/test_crypt_vectors.c` (`test_blake2b_rfc7693_abc` vetor canonico RFC 7693 Appendix A "abc"; `test_blake2b_empty` vetor Python hashlib; `test_blake2b_multiblock` fox vetor; `test_blake2b_streaming_equals_oneshot` boundary crossing 128+256+tail; `test_blake2b_variable_output` outlen 16/32/64 distintos via param block; `test_blake2b_keyed` HMAC-like keys diferentes; `test_blake2b_fail_closed` NULL/outlen=0/65/keylen=65/NULL key) + `test_argon2id_smoke` (8 KiB memory, determinismo cross-call, sensibilidade a password/salt/t_cost/m_cost/out_len, empty password OK, fail-closed em NULL salt/salt<8/t=0/m=7/memory<8KiB/out<4/NULL out/NULL memory). Threat model documentado inline: BLAKE2b cobre length-extension via flag f[0] + colision resistance 2^256 + preimage 2^512 + PRF indistinguibilidade quando chaveado; Argon2id cobre brute-force GPU/ASIC via memory wall (m_cost*1024 bytes por candidate forca <10x speedup em ASIC) + TMTO resistance (reducao de memoria por fator k aumenta tempo por k^2 ate sqrt(m_cost)) + side-channel partial (data-independent slice 0-1 pass 0 contra cache-timing, data-dependent depois para resistir a ranking-TMTO). Build: `blake2b.o` + `argon2.o` em `CAPYOS64_OBJS`; `blake2b.c` + `argon2.c` em `TEST_SRCS`. ABI nova aditiva preservando todas as primitivas existentes.
- [x] **On-disk volume header module (alpha.221)** com algorithm marker + per-volume random salt + HMAC-SHA256 check tag em `include/security/volume_header.h` + `src/security/volume_header.c` (~290 + ~620 LOC); fecha o gap conhecido desde alpha.218 ("volume key derivation continua PBKDF2 + g_disk_salt hardcoded") e prepara o consumo da primitiva `crypt_derive_xts_keys_argon2id` entregue em alpha.220 ate entao sem caller real. Define struct on-disk fixa de 512 bytes (magic 'CAPYVHDR' little-endian + version + flags + kdf_algo_id + t_cost + m_cost + salt_len + kdf_salt[64] zero-padded + data_offset_lba + reserved_lba_count + kdf_check_tag[32]=HMAC-SHA256(K1||K2, 'CAPYOS-VOL-HDR-CHECK-v1' || prefix[0..104]) + creation_timestamp_ns + creator_version[32] null-padded + reserved[332] zerado + header_crc32 IEEE 802.3 reflected). `vh_serialize_prefix` e autoridade unica do layout dos primeiros 104 bytes (consumida pelo serializer e pelo HMAC tag computation — impede drift entre o que vai a disco e o que e autenticado). Endianness little-endian explicito via byte stores. CRC32 no-table branchless ~10 LOC com `mask = -(int32_t)(crc & 1u)` evitando branch. `vh_validate_params` strict: PBKDF2 exige t_cost>=1000 e m_cost==0 (rejeita downgrade tampered); Argon2id exige t_cost>=1 e m_cost>=8 (RFC 9106 §3.1); salt_len em [8,64]; data_offset_lba>=1; reserved_lba_count em [1, data_offset_lba]. `_parse` fail-safe: wipe out struct ANTES de qualquer validacao, depois verifica CRC -> magic -> version (sequencia barata) ANTES de params -> reserved-all-zero (carrega). Threat model two-tier documentado inline: `header_crc32` e bit-rot gate fast NAO seguranca (atacante recomputa trivialmente); `kdf_check_tag` e o binding criptografico — atacante que altera salt/algo/t_cost/m_cost/data_offset/reserved_lba forca user a derivar chave diferente, recomputo do HMAC nao bate com tag stored, mount recusa. `_derive_keys` dispatcher fail-closed first: wipe key1/key2 antes de parameter check + dispatch PBKDF2 ou Argon2id conforme algo_id + verify_check_tag + wipe em qualquer falha. NAO distingue 'wrong password' de 'tampered header' no retorno publico (ambos `ERR_CHECK_TAG`) — evita oracle de tampering distinguivel da experiencia normal de senha errada. Tests host-side em `tests/test_volume_header.c`: 13 funcoes / ~70 assertions cobrindo CRC32 RFC 3309 KAT ('', 'a' -> 0xE8B7BE43, '123456789' -> 0xCBF43926, NULL guard, zero-buffer sem null-stop), `_init` happy paths PBKDF2 e Argon2id (verifica todos os campos + salt-tail-zero + creator-null-pad), `_init` fail-closed (13 vetores: NULL out/salt, algo desconhecido, params invalidos PBKDF2/Argon2id, salt_len out of range, data_offset=0, reserved=0, reserved>data_offset), serialize/parse roundtrip com endianness explicit (bytes 0..7 lidos diretamente como ASCII 'CAPYVHDR'), `_parse` fail-closed (NULL inputs, magic/version/CRC/algo/flags/reserved tampered com CRC refixada conforme necessario), `_looks_valid` quick gate (valid -> 1, corrupt -> 0, NULL -> 0, all-zero -> 0), `_derive_keys` success em PBKDF2 e Argon2id (dispatcher produz mesmas keys que `crypt_derive_xts_keys{,_argon2id}` direto + k1!=k2 anti-split-bug), wrong password com sentinela 0xA5 plantada em k1/k2 + assertion que ambos sao wiped a zero, tampered salt rejeitado mesmo com password correto (1-byte flip), algo downgrade attempt rejeitado (header Argon2id legitimo + tag computed; atacante reescreve algo=PBKDF2 + t_cost=1000 + m_cost=0 para passar param validator; dispatcher passa o validador mas derivacao PBKDF2 produz keys diferentes da derivacao Argon2id original; HMAC mismatch; rejeita), fail-closed NULL hdr/pwd/k1/k2 com sentinela wipe verificada. Wiring: Makefile adiciona `$(BUILD)/x86_64/security/volume_header.o` em CAPYOS64_OBJS apos crypt.o; TEST_SRCS inclui `tests/test_volume_header.c src/security/volume_header.c`; `tests/test_runner.c` declara e chama `run_volume_header_tests` apos `run_crypt_vector_tests`. NAO altera installer (`src/installer/installer_main.c` continua escrevendo o filesystem em LBA 0 raw com PBKDF2 + g_disk_salt — volumes instalados em alpha.221 sao bit-identicos aos de alpha.220) nem boot path (`src/arch/x86_64/kernel_volume_runtime/public_mount_api.c` continua usando crypt_derive_xts_keys com PBKDF2). Mapa de entrega linear: **alpha.221 (esta release) entrega a primitiva, testada, audit-friendly**; alpha.222 (planejado) instalador escreve header no LBA 0 raw + filesystem em LBA 1+ via `block_offset_wrap`, boot path tenta header read primeiro com fallback para legacy PBKDF2 + g_disk_salt em volumes pre-alpha.222; alpha.223 (planejado) ferramenta de re-keying in-place de volumes legacy. Composicao integral com alpha.220 (crypt_derive_xts_keys_argon2id backend), alpha.218 (argon2id_hash primitiva), alpha.214 (CSPRNG futuro fornecera salt em alpha.222), alpha.209 (sha256_clear hygiene propaga via crypt_hmac_sha256 para vh_compute_tag_internal). ABI publica preservada (todos os simbolos novos sao aditivos).
- [x] Loginwindow GUI com senha, recuperação segura e handoff de sessão.
- [x] Dispatcher central ganhou snapshot de saude da fila e captura de mouse.
- [x] Teclado de janelas focadas passa pelo dispatcher central sem duplo callback.
- [x] Scroll de mouse de janelas focadas passa pelo dispatcher central.
- [x] Hover de mouse de janelas passa pelo dispatcher central.
- [x] Launcher usa viewport/scroll e File Manager/desktop icons movem itens para pastas por drag-and-drop seguro.
- [x] Click esquerdo de janelas comuns passa pelo dispatcher central sem fila espelhada nem callback direto.
- [x] Right-click/context menu de janelas comuns passa pelo dispatcher central sem callback direto.
- [x] Dispatcher central de mouse/teclado fim-a-fim expõe contrato de rotas comuns e especiais.
- [x] Sessão gráfica expõe snapshot operacional para prontidão de smokes futuros.
- [x] Sessão gráfica deriva bitmask de bloqueios e prontidão `gui-session`/`mouse-events`.
- [x] Prontidão de smokes GUI foi isolada em unidade pura com cobertura host planejada.
- [x] Blockers de smokes GUI expõem máscara conhecida e nomes estáveis.
- [x] Blockers de smokes GUI expõem resumo determinístico para diagnóstico.
- [x] Snapshot de readiness de smokes GUI embute resumo dos blockers.
- [x] Snapshot de readiness de smokes GUI diagnostica rotas ausentes do dispatcher.
- [x] Orquestrador automático da migração cifrada escolhe stage/copy/commit/cleanup/rollback por passo seguro e cobre resume/abort em teste host-side.
- [x] Submit real do loginwindow chama autenticação autoritativa por `userdb_authenticate_with_policy`, preserva lockout e zera credenciais.
- [x] Recuperação segura do loginwindow consolida rotas redigidas, bloqueia bypass de lockout e exige reset+rerender no resume.
- [x] Handoff seguro do loginwindow para sessão gráfica libera desktop autostart apenas após submit autenticado, recovery seguro e usuário elegível.
- [x] Gate/evidência externa determinística para smoke `gui-session`.
- [x] Gate/evidência externa determinística para smoke `mouse-events`.
- [x] Execução externa final dos gates informada pelo operador em 2026-05-18.

---

## Itens movidos para histórico

Foram retirados do plano ativo e preservados em documentação histórica:

- Fundações M0-M8, M4, M5 e W1-W3.
- F1 release snapshot.
- F2 tooling de assinatura/publicação já entregue.
- F3 browser ring-3 histórico.
- F4 rede/HTTP/DNS e `libcapy-tls` metadata-only até `alpha.93`.
- F5 update agent entregue até `update-prepare-explain`.
- F6 fundação GUI/dispatcher já entregue.
- Shims Linux ABI parciais existentes.

Arquivos de referência:

- [`historical/implementation-delivered-through-alpha93.md`](historical/implementation-delivered-through-alpha93.md)
- [`historical/capyos-master-plan-legacy-through-alpha93.md`](historical/capyos-master-plan-legacy-through-alpha93.md)
- [`historical/capyos-status-legacy-through-alpha93.md`](historical/capyos-status-legacy-through-alpha93.md)
- [`historical/capyos-master-plan-pre-roi-reorder.md`](historical/capyos-master-plan-pre-roi-reorder.md)

---

## Nota operacional

A Etapa 2 está oficialmente fechada em `alpha.237` com gates externos `gui-session` e `mouse-events` entregues por contrato, markers seriais, wiring de release e execução externa final informada como aprovada pelo operador em 2026-05-18.
Etapa 3 (sequência reorganizada por ROI em 2026-05-15) está em andamento como **Driver framework + entrada USB HID + storage estável**. Slices 3A-3D da onda USB/XHCI estão entregues por código/testes host-side; o Slice 3D já cobre SET_CONFIGURATION, HID boot protocol, Configure Endpoint e polling interrupt, e o próximo foco é sua validação externa antes de liberar 3E. Etapas 4-16 continuam bloqueadas até o fechamento sequencial da etapa anterior.
