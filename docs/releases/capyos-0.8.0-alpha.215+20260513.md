# CapyOS 0.8.0-alpha.215+20260513

**Data:** 2026-05-13
**Canal:** alpha (experimental)
**Tema:** ChaCha20-Poly1305 AEAD canonico em `src/security/`

## TL;DR

Implementacao do zero da primitiva AEAD ChaCha20-Poly1305 (RFC 8439) em
`src/security/chacha20_poly1305.c`. Independente do TLS stack BearSSL e
audit-friendly. Primeira AEAD nativa do CapyOS — AES-XTS existente
fornece apenas confidencialidade sem autenticacao. Habilita secure
messaging local, key wrapping autenticado, channel binding e
mensagens de IPC autenticadas sem depender do BearSSL fora do contexto
TLS.

## ANTES (estado em alpha.214)

O CapyOS tinha:

- **PBKDF2-SHA256** (`crypt.c`): KDF para senhas com 64000 iteracoes.
- **HMAC-SHA256** (`crypt.c`): MAC fundacional.
- **HKDF-SHA256** (`crypt.c` desde alpha.213): KDF context-aware
  para subkeys.
- **AES-128-XTS** (`crypt.c`): confidencialidade de volume cifrado.
- **SHA-256** (`sha256.c`): hash fundacional.
- **CSPRNG** (`csprng.c` hardened em alpha.214): gerador robusto.
- **Ed25519** (`ed25519.c`): esqueleto fail-closed (alpha.210).
- **TLS via BearSSL**: ChaCha20-Poly1305 existia mas SO via
  `third_party/bearssl` e SO acessivel pelas cipher suites do TLS
  handshake.

**Gap critico:** nao havia AEAD canonica em `src/security/`. Para
qualquer uso fora de TLS (secure messaging local, key wrapping
autenticado, container cifrado em userland, channel binding,
mensagens de IPC autenticadas, futuros pacotes WireGuard-like),
faltava primitiva. Alternativas eram:

1. **Reinventar em cada caller** — overhead, bugs sutis (Poly1305 e
   notoriamente facil de implementar errado: clamping, modular
   reduction, OTK reuse).
2. **Abusar a interface SSL do BearSSL** — acoplamento indesejado
   entre callers nao-TLS e o stack TLS completo.
3. **Aceitar AES-XTS sem MAC** — confidencialidade sem integridade.
   Atacante com acesso fisico ao disco podia flipar bits nas areas
   cifradas sem detectar.

## AGORA (alpha.215)

### `src/security/chacha20_poly1305.c` (~600 LOC)

Quatro primitivas em um arquivo coeso (familia ChaCha20+Poly1305+AEAD):

1. **`chacha20_block(key, counter, nonce, out)`** — RFC 8439 §2.3.
   20-round permutation produzindo 64 bytes de keystream por
   `(key, counter, nonce)`. Implementacao via macro `QR` para forcar
   inlining do quarter-round (4 ARX operations). State =
   "expand 32-byte k" constants + key + counter + nonce. 10 column
   rounds + 10 diagonal rounds = 20 alternados. Output = state +
   state_inicial (Feistel-like back-add).

2. **`chacha20_encrypt(key, counter, nonce, in, out, len)`** — RFC
   8439 §2.4. Stream cipher XOR-based. `in`/`out` podem ser
   identicos (in-place). **Counter overflow fail-closed**: rejeita
   se `initial_counter + ceil(len/64) > 2^32` para prevenir reuso
   de keystream blocks (vetor catastrofico).

3. **`poly1305_mac(otk, msg, msg_len, tag)`** — RFC 8439 §2.5. MAC
   one-time. Implementacao em radix-26 (5 limbs de 26 bits). Clamping
   correto da chave `r` per spec. Multiplicacao h*r reduzida via
   `*5` (porque 2^130 ≡ 5 mod 2^130-5). Reducao final em tempo
   constante via dual representation.

4. **`chacha20_poly1305_encrypt/decrypt`** — RFC 8439 §2.8.
   Orchestration AEAD:
   - OTK = primeiros 32 bytes de `ChaCha20-block(key, counter=0, nonce)`.
   - ciphertext = `ChaCha20-encrypt(key, counter=1, nonce, plaintext)`.
   - tag = `Poly1305(OTK, aad || pad16 || ct || pad16 || u64le(aad_len) || u64le(ct_len))`.
   - Decrypt: verifica tag em **tempo constante** via
     `crypt_constant_time_compare` ANTES de decifrar. Se invalido,
     nao revela plaintext parcial.

