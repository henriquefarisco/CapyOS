# CapyOS 0.8.0-alpha.213+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch introduz **HKDF-SHA256 (RFC 5869)** como primitiva
criptografica fundacional. Antes, o sistema tinha apenas
PBKDF2-SHA256 (usado em senhas, com 64000 iteracoes) e
HMAC-SHA256 (publico) — faltava um KDF context-aware adequado
para derivar subkeys a partir de segredos ja uniformes (output
do CSPRNG, Diffie-Hellman shared secret, output pos-PBKDF2). Os
proximos slices de seguranca exigem essa primitiva: TLS userland
(Etapa 5) precisa de HKDF para derivar handshake/traffic keys do
master secret, key wrapping para AES-XTS precisa de HKDF para
isolar dominio de cifragem, secure boot precisa de HKDF para
derivar verification keys versionadas. Implementar agora em
fundacao audit-friendly evita gambiarras downstream.

## Anatomia

### Construcao (RFC 5869)

```
HKDF = Extract-then-Expand

  PRK = HMAC-SHA256(salt, IKM)         (extract; PRK is HashLen bytes)
  OKM = T(1) || T(2) || ... || T(N)    (expand; up to 255*HashLen bytes)
    T(0) = empty
    T(i) = HMAC-SHA256(PRK, T(i-1) || info || byte(i))
```

### API publica

```c
int crypt_hkdf_sha256_extract(const uint8_t *salt, size_t salt_len,
                              const uint8_t *ikm, size_t ikm_len,
                              uint8_t prk[SHA256_DIGEST_SIZE]);

int crypt_hkdf_sha256_expand(const uint8_t *prk, size_t prk_len,
                             const uint8_t *info, size_t info_len,
                             uint8_t *out, size_t out_len);

int crypt_hkdf_sha256(const uint8_t *salt, size_t salt_len,
                      const uint8_t *ikm, size_t ikm_len,
                      const uint8_t *info, size_t info_len,
                      uint8_t *out, size_t out_len);
```

Retornam 0 em sucesso, -1 em invalid input. Todos os scratch
buffers (PRK, T(i), HMAC kipad/kopad/key_hash, contextos
SHA-256) sao zerados em todos os caminhos de saida, incluindo
erros.

### Streaming HMAC interno

A funcao `crypt_hmac_sha256` publica espera todos os dados num
buffer contiguo. HKDF expand precisa de `HMAC(PRK, T(i-1) ||
info || byte(i))` onde `info` pode ser arbitrariamente longo.
Buffer-ar tudo em um array contiguo exigiria stack frame
ilimitado ou kalloc — indesejavel num primitivo cripto kernel-side.

Solucao: tres helpers `static` privados que envolvem a
construcao HMAC padrao em torno da API streaming
`sha256_init/update/final`:

```c
struct hkdf_hmac_ctx {
  struct sha256_ctx inner;
  uint8_t kopad[SHA256_BLOCK_SIZE];
};
static void hkdf_hmac_begin(struct hkdf_hmac_ctx *ctx,
                            const uint8_t *key, size_t key_len);
static void hkdf_hmac_update(struct hkdf_hmac_ctx *ctx,
                             const uint8_t *data, size_t data_len);
static void hkdf_hmac_end(struct hkdf_hmac_ctx *ctx,
                          uint8_t out[SHA256_DIGEST_SIZE]);
```

`begin` instala kipad no SHA-256 interno e guarda kopad para
finalizacao. `update` aceita pedacos arbitrarios e roteia para
o SHA-256 interno. `end` finaliza inner, faz outer
SHA256(kopad || inner_digest), zera todo o estado sensivel.

## Fail-closed (RFC 5869 §2.2 / §2.3)

- `prk == NULL` em `extract`: retorna -1.
- `prk == NULL` em `expand`: retorna -1.
- `out == NULL` em `expand`: retorna -1.
- `L > 255 * HashLen` (8160 bytes em SHA-256): retorna -1.
  Sem isso, o counter byte enrolaria de 255 para 0 ou para 256
  (UB com cast incorreto) e produziria output silenciosamente
  truncado/duplicado.
- `prk_len < HashLen` em `expand`: retorna -1. RFC obriga PRK >=
  HashLen para preservar o limite de seguranca; PRKs curtos
  produziriam output mas com entropia degradada.
- `L == 0`: retorna 0 sem escrever (no-op success, semanticamente
  vazio).
- `salt == NULL` ou `salt_len == 0` em `extract`: substituicao
  obrigatoria por HashLen bytes de zero. Sem isso, HMAC degenera
  para chave vazia e perde a propriedade universal hash.

## Wipe hygiene

- `crypt_hkdf_sha256_extract`: `zero_salt` (substituicao) zerada
  antes do retorno.
- `crypt_hkdf_sha256_expand`: `t_prev` (saida do HMAC anterior)
  zerada antes do retorno em sucesso E em todos os caminhos de
  erro intermediario (counter > 255).
