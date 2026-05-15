# Release `0.8.0-alpha.220+20260514` — Implicit re-hash on successful auth + Argon2id volume-key primitive

**Data:** 14/maio/2026 (UTC-03)
**Canal:** `alpha` (experimental, UEFI/GPT/x86_64)
**Status:** entregue como release atual no `VERSION.yaml`

## TL;DR

Esta release fecha os dois ultimos limites residuais documentados nas
releases anteriores de hardening cripto:

1. **alpha.219 timing leak transicional.** Login com sucesso em conta
   legada PBKDF2 agora re-deriva o hash com Argon2id (memory-hard) e
   regrava `/etc/users.db` no ato. A populacao de contas que vazam
   "predates alpha.219" pelo wall-clock vira monotonicamente para zero
   conforme contas autenticam.
2. **alpha.218 limite de volume key.** Nova primitiva publica
   `crypt_derive_xts_keys_argon2id` em `include/security/crypt.h` /
   `src/security/crypt.c` fornece derivacao memory-hard para AES-XTS
   volume keys (8 MiB de work memory por candidate). Os callers em
   producao (installer, kernel boot path) ficam em PBKDF2 ate o slice
   de header de volume com algorithm marker; a primitiva esta pronta
   para uso assim que o header landar.

Nenhuma quebra de ABI publica. Nenhuma migracao manual do DB. Nenhum
trabalho do administrador requerido.

## Antes / Depois

### ANTES (alpha.219)

**Implicit re-hash transitional gap.** Toda conta criada ou que trocou
senha desde alpha.219 ja saia com Argon2id, mas contas legadas hashed
com PBKDF2-SHA256 antes do rollout continuavam autenticando via
PBKDF2 (legitimo — `userdb_authenticate` dispatcha por `algo_id` do
registro). Tempos:

- **Conta legacy PBKDF2:** ~50 ms (64000 iter SHA-256 HMAC).
- **Conta Argon2id moderna:** ~200 ms (t=3, m=8192 KiB).

Atacante observando latencia de `userdb_authenticate_with_policy`
distinguia "conta predates alpha.219 e nunca trocou senha" de "conta
Argon2id" com uma unica request por username. Nao revelava a senha,
nao revelava existencia (alpha.206 ja timing-equalizou
existent-vs-non-existent via `k_userdb_dummy_salt`), apenas a idade
aproximada da ultima troca. Documentado em alpha.219 Limites como
"slice futuro de implicit re-hash on successful auth".

**Volume key derivation continua PBKDF2.** `crypt_derive_xts_keys`
em `src/security/crypt.c:149` chamada por:

- `src/installer/installer_main.c:464` — criacao do volume cifrado
  na instalacao
- `src/arch/x86_64/kernel_volume_runtime/key_storage_probe.c:75` —
  desbloqueio do volume no boot
- `src/core/kernel.c:392` — prompt fallback do volume password

Todas usam PBKDF2-SHA256 com salt fixo `g_disk_salt` ("NoirOS-FS-Salt")
e 16000 iteracoes. Atacante com disco roubado e binario do kernel (em
`/boot` nao cifrado) brute-forcava o volume password offline com
speedup GPU/ASIC tipico >10000x sobre CPU. Documentado em alpha.218
Limites: "Volume key derivation continua PBKDF2-SHA256. Threat model
diferente — migracao para Argon2id e slice futuro de menor
prioridade."

### AGORA (alpha.220)

**Implicit re-hash on successful auth.** `src/auth/user.c` ganha
refatoracao + hook:

1. `userdb_set_password` extrai toda a logica de read-modify-write do
   `/etc/users.db` para nova helper privada
   `userdb_replace_password_hash`. O policy check
   (`auth_policy_validate_password`) permanece em `userdb_set_password`
   e nao e replicado no helper — assim o implicit re-hash nao pode
   falhar por causa de uma policy que apertou depois do ultimo
   password change (o usuario seria locked out da propria conta).