### `include/security/chacha20_poly1305.h`

Header publico com:

- Constantes `CHACHA20_KEY_SIZE` (32), `CHACHA20_NONCE_SIZE` (12),
  `CHACHA20_BLOCK_SIZE` (64), `POLY1305_KEY_SIZE` (32),
  `POLY1305_TAG_SIZE` (16), `CHACHA20_POLY1305_TAG_SIZE` (16).
- Quatro APIs publicas com fail-closed em NULLs e counter overflow.
- Documentacao SECURITY embutida: threat model (confidencialidade,
  integridade, indistinguibilidade polynomial, replay
  caller-responsibility), limites (256 GiB por key/nonce, nao
  constant-time em tamanho de inputs), composicao com slices
  anteriores.

### Higiene cripto

- **Wipe volatile-safe** em todos os exits: OTK, state, keystream,
  Poly1305 internal state (r/s/accumulator), digest intermediario,
  len_block.
- **Fail-closed** em NULL key/nonce/tag, NULL ciphertext/plaintext
  com `pt_len > 0`, NULL aad com `aad_len > 0`, counter overflow.
- **Tag verification em tempo constante** via
  `crypt_constant_time_compare` (mesmo helper usado em
  `userdb_authenticate` desde alpha.206).

### Tests novos em `tests/test_crypt_vectors.c`

Quatro novas funcoes de teste com 30+ assertions:

1. **`test_chacha20_block_vectors`** — RFC 8439 §A.1 TC1
   (counter=0) e TC2 (counter=1) com key/nonce zero. Valida que
   counter incrementa o keystream conforme spec.
2. **`test_chacha20_encrypt_round_trip`** — encrypt + re-encrypt
   recupera plaintext (stream cipher symmetry). In-place encrypt.
   Counter overflow rejection. Len=0 no-op success. Fail-closed.
3. **`test_poly1305_vectors`** — RFC 8439 §A.3 TC1 (zero key, zero
   msg → zero tag) e TC2 (r=0, tag=s). Avalanche (msg flip → tag
   differ). Empty msg suportado. Fail-closed.
4. **`test_chacha20_poly1305_aead`** — round-trip + tampering
   rejection (ciphertext/AAD/tag flips), wrong key/nonce rejection,
   empty plaintext (AEAD-over-AAD), empty AAD support, fail-closed.

## Curso linear da entrega

```
1. AUDITORIA       →  Mapear o gap: AEAD existe SO em BearSSL TLS
                       internals. AES-XTS = confidencialidade sem MAC.
                       ↓
2. PROBLEMA        →  Falta primitiva canonica para usos fora de TLS.
                       ↓
3. DESIGN          →  4 APIs publicas com fail-closed e wipe hygiene.
                      OTK derivada do bloco 0, tag em tempo constante.
                       ↓
4. IMPLEMENTACAO   →  include/security/chacha20_poly1305.h (publica).
                      src/security/chacha20_poly1305.c (~600 LOC).
                       ↓
5. TESTES          →  tests/test_crypt_vectors.c +4 funcoes / +30 asserts.
                       ↓
6. BUILD           →  Makefile: TEST_SRCS + CAPYOS64_OBJS.
                       ↓
7. VERSAO          →  alpha.215 em version.h + VERSION.yaml.
                       ↓
8. DOCS            →  release note + master plan + architecture +
                       README + STATUS + releases/README + screenshots.
                       ↓
9. REVISAO         →  ABI publica nova, nao quebra existentes.
                      Wipe hygiene preservada.
                      Composicao com alpha.213 e alpha.214 integral.
```

## Impacto para o usuario final

- **Imediato observavel:** zero. AEAD e primitiva fundacional; sem
  callers reais em alpha.215 (callers chegarao em slices futuros).
- **Setup para mudancas futuras visiveis:**
  - **Secure messaging local** entre processos do usuario com
    autenticidade garantida (atacante nao forja mensagens entre
    apps).
  - **Container cifrado em userland** com integridade autenticada
    — diferente de AES-XTS que so cifra (e onde atacante pode
    flipar bits sem detectar).
  - **Channel binding** para futuras conexoes seguras: previne
    ataques de cross-protocol replay.
  - **Mensagens de IPC autenticadas** entre kernel e userland.
- **Fundacao para futuros**: TLS 1.3 userland real (Etapa 5) tera
  AEAD canonica disponivel; futuras containers tipo WireGuard
  ganham AEAD nativa sem depender do BearSSL.

## Impacto para a estrutura do sistema

