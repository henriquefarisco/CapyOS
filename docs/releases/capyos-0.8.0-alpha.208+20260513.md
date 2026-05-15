# CapyOS 0.8.0-alpha.208+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch consolida o hardening criptográfico do CapyOS no nível das
primitivas e do gerenciamento de estado sensível. Fecha **quatro vetores
distintos** de vazamento residual: (1) snapshot de pool no CSPRNG
permanecia na stack após `sha256_final`; (2) tabela de tentativas do
`auth_policy` podia ser exaurida por probing read-only, desabilitando
silenciosamente o lockout; (3) `userdb_set_password` deixava o
`/etc/passwd` inteiro (salt+hash hex de todos os usuários) no heap
freed; (4) `memory_zero` em `user.c` podia ser eliminado por
dead-store elimination. Continua a trilha iniciada em
`alpha.206`/`alpha.207`, agora no nível das primitivas (CSPRNG +
SHA-256 + auth_policy + wipe hygiene).

## Entregas

### Crypto primitives (CSPRNG + SHA-256)

- `include/security/sha256.h` declara `sha256_clear(struct sha256_ctx
  *ctx)` como API pública. A implementação em `src/security/sha256.c`
  usa loop com ponteiro `volatile uint8_t *` para resistir a
  dead-store elimination. Após `sha256_final`, o contexto carrega no
  `state[]` o digest produzido (que IS o segredo emitido) e no
  `data[]` o último bloco padded — ambos viram zero.
- `src/security/csprng.c::csprng_get_bytes` agora invoca
  `sha256_clear(&temp_ctx)` ao final de cada iteração do laço de
  emissão. `temp_ctx` é cópia do pool de entropia (`entropy_pool`) com
  o digest finalizado dentro — sem o wipe, esses bytes permaneciam na
  stack do kernel até serem sobrescritos pelo próximo uso da frame ou
  expostos via info-leak (dump de panic, use-after-pop, leitura de
  stack adjacente). O loop existente também zera o `digest[32]` local,
  agora complementado por wipe do contexto inteiro.

### Lockout policy (auth_policy)

- `src/auth/auth_policy.c` agora separa `find_existing` (read-only) de
  `find_or_alloc` (read-write com LRU eviction). As funções de leitura
  da política — `auth_policy_check_allowed`, `auth_policy_is_locked`,
  `auth_policy_record_success`, `auth_policy_unlock` — passam a chamar
  `find_existing`, que retorna NULL se o usuário não está rastreado.
  Antes, todas essas chamadas passavam por `find_or_alloc`, que **criava
  uma entrada nova** mesmo em caminho de leitura. Isso permitia um
  ataque: probing read-only com `AUTH_MAX_TRACKED_USERS+1` usernames
  forjados exauria a tabela `g_attempts[32]`, e a partir daí
  `find_or_alloc` retornava NULL para usuários legítimos, **desabilitando
  o lockout para eles** (NULL → caller trata como "permitido", failures
  não contabilizados).
- `find_or_alloc` (usado apenas por `record_failure`) ganha LRU
  eviction: quando a tabela está cheia, expira lockouts naturalmente
  encerrados e evicta a entrada **não-bloqueada** com menor
  `last_fail_tick`. Entradas com lockout ativo são **stickys** —
  evictá-las permitiria que um atacante resetasse seu próprio lockout
  fazendo spray de usernames novos. Se todos os 32 slots estão
  bloqueados simultaneamente (cenário de ataque massivo), retorna NULL,
  e a tabela se recupera naturalmente quando os lockouts expiram pelo
  tempo configurado.
- `auth_policy_record_success` move o `klog` de auditoria para antes
  da busca de entrada, preservando o log "Login success" também para
  primeiros logins de usuários nunca rastreados (que agora não passam
  por `find_or_alloc` e não criam slot).

### Credential storage (userdb_set_password)

