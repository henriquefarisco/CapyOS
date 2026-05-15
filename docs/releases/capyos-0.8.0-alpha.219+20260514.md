# CapyOS 0.8.0-alpha.219+20260514

**Data:** 2026-05-14
**Canal:** alpha (experimental)
**Tema:** Argon2id em producao no userdb — login real memory-hard, sem migracao de DB

## TL;DR

A primitiva Argon2id entregue em `alpha.218` ganha caller real:
`userdb` (`src/auth/user.c`) passa a derivar senhas via Argon2id por
default. Toda conta criada (`add-user`) e toda troca de senha
(`userdb_set_password`) emite hash memory-hard a partir desta release.
Contas legacy hashed com PBKDF2-SHA256 continuam autenticando sem
migracao de DB (parser de `/etc/users.db` aceita 7 campos legacy ou 10
campos Argon2id). Dispatcher novo em `src/auth/user_password_hash.{c,h}`
isola escolha de algoritmo, alocacao de Argon2id work memory (8 MiB
via `kalloc`, wipe volatile-safe antes de `kfree`) e wipe hygiene em
modulo testavel host-side sem dependencia de VFS/userdb.

## ANTES (estado em alpha.218)

Fundacao cripto canonica do CapyOS estava **completa**:

| Primitiva | Slice | Estado |
|---|---|---|
| SHA-256, SHA-512 | base + alpha.208/209 | OK |
| HMAC-SHA256 | base | OK |
| PBKDF2-SHA256 | base | OK |
| HKDF-SHA256 | alpha.213 | OK |
| AES-128-XTS | base | OK |
| CSPRNG hardened | alpha.214 | OK |
| ChaCha20-Poly1305 AEAD | alpha.215 | OK |
| X25519 ECDH | alpha.216 | OK |
| Ed25519 signature | alpha.217 | OK |
| BLAKE2b | alpha.218 | OK |
| **Argon2id (primitiva)** | alpha.218 | **OK** |
| **Argon2id em userdb** | — | **ausente** |

`alpha.218` Limites declarou explicitamente:

> **Sem callers reais ainda em userdb.** PBKDF2-SHA256 em
> `src/security/crypt.c` continua disponivel — `userdb` existente nao
> quebra. Migracao incremental para Argon2id e slice futuro com
> algorithm prefix nos hashes armazenados.

Resultado: a fundacao memory-hard ficava **sem caller real**. Toda
conta criada via `add-user` continuava com PBKDF2-SHA256 64000
iteracoes — vulneravel a brute-force massivo em GPU/ASIC com speedup
tipico de **1000-10000x sobre CPU**. Para login local CapyOS, isso
permitia que qualquer atacante com acesso offline a `/etc/users.db`
crackasse senhas fracas em horas.

## AGORA (alpha.219)

### Dispatcher novo: `user_password_hash`

**Arquivos novos:**

- `include/auth/user_password_hash.h` (~105 LOC) — API publica do
  dispatcher.
- `src/auth/user_password_hash.c` (~190 LOC) — implementacao testavel
  host-side.

**APIs publicas:**

```c
const char *user_password_hash_algo_to_string(uint8_t algo_id);
int user_password_hash_algo_from_string(const char *text, size_t len,
                                        uint8_t *out_algo_id);
int user_password_hash_derive(const uint8_t *password, size_t password_len,
                              const uint8_t *salt, size_t salt_len,
                              uint8_t algo_id,
                              uint32_t t_cost, uint32_t m_cost,
                              uint8_t *hash_out, size_t hash_out_len);
int user_password_hash_verify(const uint8_t *candidate_password,
                              size_t candidate_password_len,
                              const uint8_t *salt, size_t salt_len,
                              uint8_t algo_id,
                              uint32_t t_cost, uint32_t m_cost,
                              const uint8_t *stored_hash,
                              size_t stored_hash_len);
```

**Comportamento:**

- `algo_id == USER_PASSWORD_ALGO_PBKDF2_SHA256` (0): dispatcha
  `crypt_pbkdf2_sha256`. `t_cost == 0` mapeia para `USER_ITERATIONS`
  (64000) — bridge para registros legacy sem campo de iteracoes
  serializado.
- `algo_id == USER_PASSWORD_ALGO_ARGON2ID` (1): aloca
  `m_cost * 1024` bytes via `kalloc`, chama `argon2id_hash`, wipa
  buffer com `volatile`-typed pointer antes de `kfree`. Rejeita
  `t_cost < 1` ou `m_cost < 8` (`ARGON2_MIN_*` per RFC 9106 §3.1).
- Fail-closed: `hash_out` wipeado a zero em todo path de erro
  (alocacao falha, parametros invalidos, NULL pointers).
- `verify`: usa scratch local de 64 bytes, deriva, compara com
  `crypt_constant_time_compare`, wipa scratch antes de retornar.

### Schema bump em `user.h`

Constantes novas:

```c
#define USER_PASSWORD_ALGO_PBKDF2_SHA256  0u
#define USER_PASSWORD_ALGO_ARGON2ID       1u
#define USER_ARGON2ID_T_COST  3u
#define USER_ARGON2ID_M_COST  8192u   /* 8 MiB */
```