- `hkdf_hmac_begin`: `kipad`, `key_hash` (quando usado) e o
  contexto SHA-256 de hash de chave zerados via `secure_clear` e
  `sha256_clear`.
- `hkdf_hmac_end`: `outer` ctx, `ctx->inner` ctx e `ctx->kopad`
  zerados.
- `crypt_hkdf_sha256` (wrapper): `prk` zerado antes do retorno
  em todos os caminhos (sucesso e erro do expand).

Composicao com a wipe hygiene de SHA-256/HMAC instalada em
alpha.208/alpha.209: cada `sha256_clear` chama o helper
volatile-safe que o compilador nao pode eliminar como
dead-store.

## Testes (RFC 5869 Appendix A)

`tests/test_crypt_vectors.c::test_hkdf_sha256_vectors` cobre:

- **TC1**: IKM=22, salt=13, info=10, L=42. Small inputs. Valida
  extract, expand, wrapper.
- **TC2**: IKM=80, salt=80, info=80, L=82. Long inputs que
  spanam 3 iteracoes do expand (L=82 > 2*HashLen=64). Valida
  loop counter, info concatenation, output truncation.
- **TC3**: IKM=22, salt empty, info empty, L=42. Valida a
  substituicao zero-octet de RFC 5869 §2.2 (com NULL e com
  ponteiro nao-NULL + length 0).
- **Contract checks**: 6 asserts fail-closed (NULL outputs, L
  acima do bound, PRK curto, L=0 no-op).

`expect_hex` foi ampliado de 64 para 256 bytes de buffer para
acomodar OKM de 82 bytes do TC2.

## Composicao com slices anteriores

- **alpha.206-212 (hardening de auth)**: HKDF NAO substitui
  PBKDF2 para senhas. Pelo contrario: o output pos-PBKDF2 (que
  e uniformemente pseudoaleatorio) e exatamente o tipo de IKM
  para o qual HKDF foi projetado. Quando a feature de "subkeys
  por contexto" entrar, o pipeline sera
  `password -> PBKDF2 -> PRK -> HKDF expand -> {key_disk,
  key_network, key_session, ...}`.
- **alpha.208-209 (sha256_clear publico, wipe hygiene)**: HKDF
  consome essas APIs. Cada contexto SHA-256 reutilizado dentro
  do HKDF inner/outer e zerado via `sha256_clear`.
- **alpha.210 (Ed25519 fail-closed)**: ortogonal. Quando
  Ed25519 real entrar, HKDF pode ser usado para domain-separar
  hashes de mensagens versionadas.

## Desempenho

- Extract: 1 HMAC-SHA256 (~2 microssegundos para mensagens
  pequenas em x86_64, dominado pelo SHA-256 fixo).
- Expand para L bytes: ceil(L / HashLen) HMAC-SHA256 invocacoes,
  cada uma com info_len + 1 byte de payload. Para L=32 (uso
  comum, derivar uma chave AES-256): 1 HMAC. Para L=42 (TC1): 2
  HMACs. Para L=8160 (max teorico): 255 HMACs (~510
  microssegundos).
- Streaming HMAC evita alocacao e copia adicional de `info` —
  o cost extra vs. uma funcao HMAC-full-buffer e nulo.

## Compatibilidade

- API publica nova: nao quebra nada existente.
- `expect_hex` em testes mudou buffer interno (64 -> 256). Todos
  os calls existentes (que testam <= 64 bytes) continuam
  funcionando identicamente.

## Limites

- Nao implementa HKDF-SHA512 (uma instanciacao trivial mas sem
  callers planejados).
- Nao implementa derivacao de chave para criptografia
  autenticada (AEAD via ChaCha20-Poly1305) — slice futuro.
- Nao adiciona callers reais ainda (TLS userland, key wrapping,
  secure boot estao todos em etapas posteriores). Esta entrega
  e a fundacao para esses slices.
- Nao destrava entregaveis pendentes da Etapa 2 (loginwindow GUI
  real, smokes `gui-session`/`mouse-events`).

## Validacao

Validado por revisao estatica:

- `include/security/crypt.h` linhas 32-65: protótipos com
  comentario inline explicando o threat model e a relacao com
  PBKDF2.
- `src/security/crypt.c` linhas 681-771: streaming HMAC helper
  privado (hkdf_hmac_begin/update/end) com wipe hygiene.
- `src/security/crypt.c` linhas 773-807: `crypt_hkdf_sha256_extract`
  com substituicao zero-octet e normalizacao NULL IKM.
- `src/security/crypt.c` linhas 809-872: `crypt_hkdf_sha256_expand`
  com bound check, prk_len check, counter wrap defence, wipe.
- `src/security/crypt.c` linhas 874-890: `crypt_hkdf_sha256`
  wrapper com wipe de PRK em todos os caminhos.
- `tests/test_crypt_vectors.c` linhas 499-644:
  `test_hkdf_sha256_vectors` com 3 vetores oficiais + 6
  contract checks.
- `tests/test_crypt_vectors.c` linha 655: chamada no
  `run_crypt_vector_tests`.