- `src/auth/user.c::userdb_set_password` ganha wipes sistemáticos em
  **todas as paths de retorno**:
  - Path `!out` (kalloc do buffer de saída falhou): adiciona
    `memory_zero(source, source_len)` antes de `kfree(source)`. Sem
    isso, o `/etc/passwd` inteiro (incluindo salt+hash hex de todos
    os usuários) permanecia no heap freed.
  - Path de falha em `serialize_user_record_line`: amplia o wipe
    parcial (era só `rec.salt` e `rec.hash`) para incluir
    `memory_zero(line, sizeof(line))`, `memory_zero(&rec, sizeof(rec))`,
    `memory_zero(out, out_cap)` e `memory_zero(source, source_len)`.
    `line[]` contém o registro serializado com salt_hex+hash_hex.
  - Path por-iteração após cópia para `out`: troca o wipe parcial de
    `rec.salt`/`rec.hash` por `memory_zero(line, sizeof(line))` e
    `memory_zero(&rec, sizeof(rec))`, cobrindo todo o registro
    parseado.
  - Path `!updated` (usuário-alvo não encontrado): adiciona
    `memory_zero(source, source_len)`.
  - Path de sucesso: adiciona `memory_zero(source, source_len)` ao
    lado do `memory_zero(out, out_cap)` já existente.
- `src/auth/user.c::memory_zero` passa a usar `volatile uint8_t *`,
  resistindo a dead-store elimination — o padrão clássico que silenciosa-
  mente otimiza wipes de segurança em C.

### Testes

- `tests/test_crypt_vectors.c` ganha `test_sha256_clear_semantics()`:
  inicializa um contexto, processa input real, finaliza, chama
  `sha256_clear`, verifica que cada byte do struct (estado interno +
  bloco padded + contadores) ficou zero, e confirma `sha256_clear(NULL)`
  como no-op seguro.
- `tests/test_auth_policy.c` ganha dois blocos novos:
  - **Read paths non-allocating**: faz `check_allowed` + `is_locked`
    em `AUTH_MAX_TRACKED_USERS+5` usernames forjados (`probe-00`…
    `probe-36`), verifica que todos retornam "permitido"/"não
    bloqueado" e que a tabela continua vazia (`probe-00` não aparece
    no `auth_policy_status`).
  - **LRU eviction**: registra 32 falhas distintas, verifica que
    `fill-00` está rastreado mas não bloqueado, registra falha para
    `newcomer`, e verifica que `newcomer` aparece no status enquanto
    `fill-00` (LRU) foi evictado.

## Segurança e privacidade

- **CSPRNG snapshot leak fechado.** O `temp_ctx` em `csprng_get_bytes`
  carregava o digest emitido em `state[]` e o último bloco padded em
  `data[]`. Após `sha256_final`, esses bytes ficavam vivos na stack
  até sobrescrita natural. Com `sha256_clear` por iteração, qualquer
  leitura subsequente de stack (use-after-pop, info-leak via syscall,
  dump de panic) não consegue recuperar bytes do pool/saída.
- **Lockout não pode mais ser desabilitado por probing.** Antes,
  qualquer caller que consultasse `check_allowed` para `K >
  AUTH_MAX_TRACKED_USERS` usernames distintos preenchia a tabela com
  entradas vazias, e a partir daí qualquer NOVO failed login de
  usuário legítimo não conseguia alocar slot → lockout silencioso. Com
  `find_existing` no caminho de leitura, probing read-only é
  completamente sem efeito sobre o estado.
- **LRU eviction preserva o invariante crítico.** Se a tabela enche
  com entradas reais (32 usuários distintos com falhas), a próxima
  falha **substitui** a entrada menos-recentemente-ativa (não-bloqueada).
  Lockouts ativos jamais são evictados, então um atacante não consegue
  resetar seu próprio bloqueio fazendo spray.
- **`/etc/passwd` deixa de vazar pelo heap.** Antes, `userdb_set_password`
  liberava o blob do `/etc/passwd` lido (que tem todos os salt+hash hex
  em ASCII) sem zerar. O próximo `kalloc` reusava aquela região com
  bytes residuais. Agora, todo retorno wipea `source`, `out`, `line`
  e o `rec` por iteração.
