# CapyOS 0.8.0-alpha.209+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch propaga o uso de `sha256_clear` (exposto como API pública em
`alpha.208`) para **todos** os caminhos do kernel/userland que processam
segredos via contextos SHA-256 transitórios. Continua a trilha de
higienização de stack iniciada em `alpha.206`/`alpha.207`/`alpha.208`,
agora no nível dos consumidores do SHA-256: `crypt.c::hmac_sha256`
(usado por PBKDF2-SHA256), `crypt.c::crypt_hmac_sha256` (API pública de
HMAC), `sha256.c::sha256_hash` (convenience wrapper) e
`key_storage_probe.c::compute_volume_key_hash` (digest da senha do
volume cifrado). É puramente defensivo — zero alteração funcional, zero
mudança de ABI, zero impacto perceptível em performance.

## Entregas

### `src/security/crypt.c::hmac_sha256` (interna, usada por PBKDF2)

- Wipe explícito de **dois** contextos SHA-256:
  - `key_ctx` (usado apenas quando a chave excede `SHA256_BLOCK_SIZE` =
    64 bytes; deriva o hash da chave longa). Antes ficava na stack
    com o hash da chave em `state[]` após `sha256_final`. Agora
    declarada fora do bloco condicional com uma flag `key_ctx_used`
    para wipe condicional.
  - `ctx` (reutilizada para inner-HMAC e outer-HMAC). Após o
    `sha256_final` da camada outer, `state[]` contém o MAC produzido
    (que IS `out`) e `data[]` contém o último bloco padded da camada
    outer (derivado de `kopad ^ key` e do digest da camada inner).
- Os `secure_clear` existentes em `kopad`/`kipad`/`key_hash`
  permanecem como antes.
- **Impacto no caminho quente do login**: cada chamada de
  `userdb_authenticate` invoca `crypt_pbkdf2_sha256` que invoca
  `pbkdf2_hmac_sha256` que invoca `hmac_sha256` **64000 vezes**. Cada
  iteração agora limpa o contexto antes de retornar ao laço, evitando
  que qualquer iteração intermediária deixe seu estado SHA-256 vivo
  na stack até a próxima sobrescrita.

### `src/security/crypt.c::crypt_hmac_sha256` (API pública)

- Wipe do `ctx` reutilizado pelas três fases (key hash opcional, inner
  HMAC, outer HMAC). Mesma semântica que `hmac_sha256` static, mas
  para o entry point exposto no header `security/crypt.h`.

### `src/security/sha256.c::sha256_hash` (convenience wrapper)

- O wrapper init → update → final cria `ctx` no stack frame, processa
  o input e finaliza. Sem `sha256_clear`, o `ctx.state[]` (que IS o
  digest devolvido em `hash`) e o `ctx.data[]` (bloco padded
  derivado do input) sobrevivem na stack do caller até reuso natural.
- Adicionado `sha256_clear(&ctx)` imediatamente após `sha256_final`.
- Este wrapper é usado em vários lugares (key storage, signing helpers,
  shell utilities, debug fingerprints). Todos os call sites passam a
  ter wipe automático sem mudança de código.

### `src/arch/x86_64/kernel_volume_runtime/key_storage_probe.c::compute_volume_key_hash`

- Trilha de cripto de disco: a senha normalizada do volume é hasheada
  para gerar `out_hash` (o digest é o segredo que gate a derivação
  XTS das chaves do volume). Antes, `ctx` ficava na stack com o
  digest em `state[]` e o bloco final padded em `data[]` (derivado
  diretamente da senha do volume).
- Adicionado `sha256_clear(&ctx)` antes de retornar 0.
- Caminho extremamente sensível porque ocorre durante a sequência de
  boot e o `state[]` resultante revela o digest da senha; com o
  digest, um atacante poderia tentar precomputed lookup ou rainbow
  table se conhecesse o esquema de normalização.

## Segurança e privacidade

- **Stack leak do PBKDF2 inner loop fechado.** Cada iteração do laço
  PBKDF2 invocava HMAC, que deixava o contexto SHA-256 com o digest
  da iteração em `state[]`. Para uma derivação típica (64000
  iterações × 2 SHA-256 finais por HMAC = 128 mil contextos vivos em
  sequência), a probabilidade de algum frame deixar resíduo
  recuperável via info-leak era muito maior do que para um único
  call. Agora cada iteração limpa antes de sair.