- **Fundacao cripto completa** em `src/security/`:
  - SHA-256 ✓ (alpha.208 + alpha.209)
  - HMAC-SHA256 ✓
  - PBKDF2-SHA256 ✓
  - HKDF-SHA256 ✓ (alpha.213)
  - AES-128-XTS ✓
  - **ChaCha20-Poly1305 AEAD ✓ (alpha.215, novo)**
  - CSPRNG ✓ (alpha.214 hardened)
  - Constant-time compare ✓
  - Ed25519 (esqueleto fail-closed) ⚠️ (alpha.210)
- **Audit-friendly:** todas as primitivas crypto canonicas do
  sistema seguem o mesmo padrao (wipe hygiene volatile-safe,
  fail-closed em NULL, documentacao SECURITY no header). Auditor
  pode revisar `src/security/` como unidade coesa.
- **Independencia do BearSSL** para usos nao-TLS: BearSSL continua
  como stack TLS handshake oficial, mas qualquer uso de AEAD fora
  de TLS agora usa a implementacao nativa do CapyOS.
- **Composicao com alpha.213 e alpha.214 integral:** CSPRNG fornece
  key/nonce uniformes (256-bit + 96-bit), HKDF deriva subkeys de
  master secret + context label.
- **ABI publica nova**, aditiva. Nao quebra callers existentes.
- **Sem callers reais ainda** — esta entrega e a primitiva. Callers
  chegarao em slices subsequentes (secure messaging, container
  cifrado, future TLS userland).

## Composicao com slices anteriores

- **alpha.208**: SHA-256 ctx wipe hygiene → padrao seguido em
  Poly1305 internal state wipe.
- **alpha.209**: SHA-256 wipe em PBKDF2/HMAC → mesmo principio
  aplicado em todos os exits do AEAD (OTK, state, keystream).
- **alpha.210**: Ed25519 fail-closed → padrao seguido em
  fail-closed do AEAD em NULL/overflow.
- **alpha.213**: HKDF-SHA256 → KDF natural para derivar chaves
  ChaCha20 a partir de master secret + context label.
- **alpha.214**: CSPRNG hardened → fonte canonica para key (256
  bits) e nonce (96 bits) do AEAD.

## Limites

- **Sem callers reais ainda.** A primitiva esta disponivel para
  slices futuros. Adicionar caller real (e.g. IPC autenticado entre
  kernel e userland) e proximo passo natural.
- **Nao implementa Ed25519 real** (RFC 8032). Esse e um slice
  separado pendente desde alpha.210.
- **Nao implementa X25519** (RFC 7748). Junto com Ed25519, completa
  os 3 pilares de TLS 1.3 quando combinados com ChaCha20-Poly1305.
- **Nao destrava entregaveis pendentes da Etapa 2** (loginwindow
  GUI real, smokes).
- **Nao substitui BearSSL para TLS.** BearSSL continua sendo o
  stack TLS handshake oficial; esta primitiva e PARA usos fora
  de TLS.

## Validacao aplicada (revisao estatica)

- `include/security/chacha20_poly1305.h` linhas 1-149: APIs publicas
  documentadas.
- `src/security/chacha20_poly1305.c` linhas 1-432: implementacao
  completa.
- `tests/test_crypt_vectors.c` linhas 647-1019: 4 funcoes de teste
  com 30+ assertions.
- `Makefile` linha 414: `chacha20_poly1305.o` em CAPYOS64_OBJS.
- `Makefile` linha 1129: `chacha20_poly1305.c` em TEST_SRCS.
- ABI publica preservada (todas as APIs existentes intocadas).
- Wipe hygiene de alpha.208/209/210/213/214 preservada.
- Zero arquivos temp.

## Proximos slices sugeridos

1. **Ed25519 real (RFC 8032)** — preencher esqueleto fail-closed
   de alpha.210 com aritmetica de curva real (~1500 LOC). Habilita
   assinatura de updates real.
2. **X25519 (RFC 7748)** — key exchange. ~500 LOC. Junto com
   Ed25519 e ChaCha20-Poly1305, fecha os 3 pilares de TLS 1.3.
3. **Caller real do AEAD** — escolher: (a) container cifrado
   autenticado em userland substituindo AES-XTS isolado, (b) IPC
   autenticado entre kernel e userland, (c) secure messaging local
   entre apps.
4. **AES-GCM (NIST SP 800-38D)** — AEAD alternativa quando hardware
   AES-NI disponivel. Complementa ChaCha20-Poly1305 (que e mais
   rapido em CPUs sem AES-NI).
5. **Loginwindow GUI submit real** — caminho critico para fechar
   Etapa 2 formalmente.