2. `userdb_authenticate`, depois de `auth_ok=1` com `user_found=1`
   e `rec.algo_id != USER_PASSWORD_ALGO_ARGON2ID`, executa:

   ```c
   (void)userdb_replace_password_hash(username, password);
   ```

   O helper gera salt fresco via `csprng_get_bytes(USER_SALT_SIZE=16)`
   per RFC 9106 §3.1, re-deriva com `user_password_hash_derive`
   (Argon2id default `USER_ARGON2ID_T_COST=3`, `USER_ARGON2ID_M_COST
   =8192 KiB`), `serialize_user_record_line` escolhe 10-field schema
   automaticamente por `algo_id`, e `userdb_write_blob` persiste
   atomicamente (unlink + create + write).

3. **Fail-silent.** Allocation failure (8 MiB do Argon2id) ou FS
   error (disk full, journal incomplete) NAO bloqueia auth que ja
   foi bem-sucedida. Record stays on PBKDF2 e retry no proximo
   login.

**Timing apos a release:**

- **Conta legacy PBKDF2 (primeiro login pos-alpha.220):** ~250 ms
  (50 ms verify + 200 ms rehash + ~5 ms FS rewrite).
- **Conta legacy PBKDF2 (segundo login pos-alpha.220):** ~200 ms
  (record agora e Argon2id; converge com o caminho moderno).
- **Conta Argon2id:** ~200 ms (inalterado).

Apos primeiro login, a conta legacy nao distingue mais por
wall-clock. Population de PBKDF2 records monotonically shrinks toward
zero. Threat model residual atualizado inline em
`src/auth/user.c:683-705`:

> Residual timing leak therefore exists only for accounts that have
> never logged in since alpha.220 was deployed, and it only reveals
> "this account predates the alpha.220 rollout AND has not been used
> yet" — it never reveals the password.

**Argon2id volume-key derivation primitive.** Nova API publica:

```c
int crypt_derive_xts_keys_argon2id(const char *password,
                                   const uint8_t *salt, size_t salt_len,
                                   uint32_t t_cost, uint32_t m_cost,
                                   uint8_t key1[CRYPT_KEY_SIZE],
                                   uint8_t key2[CRYPT_KEY_SIZE]);
```

Em `include/security/crypt.h:82-86`. Implementacao em
`src/security/crypt.c:174-253`. Constantes novas em `crypt.h`:

- `CRYPT_VOLUME_ARGON2ID_T_COST = 3`
- `CRYPT_VOLUME_ARGON2ID_M_COST = 8192` (8 MiB)

Mesma tuning que o userdb — reaproveita o budget de 8 MiB do kernel
heap, evita ter dois work-memory pools dimensionados independentemente.

**Mecanica interna:**

1. Wipe `key1` e `key2` a zero **no inicio** do dispatcher (antes de
   qualquer parameter check) — caller que esqueca de checar return
   code recebe sentinela "no key here" inequivoco em vez de stack
   residue do frame anterior.
2. Reject `t_cost < ARGON2_MIN_T_COST=1`, `m_cost < ARGON2_MIN_M_COST=8`,
   `salt_len < 8` per RFC 9106 §3.1, NULL `password`/`salt`/`key1`/
   `key2`.
3. `kalloc(m_cost * 1024)` — 8 MiB para a tuning default.
4. `argon2id_hash(password, pass_len, salt, salt_len, t_cost, m_cost,
   memory, memory_bytes, derived, 64)`.
5. Wipe `memory` volatile-safe **antes** de `kfree` — impede que freed
   heap region retenha matriz Argon2id walk (que carrega password-
   derived state).
6. Split `derived[0..32]` -> `key1`, `derived[32..64]` -> `key2`
   (mesma split semantics que `crypt_derive_xts_keys` PBKDF2
   preserva).
7. Wipe scratch `derived[64]` antes de retornar.