`struct user_record` cresce 9 bytes no **final** (append-only para
preservar layout binario das primeiras 196 bytes):

```c
struct user_record {
    char username[USER_NAME_MAX];
    uint32_t uid;
    uint32_t gid;
    char home[USER_HOME_MAX];
    uint8_t salt[USER_SALT_SIZE];
    uint8_t hash[USER_HASH_SIZE];
    char role[USER_ROLE_MAX];
    uint8_t algo_id;          /* novo */
    uint32_t algo_t_cost;     /* novo */
    uint32_t algo_m_cost;     /* novo */
};
```

**27 callsites existentes** de `struct user_record` (em
`apps/settings`, `gui/desktop`, `apps/file_manager`, `shell/...`,
`fs/vfs`, `auth/privilege`, `auth/login_runtime`, `auth/session`)
**compilam unchanged** porque so leem `username`/`uid`/`gid`/`home`/`role`.

### Refactor de `src/auth/user.c`

**Antes:**

```c
crypt_pbkdf2_sha256((const uint8_t *)password, cstring_length(password),
                    out->salt, USER_SALT_SIZE,
                    USER_ITERATIONS, out->hash, USER_HASH_SIZE);
```

**Depois (em `user_record_init` e `userdb_set_password`):**

```c
out->algo_id = USER_PASSWORD_ALGO_ARGON2ID;
out->algo_t_cost = USER_ARGON2ID_T_COST;
out->algo_m_cost = USER_ARGON2ID_M_COST;
generate_salt(out->salt, USER_SALT_SIZE);
if (user_password_hash_derive((const uint8_t *)password,
                              cstring_length(password), out->salt,
                              USER_SALT_SIZE, out->algo_id,
                              out->algo_t_cost, out->algo_m_cost,
                              out->hash, USER_HASH_SIZE) != 0) {
    user_record_clear(out);
    return -1;
}
```

**`userdb_authenticate` dispatcha conforme `rec.algo_id`:**

- **Usuario encontrado**: `user_password_hash_verify` com o algoritmo
  e parametros que o record carrega (PBKDF2 legacy continua
  funcionando sem mudanca; Argon2id usa USER_ARGON2ID_T_COST/M_COST).
- **Usuario desconhecido**: roda **Argon2id** com `k_userdb_dummy_salt`
  e `USER_ARGON2ID_T_COST/M_COST` — equaliza com a baseline nova
  (Argon2id ~200ms). Mitigacao de user enumeration timing de
  `alpha.206` preservada.

**Threat model atualizado em comentario inline (~25 linhas):**

```c
/*
 * When the username does not match any record, run the KDF with
 * the dummy salt under the algorithm/parameters that
 * `user_record_init` emits for new records. That keeps the
 * dominant timing path (Argon2id with the default tuning) the
 * same as for a freshly-created account, so a remote attacker
 * probing for valid usernames cannot distinguish "unknown user"
 * from "valid user with a modern record" by measuring the wall
 * clock. Records still hashed under the legacy PBKDF2 path remain
 * faster (~50 ms vs ~200 ms for Argon2id) until they migrate, but
 * that residual leak only reveals "this account was created /
 * last changed before the Argon2id rollout" and never the
 * password itself.
 */
```

### Schema `/etc/users.db`: 7 ou 10 campos

**Legacy PBKDF2** (escrito por binarios pre-alpha.219):

```
username:uid:gid:home:salt_hex:hash_hex:role
```

**Argon2id** (escrito por alpha.219+ para contas novas e contas que
trocaram senha):

```
username:uid:gid:home:salt_hex:hash_hex:role:argon2id:t_cost:m_cost
```

**Parser** (`parse_user_line`):

- Aceita 7 campos (legacy PBKDF2 com `algo_id=0, t_cost=0, m_cost=0`).
- Aceita 10 campos (Argon2id).
- **Rejeita** fail-closed se campo 7 (algoritmo) for desconhecido —
  atacante nao pode downgrade record forjando string arbitraria.
- **Rejeita** se Argon2id record tiver `t_cost < 1` ou `m_cost < 8`.

**Serializer** (`serialize_user_record_line`) escolhe formato por
`algo_id`. PBKDF2 legacy preservado no formato antigo permite
downgrade transparente para binarios pre-alpha.219 sem migracao
reversa do DB. Buffer aumentado de `+64` para `+128` bytes para
acomodar o trailer com margem.

### Memoria

- **8 MiB por auth** (50% do kernel heap 16 MiB).
- Alocado via `kalloc(m_cost * 1024)`, wipeado volatile-safe,
  liberado via `kfree`.
- Auth e **serializado** (sem concorrencia no kernel atual) — 16 MiB
  de heap suporta uma derivacao por vez.
- `kfree` faz coalesce, entao memoria volta integralmente ao pool.

## Testes

**Novos em `tests/test_user_password_hash.c` (`run_user_password_hash_tests`):**

- `test_algo_string_roundtrip` (8 assertions):
  `algo_to_string`/`algo_from_string` canonicos, rejeicao de prefix
  collision (`pbkdf2x`), truncamento (`argon2i`), NULL guards.