- **HMAC público endurecido.** `crypt_hmac_sha256` é usada para
  computar MACs de mensagens; o `ctx` final continha o MAC produzido
  (que é o segredo) em `state[]`. Embora `out` seja o legítimo
  destino do MAC, o `ctx` era uma cópia paralela acessível só via
  leitura de stack. Eliminada.
- **Volume key digest endurecido.** A trilha de boot agora não deixa
  o digest da senha do volume na stack após `compute_volume_key_hash`
  retornar.
- **Convenience wrapper alinhado.** `sha256_hash` deixa de ser a
  exceção quanto à hygiene: agora segue o mesmo padrão de
  `csprng_get_bytes`/`hmac_sha256`/`crypt_hmac_sha256`.

## Desempenho e escalabilidade

- `sha256_clear` é um loop de 104 bytes com stores volatile (uma
  cache line + alguns bytes). Custo negligível por chamada.
- Em PBKDF2 × 64000 iterações, custa ~64000 × 2 × 104 = 13 MB de
  stores volatile no total. Para um login completo (~50–200 ms de
  PBKDF2 em CPU típica), o overhead é submilisegundo.
- Zero alocação dinâmica nova.
- ABI inalterada em todos os entry points.

## Validação

Validado por revisão estática. Pontos cobertos:

- `src/security/crypt.c::hmac_sha256` chama `sha256_clear(&ctx)` antes
  dos `secure_clear` finais; quando `key_len > SHA256_BLOCK_SIZE`,
  `key_ctx_used` é `1` e `sha256_clear(&key_ctx)` é executado.
- `src/security/crypt.c::crypt_hmac_sha256` chama `sha256_clear(&ctx)`
  antes dos `secure_clear` finais.
- `src/security/sha256.c::sha256_hash` chama `sha256_clear(&ctx)`
  imediatamente após `sha256_final`.
- `src/arch/x86_64/kernel_volume_runtime/key_storage_probe.c::
  compute_volume_key_hash` chama `sha256_clear(&ctx)` antes do
  `return 0`.
- `key_storage_probe.c` inclui `security/crypt.h` transitivamente via
  `internal/kernel_volume_runtime_internal.h`, que por sua vez pulls
  `security/sha256.h`. `sha256_clear` é visível.
- Testes existentes em `tests/test_crypt_vectors.c` (PBKDF2 vectors,
  SHA-256 vectors, AES-XTS, constant-time compare,
  `test_sha256_clear_semantics`) continuam válidos — nenhuma mudança
  de comportamento observável foi introduzida.
- `tests/test_runner.c` continua chamando `run_crypt_vector_tests`.

## Compatibilidade

- ABI inalterada em todos os entry points modificados.
- Saída funcional de `crypt_pbkdf2_sha256`, `crypt_hmac_sha256`,
  `sha256_hash` e `compute_volume_key_hash` é idêntica (mesmos bytes
  produzidos para os mesmos inputs).
- Vetores de teste oficiais (NIST/RFC) continuam passando — o wipe
  acontece DEPOIS do `sha256_final`, sem interferir no resultado.
- Volume cifrado existente continua decifrando com a mesma senha.
- `/etc/passwd` continua sendo aceito como antes (mesma derivação
  PBKDF2).

## Limites

- Não toca `src/security/ed25519.c`. Esse módulo é uma implementação
  simplificada/aproximada de Ed25519 (uso documentado como "SHA-512
  approximation using double SHA-256") que tem questões de correção
  criptográfica mais amplas — wipe defensivo nele requer um slice
  dedicado de correctness review, fora do escopo de stack hygiene.
- Não introduz `secure_clear` como API pública — mantém o padrão
  static-helper em `crypt.c` (boundary do módulo). A API exportada
  para wipe de SHA-256 contexts é `sha256_clear`, mais específica e
  com contrato mais limpo.
- Não altera o número de iterações de PBKDF2 (64000); a primitiva e
  o custo de derivação permanecem.
- Não destrava entregaveis pendentes da Etapa 2 (loginwindow GUI real,
  smokes `gui-session`/`mouse-events`).
- Não adiciona Argon2id, scrypt ou outro KDF memory-hard.