- **`memory_zero` resistente a otimizador.** Sem o `volatile`, o
  compilador pode provar que o store é dead (ninguém lê o buffer
  depois) e eliminar a chamada inteira. Com `volatile uint8_t *`, o
  store é mandatório do ponto de vista da ABI.

## Desempenho e escalabilidade

- `sha256_clear`: 104 bytes (96 do struct sha256_ctx + alguns
  contadores) por iteração de CSPRNG; ~32 stores. Desprezível frente
  ao próprio `sha256_final` (~80 rounds de compressão).
- `find_existing` vs `find_or_alloc`: a busca linear continua O(N) com
  N=32. Não há overhead novo no path comum (read paths agora EVITAM o
  loop de allocação).
- LRU eviction: O(N) extra quando a tabela está cheia (raro). Sem
  alocação dinâmica nova; eviction é in-place no array estático.
- `userdb_set_password`: os wipes adicionais somam ~`source_len * 2 +
  out_cap + sizeof(rec) + sizeof(line)` stores por retorno. Para um
  `/etc/passwd` típico de poucos KB, é submicrossegundo.

## Validação

Validado por revisão estática. Pontos cobertos:

- `include/security/sha256.h` declara `sha256_clear` com comentário de
  contrato; `src/security/sha256.c` implementa com volatile loop.
- `src/security/csprng.c` inclui `security/sha256.h` e chama
  `sha256_clear(&temp_ctx)` na linha de wipe antes do laço de digest.
- `src/auth/auth_policy.c` tem `find_existing` (read-only) e
  `find_or_alloc` (com LRU eviction). Read callers
  (`check_allowed`, `record_success`, `is_locked`, `unlock`) usam
  `find_existing`. Write caller (`record_failure`) usa `find_or_alloc`.
- `src/auth/user.c::memory_zero` usa `volatile uint8_t *p`.
- `src/auth/user.c::userdb_set_password` tem wipes em 5 paths
  (`!out`, serialize-fail, by-iteration, `!updated`, success).
- Testes registrados em `run_auth_policy_tests` e
  `run_crypt_vector_tests`, ambos já invocados por `tests/test_runner.c`.

## Compatibilidade

- ABI de `csprng_get_bytes` inalterada; saída funcionalmente idêntica
  (mesmos bytes, mesmo forward-secrecy via rekey).
- ABI de `auth_policy_*` inalterada (parâmetros, retornos, semântica
  observável). Comportamento de eviction é novo, mas só ativa quando a
  tabela está cheia.
- ABI de `userdb_set_password` inalterada (mesmos retornos, mesma
  função observável); apenas os wipes internos mudam.
- Formato de `/etc/passwd` inalterado.
- `sha256_clear` é nova API pública mas não conflita com nada existente.

## Limites

- Não promove `secure_clear` de `crypt.c` a API pública geral. PBKDF2
  e AES-XTS internamente já wipam seus locais via `secure_clear`
  static.
- Não toca o uso de SHA-256 em HMAC (`crypt.c::hmac_sha256` e
  `pbkdf2_hmac_sha256`): essas funções já chamam `secure_clear` nos
  buffers `kopad/kipad/key_hash/u/t/salt_block`, mas o `sha256_ctx`
  intermediário em `pbkdf2_hmac_sha256` poderia ganhar `sha256_clear`
  num próximo slice de hygiene.
- Não fortalece a tabela `g_attempts` contra ataques que LOCKEIAM todas
  as 32 entradas simultaneamente — nesse cenário extremo, novos
  usuários não conseguem registrar failures até que algum lockout
  expire por timeout. Configurável via `lockout_duration_seconds` (padrão
  300s).
- Não introduz Argon2id, scrypt ou bcrypt; PBKDF2-SHA256 com 64000
  iterações continua o piso.
- Não destrava entregaveis pendentes da Etapa 2 (loginwindow GUI real,
  smokes `gui-session`/`mouse-events`).