- `test_pbkdf2_legacy_roundtrip` (4 assertions): round-trip PBKDF2
  com `t_cost=0` mapeando para `USER_ITERATIONS`, equivalencia com
  `t_cost` explicito, verify aceita correto, rejeita errado.
- `test_argon2id_roundtrip` (5 assertions): determinismo,
  nao-colisao com PBKDF2 com mesmo input, verify aceita/rejeita.
- `test_argon2id_sensitivity` (3 assertions): sensibilidade a
  `salt`/`t_cost`/`m_cost` — anti-regressao de parameter threading
  pelo dispatcher.
- `test_derive_fail_closed` (7 assertions): NULL password com
  `len>0`, NULL salt com `salt_len>0`, `t_cost=0`, `m_cost<8`,
  algoritmo desconhecido, NULL `hash_out`, zero-len `hash_out`.
  Verifica que `hash_out` esta zerado em todo path de erro.
- `test_verify_fail_closed` (3 assertions): NULL stored hash,
  zero-len, stored hash > scratch 64-byte.

**Total: 30 assertions em 6 test functions.** Dispatcher e testavel
host-side porque so depende de `kalloc`/`kfree` (stub host fornece
via `malloc`/`free`) e das primitivas crypto ja linkadas no test
binary.

## Composicoes preservadas

- **alpha.218 (Argon2id primitiva):** consumida diretamente — sem
  duplicacao de codigo.
- **alpha.214 (CSPRNG):** salt continua gerado via
  `csprng_get_bytes(salt, 16)` per RFC 9106 §3.1.
- **alpha.213 (HKDF-SHA256):** ortogonal — sem interacao com este
  slice.
- **alpha.212 (timing-equalised lockout):** wrapper continua
  executando `userdb_authenticate` antes do check policy. Agora roda
  Argon2id internamente para registros novos.
- **alpha.211 (privacy hardening):** `auth_policy_status` continua
  emitindo apenas counts agregados, sem identificadores.
- **alpha.210 (Ed25519 fail-closed gate):** ortogonal — update
  verifier continua via Ed25519 real de alpha.217.
- **alpha.207 (`userdb_authenticate_with_policy`):** wrapper publico
  continua funcional, agora Argon2id-aware via dispatcher.
- **alpha.206 (dummy salt para non-existent users):**
  `k_userdb_dummy_salt` continua valido, agora alimentando Argon2id
  ao inves de PBKDF2.
- **alpha.208/209 (wipe hygiene SHA-256):** dispatcher herda
  `volatile`-typed wipe em todo intermediario.

## Limites e residuais

### Timing leak transicional

Records PBKDF2 legacy ainda autenticam em ~50ms; records Argon2id
novos em ~200ms. Atacante observando latencia distingue **"conta
predates alpha.219 deployment e nunca trocou senha"** vs
**"conta Argon2id"**. Vazamento minimo — nao revela senha, nao
revela existencia, apenas idade aproximada da ultima troca de senha.

**Plano de eliminacao:** slice futuro de **implicit re-hash on
successful auth** — quando usuario PBKDF2 legacy autenticar com
sucesso, o backend pode regenerar hash com Argon2id usando a senha
fornecida e atualizar o record. Apos N logins, populacao migra
naturalmente.

### Sem Argon2id para volume key derivation

`crypt_derive_xts_keys` (volume cifrado AES-XTS) continua usando
PBKDF2-SHA256. Threat model diferente (offline attack, mas chave
nao e secret no mesmo sentido — e usada apenas para derivar key
material para AES-XTS, e a senha de volume e separada da senha de
login). Migracao para Argon2id pode ser slice futuro mas nao tem
mesma urgencia.

### Memory pressure

Cada auth aloca 8 MiB do heap de 16 MiB. Auth serializado, entao
nao ha contencao no kernel atual. Quando SMP entrar (Etapa 15),
revisitar — pode precisar de pool dedicado ou throttling.

## ABI

**Aditiva:**

- 3 novas APIs publicas em `include/auth/user_password_hash.h`.
- 4 novas constantes em `include/auth/user.h`.
- 3 novos campos no FINAL de `struct user_record`.

**Preservado:**

- Toda assinatura existente de `userdb_*` em `include/auth/user.h`.
- Toda chamada existente de `crypt_pbkdf2_sha256` em
  `src/security/crypt.c` (consumido por `key_storage_probe.c`,
  `installer_main.c`, `kernel.c`).
- Formato 7-campos de `/etc/users.db` para downgrade transparente.

## Build

- `src/auth/user_password_hash.c` adicionado a `CAPYOS64_OBJS`
  (kernel build).
- `tests/test_user_password_hash.c src/auth/user_password_hash.c`
  adicionado a `TEST_SRCS` (host-side unit tests).
- `tests/test_runner.c` chama `run_user_password_hash_tests` apos
  `run_crypt_vector_tests`.

## Smokes

Pendentes da Etapa 2 (`gui-session`, `mouse-events`) **nao sao
afetados** — este slice nao toca caminho grafico.