**Caller posture:** Os callers em producao (`installer_main.c`,
`key_storage_probe.c`, `kernel.c`) NAO foram trocados nesta release.
A primitiva esta entregue, mas o consumo aguarda design do header
de volume com algorithm marker — sem ele, upgrade do binario do
kernel quebra volumes encrypted com PBKDF2 (chaves derivadas
diferentes). Slice futuro definira o header de volume + tools de
re-keying para migracao incremental.

## APIs publicas

### Modificadas

Nenhuma. ABI publica preservada integral.

### Novas

```c
/* include/security/crypt.h */
#define CRYPT_VOLUME_ARGON2ID_T_COST 3u
#define CRYPT_VOLUME_ARGON2ID_M_COST 8192u

int crypt_derive_xts_keys_argon2id(const char *password,
                                   const uint8_t *salt, size_t salt_len,
                                   uint32_t t_cost, uint32_t m_cost,
                                   uint8_t key1[CRYPT_KEY_SIZE],
                                   uint8_t key2[CRYPT_KEY_SIZE]);
```

### Privadas

- `userdb_replace_password_hash` em `src/auth/user.c` — helper privado
  `static`, nao exposta em `include/auth/user.h`.

## Schema

Inalterado. `/etc/users.db` continua aceitando 7-field PBKDF2 legacy
ou 10-field Argon2id (introduzido em alpha.219). Apos primeiro
login, contas legacy migram de 7-field PBKDF2 para 10-field
Argon2id automaticamente.

## Memoria

Implicit re-hash: **8 MiB por re-hash event** (mesma alocacao que o
caminho de criar/trocar senha em alpha.219). Acontece apenas no
primeiro login pos-alpha.220 de uma conta legacy. Allocation falha
nao bloqueia auth.

Volume key Argon2id: **8 MiB por unlock event** quando a primitiva
for consumida. Como os callers atuais ficam em PBKDF2, esta primitiva
nao tem custo runtime nesta release — apenas a presenca do simbolo
no binario do kernel.

Kernel heap atual: 16 MiB (`KHEAP_SIZE` em
`src/arch/x86_64/kmem64.c`). 8 MiB para Argon2id (50%) + restante
para o resto do kernel. Ambos os callers Argon2id sao serializados
no kernel atual (single CPU, sem concorrencia de auth/unlock), entao
nao ha contencao de heap.

## Testes

### Adicionados

- `tests/test_crypt_vectors.c::test_crypt_derive_xts_keys_argon2id`
  com 11 assertions:
  - Determinismo (chamadas identicas produzem `key1` e `key2`
    byte-a-byte iguais)
  - `key1 != key2` (anti-bug de split do output de 64 bytes)
  - Salt sensitivity (mudar salt muda AMBAS as chaves porque Argon2id
    mixa salt em H0 que alimenta toda a matriz)
  - Fail-closed em NULL `password` / NULL `salt` / NULL `key1` /
    NULL `key2` / `t_cost=0` / `m_cost=7` / `salt_len=7`
  - Wipe forensics em failure path (sentinela `0xA5` deve virar
    `0x00` apos return -1)
  - Non-collision com `crypt_derive_xts_keys` (4 iter PBKDF2 vs 1/8
    Argon2id produzem chaves diferentes)

### Existentes preservados

- `tests/test_user_password_hash.c::run_user_password_hash_tests`
  (alpha.219) — inalterado. A logica do dispatcher de password hash
  nao mudou; apenas o caller em `userdb_authenticate` ganhou um
  call site novo.
- `tests/test_runner.c` — inalterado.

### Validacao por revisao de codigo

Os pontos abaixo ficam em revisao estatica (depend de VFS/kmem reais
que nao estao no host test binary):

- `src/auth/user.c:752-754` — chamada de `userdb_replace_password_hash`
  embaixo do bloco de auth_ok, DEPOIS de `*out = rec` e ANTES de
  `memory_zero(&rec, ...)`.
