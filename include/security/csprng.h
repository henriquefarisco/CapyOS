#ifndef SECURITY_CSPRNG_H
#define SECURITY_CSPRNG_H

#include <stdint.h>
#include <stddef.h>

/*
 * CSPRNG — Cryptographically Secure Pseudo-Random Number Generator.
 *
 * Single global pool keyed by SHA-256 absorbing entropy from:
 *   - Boot: salt fixo + RDRAND (4 iterations) + TSC + TSC jitter loop
 *           + boot-marker bytes. TSC jitter loop coleta deltas micro
 *           de iteracoes de busy-wait — fornece entropia mesmo em VM
 *           sem RDRAND, atraves de variacao de tempo nao-deterministica
 *           introduzida por cache/branch predictor/scheduler.
 *   - Runtime: interrupcoes de hardware (PIT/keyboard/disk) via
 *              csprng_feed_entropy. Buffers de qualquer tamanho via
 *              csprng_feed_entropy_buffer (network packets, disk reads).
 *   - Output: cada bloco de 32 bytes emitido e tambem misturado de
 *             volta no pool — forward secrecy (output passado nao
 *             revela output futuro mesmo se atacante extrair o pool
 *             em algum momento).
 *   - Proativo: csprng_get_bytes faz reseed automatico com fontes de
 *               hardware (RDRAND + TSC + counter) a cada
 *               CSPRNG_RESEED_INTERVAL_BYTES emitidos, garantindo que
 *               sessoes longas nao operam indefinidamente em um pool
 *               estagnado.
 *   - Manual: csprng_reseed pode ser chamado por callers criticos
 *             antes de operacoes de longa duracao (key generation,
 *             TLS handshake).
 *
 * Threat model: O atacante NAO consegue:
 *   - prever output futuro mesmo conhecendo todo output passado
 *     (forward secrecy via output feedback no pool)
 *   - recuperar output passado mesmo extraindo o pool atual
 *     (backward secrecy via SHA-256 one-way)
 *   - distinguir output do CSPRNG de bytes verdadeiramente aleatorios
 *     em tempo polynomial (SHA-256 assumption)
 *
 * Limites conhecidos:
 *   - Em VM hostil sem RDRAND e com TSC virtualizado para zero, o boot
 *     se reduz ao salt fixo + boot-marker — atacante com mesmo binario
 *     poderia replicar o pool inicial. Mitigacao parcial: TSC jitter
 *     loop continua produzindo entropia residual via cache effects.
 *     Mitigacao completa exige fonte real (HW RNG, hypercall, fonte
 *     externa) que nao esta no escopo desta camada.
 *   - Reseed proativo NAO substitui interrupt-time entropy feeding;
 *     ambos contribuem. Sem callers de feed_entropy, o pool ainda
 *     funciona mas converge para um espaco de estados mais previsivel
 *     a longo prazo (mitigado pelo reseed proativo).
 */

/* Reseed automatico a cada 64 KiB emitidos. Valor empirico que
 * balanceia custo (uma chamada a RDRAND/TSC por 64 KiB e desprezivel)
 * vs frescor (limita janela de comprometimento se pool for parcialmente
 * inferido). Linux usa 4 MiB; FreeBSD usa 1 MiB; OpenBSD reseed por
 * tempo (5 min). 64 KiB e conservador. */
#define CSPRNG_RESEED_INTERVAL_BYTES (64u * 1024u)

void csprng_init(void);

/*
 * Adiciona 4 bytes de entropia ao pool. Designed para ISRs (PIT,
 * keyboard, disk) onde o evento gera um valor pequeno (tick counter,
 * scancode, sector number). Lock-free com respeito a outros ISRs
 * (irq_save/restore protege contra preemption).
 */
void csprng_feed_entropy(uint32_t data);

/*
 * Adiciona um buffer arbitrario ao pool. Usar para fontes de buffer
 * (network packet content, disk read buffer, audio frame). O CSPRNG
 * NAO conta entropia — assume que o caller passa dados com alguma
 * entropia (mesmo dados parcialmente preditiveis contribuem via
 * SHA-256 mixing, nao prejudicam).
 */
void csprng_feed_entropy_buffer(const void *data, size_t len);

/*
 * Forca reseed do pool com fontes de hardware (RDRAND + TSC + boot
 * counter). Para callers que vao realizar operacoes criticas de longa
 * duracao (key generation, TLS handshake, master key derivation) e
 * querem garantir que o pool foi alimentado com entropia fresca antes
 * do trabalho. csprng_get_bytes ja chama isso automaticamente a cada
 * CSPRNG_RESEED_INTERVAL_BYTES emitidos.
 */
void csprng_reseed(void);

/*
 * Preenche o buffer com bytes aleatorios criptograficamente seguros.
 * Pode bloquear chamadas concorrentes via irq_save (uniprocessador).
 * Aciona reseed automatico quando o contador de bytes emitidos cruza
 * o intervalo de reseed.
 */
void csprng_get_bytes(void *buf, size_t len);

uint32_t csprng_next_u32(void);

/* Compatibility alias used by newer kernel modules. */
static inline void csprng_fill(void *buf, size_t len) {
  csprng_get_bytes(buf, len);
}

#endif
