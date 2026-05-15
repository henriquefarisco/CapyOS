#ifndef SECURITY_CHACHA20_POLY1305_H
#define SECURITY_CHACHA20_POLY1305_H

#include <stddef.h>
#include <stdint.h>

/*
 * ChaCha20-Poly1305 AEAD (RFC 8439).
 *
 * Implementacao canonica do CapyOS, audit-friendly, alinhada com a
 * higiene criptografica do resto do projeto (wipe volatile-safe,
 * fail-closed em todos os bounds, NULL graceful nos extremos
 * razoaveis). Independente do TLS stack do BearSSL — esta API serve
 * usos fora de TLS (secure messaging local, key wrapping, channel
 * binding, mensagens de IPC autenticadas, futuras containers cifrados
 * em userland).
 *
 * Modelo de ameacas:
 *  - Atacante NAO consegue decifrar o plaintext sem a chave
 *    (confidencialidade da ChaCha20).
 *  - Atacante NAO consegue forjar ciphertext ou AAD aceito como valido
 *    sem a chave (integridade autenticada via Poly1305).
 *  - Atacante NAO consegue replay um mesmo (nonce, key) com plaintext
 *    diferente sem detectar (mas o caller e responsavel por nao
 *    reutilizar nonce com a mesma chave — reutilizacao quebra
 *    confidencialidade catastroficamente).
 *  - Atacante NAO distingue ciphertext de bytes aleatorios uniformes
 *    em tempo polynomial (PRF assumption da ChaCha20).
 *
 * Limites:
 *  - Nonce de 96 bits e caller-responsibility: deve ser unico por
 *    (key). 12 bytes random do CSPRNG fornece ~2^48 nonces antes do
 *    risco de colisao birthday — adequado para sessoes; insuficiente
 *    para longa-duracao com bilhoes de mensagens.
 *  - Maximum plaintext: 2^32 * 64 bytes = 256 GiB por (key, nonce).
 *    A API rejeita inputs maiores via overflow do counter.
 *  - Esta implementacao NAO e constant-time em relacao ao tamanho dos
 *    inputs (loops iteram sobre len), apenas em relacao aos bytes
 *    secretos. Aceitavel pelo threat model padrao; inputs com sizes
 *    confidenciais devem aplicar padding em uma camada superior.
 *
 * Composicao com slices anteriores:
 *  - alpha.213 (HKDF-SHA256): KDF natural para derivar chaves
 *    ChaCha20 a partir de master secret + context label.
 *  - alpha.214 (CSPRNG): fonte canonica para a chave (256 bits) e
 *    para o nonce (96 bits).
 */

#define CHACHA20_KEY_SIZE        32u
#define CHACHA20_NONCE_SIZE      12u
#define CHACHA20_BLOCK_SIZE      64u
#define POLY1305_KEY_SIZE        32u
#define POLY1305_TAG_SIZE        16u
#define CHACHA20_POLY1305_TAG_SIZE 16u

/*
 * ChaCha20 block function (RFC 8439 §2.3). Gera 64 bytes de keystream
 * a partir de (key, counter, nonce). Util para derivar Poly1305 OTK
 * via counter=0 e para testes contra os vetores do RFC 8439 §2.3.2.
 * Nao zera a stack alem do necessario — caller deve tratar `out` como
 * sensivel.
 *
 * Retorna 0 em sucesso, -1 se algum input obrigatorio for NULL.
 */
int chacha20_block(const uint8_t key[CHACHA20_KEY_SIZE],
                   uint32_t counter,
                   const uint8_t nonce[CHACHA20_NONCE_SIZE],
                   uint8_t out[CHACHA20_BLOCK_SIZE]);

/*
 * ChaCha20 stream cipher (RFC 8439 §2.4). Cifra/decifra `len` bytes
 * de `in` para `out` via XOR com keystream(key, counter..counter+N).
 * `in` e `out` podem ser identicos (in-place). Counter inicial e
 * `initial_counter` (RFC sugere 1 para mensagens AEAD; 0 para
 * derivar Poly1305 OTK).
 *
 * Fail-closed: NULL key/nonce/in/out com len > 0 retornam -1.
 * Counter overflow (initial_counter + ceil(len/64) > 2^32) retorna
 * -1 sem cifrar — previne reuso silencioso de keystream blocks que
 * tornaria duas porcoes da mesma mensagem usaveis para keystream
 * recovery via XOR.
 *
 * Wipe hygiene: blocos de keystream temporarios sao zerados antes
 * do retorno (sucesso ou erro).
 */
