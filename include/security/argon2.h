#ifndef SECURITY_ARGON2_H
#define SECURITY_ARGON2_H

#include <stddef.h>
#include <stdint.h>

/*
 * Argon2id (RFC 9106): password hashing memory-hard.
 *
 * Argon2 e o vencedor do Password Hashing Competition (2015) e foi
 * padronizado em RFC 9106 em 2021. E o algoritmo recomendado pela
 * OWASP, NIST SP 800-63B (rascunho 2024), e a maioria das auditorias
 * cripto modernas para password hashing.
 *
 * Tres variantes: Argon2d (data-dependent indexing, mais rapido mas
 * vulneravel a side-channels), Argon2i (data-independent, resistente
 * a side-channels mas vulneravel a ranking-tradeoff), Argon2id
 * (hibrido: primeira metade da pass 0 e data-independent, depois
 * data-dependent). **Argon2id e a variante recomendada para password
 * hashing** — a unica implementada aqui no CapyOS.
 *
 * Comparacao com PBKDF2-SHA256 (atual default em src/security/crypt.c):
 *
 *  | Aspecto              | PBKDF2-SHA256        | Argon2id            |
 *  |----------------------|----------------------|---------------------|
 *  | Memory cost          | O(1) ~constante      | O(m_cost * 1 KiB)   |
 *  | GPU/ASIC speedup     | 1000-10000x          | <10x (memory wall)  |
 *  | TMTO resistance      | Nenhuma              | Forte (Argon2id)    |
 *  | Side-channel         | Constant-time        | Mixed (id hibrido)  |
 *  | Standard             | RFC 8018             | RFC 9106            |
 *  | Configurabilidade    | Apenas iteracoes     | t, m, p separados   |
 *
 * Em alpha.218 esta primitiva e a fundacao para migracao gradual de
 * PBKDF2-SHA256 para Argon2id em userdb (`src/auth/user.c`). A
 * migracao em si nao acontece neste slice (compatibilidade com
 * databases existentes) — esta entrega adiciona a primitiva auditavel.
 *
 * Modelo de ameacas:
 *
 *  - **Brute-force massivo em GPU/ASIC**: Argon2id forca atacante a
 *    alocar m_cost * 1024 bytes por candidate test. Com m_cost = 65536
 *    (64 MiB), um ASIC com 1 GB de memoria avalia <= 16 candidates em
 *    paralelo (vs >10000 para PBKDF2-SHA256).
 *  - **TMTO (time-memory tradeoff)**: Argon2 e projetado para que
 *    reduzir memoria em fator k aumente tempo em fator >= k^2 (ate
 *    k = sqrt(m_cost)). Argon2id estende isso para o caso hibrido.
 *  - **Side-channel timing**: a primeira metade da pass 0 e
 *    data-independent (resistente a cache-timing); a segunda metade e
 *    data-dependent (cache-friendly mas com leak parcial). Para
 *    threats com acesso a cache observation (process-level attacker),
 *    use Argon2i puro — Argon2id assume threat model server-side.
 *
 * Parametros recomendados (OWASP 2024, RFC 9106 §4):
 *
 *  - **t_cost (time/iterations)**: >= 2 (RFC §4 recomenda 3 para
 *    high-security; pode usar 1 se m_cost >= 2 GiB).
 *  - **m_cost (memory em KiB)**:
 *    - Servidor potente: 65536 (64 MiB) ou mais.
 *    - Servidor moderado: 19456 (19 MiB) per OWASP.
 *    - Constrained device (login local CapyOS): 8192 (8 MiB) e o
 *      minimo defensavel; abaixo disso GPU speedup volta.
 *  - **parallelism**: 1 (fixado nesta implementacao; multi-lane fica
 *    para slice futuro se necessario).
 *
 * Limites desta implementacao:
 *
 *  - **parallelism = 1 fixo.** Multi-lane requer thread sync que nao
 *    ha valor no contexto CapyOS atual (passwords sao verificados
 *    serialmente). RFC 9106 permite parallelism = 1 explicitamente.
 *  - **memory e caller-provided** (sem malloc interno). Caller fornece
 *    buffer de m_cost * 1024 bytes. Permite uso em stack/static/heap
 *    sem acoplamento.
 *  - **Sem suporte a Argon2d ou Argon2i puros.** Apenas Argon2id.
 *  - **Sem suporte a "secret key" K** (associated authentication key)
 *    nem "associated data" X (RFC §3.1). Ambos sao length-zero.
 *
 * Caller responsibility:
 *
 *  - Salt **deve ser unico por password** e gerado via CSPRNG
 *    (src/security/csprng.c). RFC 9106 §3.1 recomenda 16+ bytes
 *    aleatorios; minimo absoluto 8 bytes.
 *  - Memory buffer **deve ser zerado pelo caller apos a chamada** se
 *    contiver material sensivel (esta primitiva nao limpa
 *    automaticamente para permitir reuse em loops).
 *  - Password buffer **nao** e wipeado por esta primitiva — caller
 *    e responsavel por limpar via memory_zero apos o hash.
 */

#define ARGON2_BLOCK_SIZE      1024u    /* bytes per memory block */
#define ARGON2_VERSION_13      0x13u    /* Argon2 v1.3 (RFC 9106) */
#define ARGON2_TYPE_ID         2u       /* Argon2id */
#define ARGON2_SYNC_POINTS     4u       /* slices per lane */
#define ARGON2_ADDRESSES_IN_BLOCK 128u  /* J1||J2 pairs per address block */

/* Limites operacionais (validados em argon2id_hash) */
#define ARGON2_MIN_OUT_LEN     4u
#define ARGON2_MIN_SALT_LEN    8u
#define ARGON2_MIN_T_COST      1u
#define ARGON2_MIN_M_COST      8u       /* >= 8 * parallelism = 8 (p=1) */

/*
 * Argon2id one-shot password hash.
 *
 * Parametros:
 *
 *  - password, password_len: input password. password_len pode ser 0;
 *    NULL password e aceito apenas se password_len == 0.
 *  - salt, salt_len: salt unico por password; salt_len >= 8 bytes.
 *  - t_cost: numero de iteracoes (passes) sobre o memory matrix.
 *    Minimo 1.
 *  - m_cost: memoria em KiB. Minimo 8.
 *  - memory, memory_len: buffer de trabalho. Deve ter pelo menos
 *    m_cost * 1024 bytes. Caller possui o buffer e e responsavel por
 *    zerar apos a chamada se desejado.
 *  - out, out_len: buffer de saida. out_len >= 4 bytes.
 *
 * Retorna 0 em sucesso, -1 em qualquer parametro invalido (NULL onde
 * obrigatorio, comprimentos fora de range, memory_len insuficiente).
 *
 * Wipe hygiene interna:
 *
 *  - H0 (pre-hash), H' state, blocos intermediarios da compressao,
 *    address_block + input_block + zero_block (data-independent path),
 *    final_block: todos wipeados antes de retornar (sucesso ou erro).
 *  - O memory buffer NAO e wipeado automaticamente — caller decide
 *    quando zerar (pode querer reusar para outra password sem perder
 *    o malloc).
 *  - password buffer do caller NAO e wipeado — caller responsabilidade.
 */
int argon2id_hash(const uint8_t *password, size_t password_len,
                  const uint8_t *salt, size_t salt_len,
                  uint32_t t_cost, uint32_t m_cost,
                  uint8_t *memory, size_t memory_len,
                  uint8_t *out, size_t out_len);

#endif /* SECURITY_ARGON2_H */