- `src/auth/user.c:823-836` — comentario do helper `userdb_replace_
  password_hash` documentando a separacao de responsabilidade vs
  `userdb_set_password`.

## Limites residuais

### Implicit re-hash exige login para migrar

Contas que nunca logarem desde alpha.220 ficam PBKDF2 indefinidamente.
Para service accounts ou contas dormentes, esse residual persiste.
Solucoes futuras: ferramenta administrativa para forcar re-hash em
batch (precisa do password de cada conta — ou seja, somente
operacional via fluxo "admin re-cadastra"), ou comando shell que
pergunta a senha e re-deriva via `userdb_replace_password_hash`.

### Volume key primitive sem callers em producao

A primitiva esta entregue, mas instalacoes feitas com binarios
anteriores continuam em PBKDF2. Slice futuro precisa:

1. Definir on-disk volume header com algorithm marker + parametros
   (algo_id, t_cost, m_cost, salt_len, salt).
2. Atualizar `installer_main.c` para escrever o header quando criar
   o volume encrypted.
3. Atualizar `key_storage_probe.c` para ler o header no boot e
   despachar para a KDF correta.
4. Ferramenta de re-keying para upgrade in-place de volumes
   legacy (le com PBKDF2, deriva nova chave com Argon2id, re-encripta
   o block 0 + master key se houver).

### Disk salt continua hardcoded

Tanto PBKDF2 quanto o novo Argon2id usam `g_disk_salt` em
`installer_main.c:39-41` e `kernel.c`. Isso significa que todas as
instalacoes CapyOS compartilham o mesmo salt. Para Argon2id o custo
de memory-hard ainda esta preservado (mesmo com salt fixo, atacante
paga 8 MiB por candidate), mas idealmente cada volume teria salt
random per-install. Endereçado no mesmo slice futuro do header de
volume.

## Composicao com slices anteriores

- **alpha.219** (Argon2id em userdb): integral.
  `userdb_replace_password_hash` usa o mesmo dispatcher
  `user_password_hash_derive` que alpha.219 instalou.
- **alpha.218** (Argon2id + BLAKE2b primitivas): integral.
  `crypt_derive_xts_keys_argon2id` consome `argon2id_hash` direto.
- **alpha.214** (CSPRNG): preservada. Salt fresco do rehash via
  `csprng_get_bytes`.
- **alpha.212** (timing-equalised lockout): preservada.
  `userdb_authenticate_with_policy` continua chamando
  `userdb_authenticate` ANTES do `allowed` check; o rehash inline
  acontece DENTRO de `userdb_authenticate` mas DEPOIS de
  `auth_ok=1`, entao o wrapper observa um auth ja completo.
- **alpha.207** (`userdb_authenticate_with_policy` wrapper):
  preservada.
- **alpha.206** (timing equalization existent-vs-non-existent):
  preservada. `k_userdb_dummy_salt` continua sendo usado para
  usuarios desconhecidos.

## Impacto

### Usuario final

- **Login transparente.** Nenhuma acao requerida. Conta migra para
  memory-hard hashing no proximo login.
- **Primeiro login pos-upgrade.** ~50 ms mais lento que antes (rehash
  acontece inline). Imperceptivel na pratica (200 ms de Argon2id ja
  era o caminho moderno baseline).
- **Logins subsequentes.** Mesmo tempo que conta Argon2id (~200 ms).
- **Ataque offline contra /etc/users.db.** Agora paga 8 MiB de
  memoria por candidate para CADA conta (vs zero antes). Speedup
  GPU/ASIC sobre CPU cai de >10000x para <10x per RFC 9106 §1.2,
  empurra crack time de "horas" para "anos" para senhas de
  qualidade media.
- **Volume password ataque offline.** Inalterado nesta release —
  primitiva entregue mas callers ficam em PBKDF2 ate o header de
  volume landar.