int chacha20_encrypt(const uint8_t key[CHACHA20_KEY_SIZE],
                     uint32_t initial_counter,
                     const uint8_t nonce[CHACHA20_NONCE_SIZE],
                     const uint8_t *in, uint8_t *out, size_t len);

/*
 * Poly1305 one-time MAC (RFC 8439 §2.5). Computa tag de 16 bytes
 * sobre `msg` (msg_len bytes) usando one-time key `otk` (32 bytes).
 *
 * CRITICO: `otk` deve ser usado para EXATAMENTE UMA mensagem.
 * Reutilizar `otk` para duas mensagens diferentes permite forjamento
 * trivial via aritmetica polinomial — Poly1305 NAO e MAC reutilizavel.
 * O wrapper AEAD garante isso derivando OTK fresca por nonce.
 *
 * Fail-closed: NULL otk/tag retorna -1.
 * msg_len=0 e valido (tag de mensagem vazia).
 *
 * Wipe hygiene: r/s/accumulator sao zerados antes do retorno.
 */
int poly1305_mac(const uint8_t otk[POLY1305_KEY_SIZE], const uint8_t *msg,
                 size_t msg_len, uint8_t tag[POLY1305_TAG_SIZE]);

/*
 * ChaCha20-Poly1305 AEAD encrypt (RFC 8439 §2.8). Cifra `plaintext`
 * (pt_len bytes) com (key, nonce), produzindo `ciphertext` (mesmo
 * tamanho) e `tag` (16 bytes). `aad` (aad_len bytes) e dado
 * adicional autenticado mas nao cifrado (e.g. cabecalho de pacote).
 *
 * Ordem de operacoes (per RFC):
 *  1. OTK = primeiros 32 bytes de ChaCha20-block(key, counter=0, nonce).
 *  2. ciphertext = ChaCha20-encrypt(key, counter=1, nonce, plaintext).
 *  3. tag = Poly1305(OTK, aad || pad16(aad) || ct || pad16(ct) ||
 *           u64le(aad_len) || u64le(pt_len)).
 *
 * `ciphertext` pode ser igual a `plaintext` (in-place).
 *
 * Fail-closed: NULL key/nonce/tag, ou NULL ciphertext/plaintext com
 * pt_len > 0, ou NULL aad com aad_len > 0, retornam -1 sem efeito
 * observavel. Mesmas condicoes de overflow do counter da ChaCha20.
 *
 * Wipe hygiene: OTK e zerada antes do retorno (sucesso e erro).
 */
int chacha20_poly1305_encrypt(const uint8_t key[CHACHA20_KEY_SIZE],
                              const uint8_t nonce[CHACHA20_NONCE_SIZE],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *plaintext, size_t pt_len,
                              uint8_t *ciphertext,
                              uint8_t tag[CHACHA20_POLY1305_TAG_SIZE]);

/*
 * ChaCha20-Poly1305 AEAD decrypt (RFC 8439 §2.8). Verifica `tag`
 * sobre (aad || ciphertext) em tempo constante, e — se valido —
 * decifra para `plaintext`. Se invalido, NAO escreve em `plaintext`
 * (mantem buffer intacto) e retorna -1.
 *
 * `plaintext` pode ser igual a `ciphertext` (in-place) — mas neste
 * caso o caller deve aceitar que em falha de auth o buffer fica
 * indefinido (provavelmente o ciphertext original, ja que a
 * implementacao verifica o tag ANTES de decifrar).
 *
 * Retorna 0 se tag valido e decifrou, -1 caso contrario (tag
 * invalido, NULL inputs obrigatorios, overflow do counter).
 *
 * Wipe hygiene: OTK e tag computada sao zerados em todos os
 * caminhos.
 */
int chacha20_poly1305_decrypt(const uint8_t key[CHACHA20_KEY_SIZE],
                              const uint8_t nonce[CHACHA20_NONCE_SIZE],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *ciphertext, size_t ct_len,
                              const uint8_t tag[CHACHA20_POLY1305_TAG_SIZE],
                              uint8_t *plaintext);

#endif /* SECURITY_CHACHA20_POLY1305_H */