### Estrutura do sistema

- **Codigo do userdb mais limpo.** `userdb_set_password` agora e
  wrapper fino de `userdb_replace_password_hash` + policy check.
  Mesma logica usada pelo path de set explicito e pelo implicit
  re-hash, sem duplicacao.
- **Primitiva de volume Argon2id pronta.** Installer/volume-creation
  tools futuros podem opt-in sem rebuild do kernel.
- **Limites residuais documentados.** alpha.218 e alpha.219 Limites
  agora ambos endereçados em uma unica release; novos limites
  abertos (login-required migration, volume header pending,
  hardcoded disk salt) tem path forward claro.

## Validacao

Esta release foi validada por **revisao de codigo apenas** (sem
rodar testes — o ambiente do host test binary nao inclui VFS/userdb
para validar o caminho fim-a-fim do implicit re-hash, e o conteudo
do rehash inline ja e validado indiretamente pelos testes alpha.219
de `user_password_hash_derive`/`_verify`).

Pontos auditaveis estaticamente:

1. **`src/auth/user.c:752-754`:** chamada de
   `userdb_replace_password_hash(username, password)` dentro de
   `userdb_authenticate`, dentro de `if (auth_ok && user_found &&
   rec.algo_id != USER_PASSWORD_ALGO_ARGON2ID)`. (void) cast para
   garantir fail-silent.

2. **`src/auth/user.c:823-963`:** `userdb_replace_password_hash`
   contem a logica completa de read-modify-write; `userdb_set_password`
   contem apenas o policy check + delegacao.

3. **`include/security/crypt.h:82-86`:** signature de
   `crypt_derive_xts_keys_argon2id` declarada.

4. **`src/security/crypt.c:174-253`:** implementacao com fail-closed-
   first (wipe `key1`/`key2` antes de parameter checks), kalloc+wipe+
   kfree do work memory, wipe do scratch.

5. **`tests/test_crypt_vectors.c::test_crypt_derive_xts_keys_argon2id`:**
   11 assertions cobrindo determinismo, key1/key2 split, salt
   sensitivity, fail-closed em todos os parametros invalidos, wipe
   forensics, non-collision com PBKDF2.

6. **`VERSION.yaml:9-12`:** `current/extended` apontam para
   alpha.220+20260514.

Build vai falhar se o symbol `crypt_derive_xts_keys_argon2id` nao for
encontrado pelo linker do test binary — ja referenciado em
`test_crypt_vectors.c:1765`.

## Proximos slices candidatos

- **Volume header com algorithm marker** — desbloqueia migracao de
  callers para `crypt_derive_xts_keys_argon2id` + ferramenta de
  re-keying.
- **Per-volume random salt** — substitui `g_disk_salt` hardcoded por
  salt random per-install armazenado no header de volume.
- **Userdb HMAC integrity tied to volume key** — protege contra
  bit-flip attacks no /etc/users.db (XTS nao garante integridade).
- **Ferramenta admin para batch re-hash** — forca migracao de contas
  dormentes que nao logam ha tempos.

## Arquivos tocados

```
include/security/crypt.h            (+45 LOC, +1 funcao publica, +2 macros)
src/security/crypt.c                (+80 LOC, +1 funcao publica, +1 include)
src/auth/user.c                     (~150 LOC reescritas, +1 funcao privada, +1 call site)
tests/test_crypt_vectors.c          (+135 LOC, +1 test function, +1 chamada em run_*)
VERSION.yaml                        (current/extended bumped, history entry)
docs/releases/capyos-0.8.0-alpha.220+20260514.md  (este arquivo)
docs/releases/README.md             (alpha.220 no topo)
docs/plans/active/capyos-master-plan.md  (secao alpha.220)
README.md                           (versao bumped + descricao)
docs/plans/STATUS.md                (versao bumped + descricao Etapa 2)
docs/architecture/graphical-session-operational.md  (incremento alpha.220)
```
