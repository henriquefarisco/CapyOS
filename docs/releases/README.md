# Release Notes do CapyOS

Indice das release notes mantidas no repositorio.

## Release atual

- `capyos-0.8.0-alpha.242+20260520.md`
  - Corrige o fluxo de download do indice de modulos via redirect em GitHub Releases, preserva staging de payload em `/var/capypkg/updates` e destrava o bootstrap remoto do primeiro boot.

## Historico recente

- `capyos-0.8.0-alpha.241+20260519.md`
  - Higienizacao end-to-end: wizard interativo de primeiro boot com selecao de modulos + progress, sources de desktop session/window/apps migrados para CapyUI 0.7.0 (build cross-repo via CAPYUI_DIR + PROFILE=full|core-only), activation gate de modulos, comando `capy` unificado, installer UEFI minimalista (so disco + chave de recovery), silent provisioning eliminado.

- `capyos-0.8.0-alpha.240+20260519.md`
  - Circuito modular de instalacao e deploy: install profile, bootstrap capypkg, comando `pkg-bootstrap`, target `make modules-index` e formato de pacote dos repos externos.

- `capyos-0.8.0-alpha.239+20260519.md`
  - Adapter `services/capypkg` consolidado in-tree como fronteira de recepcao da Etapa 9 (4 TUs runtime + 9 comandos `pkg-*` + 28 testes), com gate de printable-ASCII propagado tambem para `update_agent_parse.c`, `http_parse_url` e `http_store_headers` (fecha terminal-escape injection e HTTP request smuggling em todas as trilhas atuais de input externo).

- `capyos-0.8.0-alpha.238+20260515.md`
  - Publicacao visual CapyUI v1.1 no README raiz e remocao da marca retangular decorativa do wallpaper do desktop.

- `capyos-0.8.0-alpha.237+20260514.md`
  - Gate externo `mouse-events` com marker serial `[smoke] mouse-events ready`, alvo `smoke-x64-vmware-mouse-events` e evidencias de release exigindo DHCP + `gui-session` + `mouse-events`.
- `capyos-0.8.0-alpha.236+20260514.md`
  - Gate externo `gui-session` com marker serial `[smoke] gui-session ready`, alvo `smoke-x64-vmware-gui-session` e evidencias de release amarradas ao gate oficial VMware + UEFI + E1000.
- `capyos-0.8.0-alpha.231+20260514.md`
  - Recovery operacional em `volume_provider_rekey_execute_rollback_step` e `volume_provider_rekey_execute_cleanup_scratch`; rollback/abort antes do commit e limpeza verificada do scratch pos-commit.
- `capyos-0.8.0-alpha.230+20260514.md`
  - Commit final guardado em `volume_provider_rekey_execute_commit_header`; grava LBA0 por ultimo, verifica read-back, abre header-managed e marca checkpoint COMPLETED.
- `capyos-0.8.0-alpha.229+20260514.md`
  - Copy/re-encrypt reverso incremental em `volume_provider_rekey_execute_copy_step`; copia um bloco por chamada para o dominio Argon2id alvo, verifica plaintext e atualiza checkpoint+manifest sem comitar LBA0.
- `capyos-0.8.0-alpha.228+20260514.md`
  - Staging criptografico do header alvo em `volume_provider_rekey_execute_stage_header`; persiste checkpoint + header Argon2id + manifest no scratch e verifica por read-back/parse/CRC/tag sem copiar blocos nem comitar LBA0.
- `capyos-0.8.0-alpha.227+20260514.md`
  - Executor checkpoint-write guardado em `volume_provider_rekey_execute_checkpoint`; grava somente o checkpoint no scratch com flag explicita, verifica por read/parse e bloqueia copia/recriptografia destrutiva.
- `capyos-0.8.0-alpha.226+20260514.md`
  - Contrato persistente de checkpoint em `volume_provider_rekey_checkpoint_{init,serialize,parse}`; record little-endian de 128 bytes com CRC32, reserved-zero e validacao semantica de progresso para resume/rollback/abort seguro.
- `capyos-0.8.0-alpha.225+20260514.md`
  - Executor transacional guardado/dry-run em `volume_provider_rekey_execute`; valida o planner, reporta fases checkpoint/copia reversa/commit/verify e recusa writes reais ou flags desconhecidas com `WRITES_DISABLED` ate existir executor write-enabled com checkpoint persistente auditado.
- `capyos-0.8.0-alpha.224+20260514.md`
  - Planner transacional read-only em `volume_provider_rekey_plan`; calcula no-op moderno, blocked shrink, blocked scratch e READY com copia reversa, scratch LBA, ranges source/target e estimativas de I/O conservadoras para futura migracao legacy -> header-managed.
- `capyos-0.8.0-alpha.223+20260514.md`
  - Preflight seguro/read-only de re-key/migracao legacy em `volume_provider_rekey_preflight`; bloqueia escrita destrutiva de header sobre CAPYFS LBA0 e define relocation/re-encryption/geometria como proximo motor transacional.
- `capyos-0.8.0-alpha.221+20260514.md`
  - On-disk volume header module (`include/security/volume_header.h` + `src/security/volume_header.c` ~290 + ~620 LOC) com algorithm marker + per-volume random salt + HMAC-SHA256 check tag + CRC32 fast bit-rot gate; fecha o gap conhecido desde alpha.218 ('Volume key derivation continua PBKDF2 + g_disk_salt hardcoded') e prepara o consumo da primitiva `crypt_derive_xts_keys_argon2id` entregue em alpha.220 ate entao sem caller real. Define struct on-disk fixa de 512 bytes: magic 'CAPYVHDR' little-endian, version, flags, kdf_algo_id (PBKDF2/Argon2id), t_cost, m_cost, salt_len, kdf_salt[64], data_offset_lba (>=1), reserved_lba_count, kdf_check_tag[32] = HMAC-SHA256(key1||key2, 'CAPYOS-VOL-HDR-CHECK-v1' || prefix[0..104]), creation_timestamp_ns, creator_version[32] null-padded, reserved[332] zerado, header_crc32 (IEEE 802.3 reflected). vh_serialize_prefix e autoridade unica do layout dos primeiros 104 bytes (usada por serializer e por compute_check_tag — impede drift entre o que vai a disco e o que e autenticado). Endianness little-endian explicito via byte stores. CRC32 no-table branchless ~10 LOC. vh_validate_params strict: PBKDF2 exige t_cost>=1000 e m_cost==0; Argon2id exige t_cost>=1 e m_cost>=8 (RFC 9106 §3.1); salt_len em [8,64]; data_offset_lba>=1; reserved_lba_count em [1, data_offset_lba]. Parse fail-safe: wipe out struct ANTES de qualquer validacao, depois CRC -> magic -> version (barato) -> flags==0 -> params -> reserved-all-zero (caro). Threat model two-tier documentado inline: header_crc32 e bit-rot gate fast NAO seguranca (atacante recomputa trivialmente); kdf_check_tag e o binding criptografico — atacante que altera salt/algo/t_cost/m_cost forca user a derivar chave diferente e o recomputo do HMAC nao bate. capyos_volume_header_derive_keys dispatcha PBKDF2/Argon2id conforme algo_id, verifica tag, wipe key1/key2 em qualquer falha. NAO distingue 'wrong password' de 'tampered header' no retorno publico (ambos ERR_CHECK_TAG) — evita oracle. Tests host-side em tests/test_volume_header.c: 13 funcoes ~70 assertions cobrindo CRC32 KAT (RFC 3309 '', 'a' -> 0xE8B7BE43, '123456789' -> 0xCBF43926, NULL guard, zero-buffer sem null-stop), init happy paths PBKDF2 e Argon2id, init fail-closed 13 vetores, serialize/parse roundtrip com endianness explicit (bytes 0..7 -> 'CAPYVHDR'), parse fail-closed (magic/version/CRC/algo/flags/reserved tampered), looks_valid quick gate, derive success em ambos KDFs com k1!=k2 anti-split, wrong password com sentinela 0xA5 wipe, tampered salt rejeitado com password correto, algo downgrade attempt rejeitado. Wiring: Makefile CAPYOS64_OBJS + TEST_SRCS; test_runner.c declara run_volume_header_tests. NAO altera installer/boot path nesta release (alpha.222 fara o write-side wiring); legacy volumes pre-alpha.222 continuam funcionando porque nada le o header ainda. Composicao integral com alpha.220 (crypt_derive_xts_keys_argon2id backend), alpha.218 (argon2id_hash primitiva), alpha.214 (CSPRNG futuro fornecera salt em alpha.222), alpha.209 (sha256_clear hygiene propaga para vh_compute_tag_internal via crypt_hmac_sha256). ABI publica preservada — todos os simbolos novos sao aditivos. Mapa de entrega linear: ALPHA.221 (esta release) primitiva entregue, testada, audit-friendly + ALPHA.222 (planejado) installer escreve header no LBA 0 raw + filesystem em LBA 1+ via block_offset_wrap, boot path tenta header read primeiro com fallback para legacy PBKDF2 em volumes pre-alpha.222 + ALPHA.223 (planejado) ferramenta de re-keying in-place de volumes legacy.
- `capyos-0.8.0-alpha.220+20260514.md`
  - Implicit re-hash on successful auth + Argon2id volume-key derivation primitive. Fecha os dois ultimos limites residuais documentados (alpha.219 timing leak transicional PBKDF2 vs Argon2id; alpha.218 volume key derivation continua PBKDF2). src/auth/user.c refatorado: userdb_replace_password_hash extraido como helper privado (logica de read-modify-write do /etc/users.db com salt fresco csprng_get_bytes(16) + Argon2id derivation via user_password_hash_derive); userdb_set_password vira wrapper fino que aplica auth_policy_validate_password e delega. userdb_authenticate, depois de auth_ok=1 com user_found=1 e rec.algo_id != USER_PASSWORD_ALGO_ARGON2ID, chama (void)userdb_replace_password_hash(username, password) — re-deriva com USER_ARGON2ID_T_COST=3/M_COST=8192 KiB, serialize escolhe 10-field schema, write_blob persiste atomicamente. Fail-silent: allocation/FS error nao bloqueia auth ja bem-sucedida — record stays on PBKDF2, retry no proximo login. Threat model self-heals: population de PBKDF2 records monotonically shrinks toward zero conforme contas autenticam; residual leak restrito a 'contas que nunca logaram desde alpha.220 deployment'. Primary auth path timing inalterado para contas Argon2id (~200ms); contas legacy pagam ~250ms apenas no primeiro login pos-upgrade (50ms verify + 200ms rehash + ~5ms FS write), depois convergem para ~200ms. Volume key primitive: include/security/crypt.h adiciona crypt_derive_xts_keys_argon2id(password, salt, salt_len, t_cost, m_cost, key1[32], key2[32]) + CRYPT_VOLUME_ARGON2ID_T_COST=3 / CRYPT_VOLUME_ARGON2ID_M_COST=8192 (reaproveita budget de 8 MiB do kernel heap). src/security/crypt.c implementa caller-allocates via kalloc(m_cost*1024) -> argon2id_hash com out_len=64 -> split key1[0..32]+key2[32..64], wipe volatile-safe do work memory antes de kfree, wipe do scratch derived[64]. Fail-closed first: key1/key2 wipeados a zero no inicio (sentinela 'no key here' inequivoca); rejeita t_cost<1, m_cost<8, salt_len<8 per RFC 9106 §3.1. Callers em producao (installer, key_storage_probe, kernel.c) ficam em PBKDF2 ate o slice futuro de header de volume com algorithm marker landar — primitiva entregue mas nao consumida nesta release. Tests novos: test_crypt_derive_xts_keys_argon2id em tests/test_crypt_vectors.c com 11 assertions (determinismo, key1!=key2 split, salt sensitivity, fail-closed NULL/t_cost=0/m_cost=7/salt_len=7, wipe forensics sentinela 0xA5 -> 0x00, non-collision com PBKDF2). ABI publica preservada (nenhuma quebra). Composicao integral com alpha.219 (mesmo dispatcher user_password_hash_derive), alpha.218 (argon2id_hash direto), alpha.214 (CSPRNG), alpha.212 (timing-equalised lockout — wrapper de policy preserva ordem), alpha.207, alpha.206.
- `capyos-0.8.0-alpha.219+20260514.md`
  - Argon2id (RFC 9106) entra em producao no userdb. Dispatcher novo em src/auth/user_password_hash.{c,h} (~190 LOC + 105 LOC) decoupla user.c das primitivas crypto e permite teste host-side; user_record_init e userdb_set_password sempre emitem Argon2id (t_cost=3, m_cost=8192 KiB) via user_password_hash_derive — toda conta criada ou que troca senha no alpha.219+ ja sai memory-hard. userdb_authenticate dispatcha conforme rec.algo_id (PBKDF2 legacy continua aceito sem migracao de DB); usuario desconhecido roda Argon2id com k_userdb_dummy_salt + USER_ARGON2ID_T_COST/M_COST para equalizacao com a nova baseline (preserva mitigacao de user enumeration timing de alpha.206). Schema /etc/users.db: 7 campos (PBKDF2 legacy) ou 10 campos com trailer ':argon2id:t_cost:m_cost' — parser aceita ambos, serializer escolhe por algo_id (downgrade transparente para binarios pre-alpha.219 sem migracao reversa). Argon2id work memory (8 MiB = 50% do kernel heap) alocado via kalloc, wipeado volatile-safe e liberado a cada derivacao; fail-closed em alocacao/parametros/NULL — hash_out wipeado a zero em todos os caminhos de erro. struct user_record cresce 9 bytes append-only (algo_id+t_cost+m_cost no FINAL) — 27 callsites existentes compilam unchanged. Testes host-side novos em tests/test_user_password_hash.c (run_user_password_hash_tests): 30 assertions em 6 test functions cobrindo algo string canonical, PBKDF2 legacy round-trip com t_cost=0 mapeando para USER_ITERATIONS, Argon2id round-trip + nao-colisao com PBKDF2, sensibilidade parameter threading a salt/t_cost/m_cost (anti-regressao), fail-closed derive (NULL/parametros invalidos/algo desconhecido) e verify (NULL stored/oversize). Composicao com alpha.218 (Argon2id primitiva) integral. Leak transicional (~50ms PBKDF2 vs ~200ms Argon2id) vazia apenas idade aproximada da ultima troca de senha — sera eliminado por slice futuro de implicit re-hash on successful auth. ABI publica preservada para todos os callers existentes.
- `capyos-0.8.0-alpha.218+20260514.md`
  - Argon2id (RFC 9106) + BLAKE2b (RFC 7693) memory-hard password hashing nativo do CapyOS em src/security/argon2.c (~600 LOC) e src/security/blake2b.c (~270 LOC); fecha o gap fundamental contra brute-force massivo em GPU/ASIC que afeta PBKDF2-SHA256 (speedup 1000-10000x); Argon2id e vencedor do Password Hashing Competition (2015), recomendado por OWASP e NIST SP 800-63B; APIs publicas blake2b_init/update/final/wipe + one-shot blake2b() com keyed mode HMAC-like ate 64-byte key, lazy compression, param block per RFC §2.5, IV/sigma/rotations identicas a SHA-512; argon2id_hash(password, salt, t_cost, m_cost, memory, memory_len, out, out_len) com caller-provided memory buffer (sem malloc kernel — flexivel para stack/static/heap), parallelism=1 fixo, sem secret/AD; pre-hash H0 com p/T/m/t/version=0x13/type=2/pwd/salt/zero+zero; variable-length H' per §3.3 com BLAKE2b iterado; G compression 1024-byte com fBlaMka(x,y) = x+y+2*(x_lo*y_lo) substituindo soma simples (acrescenta mul 32x32->64 que aumenta cost-per-op em ASIC); matrix 8x8 de registers 16-byte com P row-wise e column-wise; address block generation per §3.4.1.1 com input_block {pass, lane, slice, m', t_cost, type, counter}, address_block = G(zero, G(zero, input_block)); Argon2id mode hybrid (data-indep slice 0-1 pass 0, data-dep resto) per §3.4; index alpha com J1^2 mapping nao-uniforme + start_pos exclude segment corrente pass>0; finalize p=1 com final_block = B[lane_length-1]; wipe volatile-safe em H0, V chains H', blocos prev/ref/new/existing, input_block, address_block, zero_block, final_block; tests 7 funcoes em test_crypt_vectors.c (RFC 7693 Appendix A 'abc', empty, multiblock fox, streaming vs oneshot cruzando 128/256, variable output 16/32/64 distintos, keyed mode HMAC-like, fail-closed em NULL/limites) + test_argon2id_smoke (determinismo, sensibilidade password/salt/t/m/out_len, fail-closed em NULL salt/curto/t=0/m=7/memory curto/out<4/NULL out/NULL memory); PBKDF2-SHA256 preservado para backward compat com userdb existente; fundacao cripto canonica CapyOS completa (11 primitivas modernas em src/security/)
- `capyos-0.8.0-alpha.217+20260513.md`
  - Ed25519 (RFC 8032) implementacao REAL substituindo esqueleto fail-closed que vinha desde alpha.210; update verifier (src/services/update_agent.c) oficialmente OPERACIONAL pela primeira vez — manifests assinados com chave canonica em producao agora aceitos quando criptograficamente validos; field arithmetic GF(2^255-19) refatorado em modulo compartilhado include/security/fe25519.h + src/security/fe25519.c (extraido de x25519.c, +fe_pow22523/fe_neg/fe_cmov/fe_isnegative/fe_iszero/fe_notequal), agora consumido por X25519 + Ed25519; APIs publicas ed25519_create_keypair + ed25519_sign (deterministico) + ed25519_verify (com cofator 8 per RFC §5.1.7); group ops twisted Edwards extended (ge_dbl dbl-2008-hwcd, ge_add add-2008-hwcd-3, ge_neg_p, ge_cmov constant-time); scalar mult double-and-add constant-time (256 doubles + cmov); encoding/decoding compressed com canonicality check; scalar arithmetic mod L via sc_reduce64 + sc_muladd (porte ref10 signed 21-bit limbs) + sc_is_canonical; constants ED_D/ED_D2/ED_SQRTM1/ED_B_X/ED_B_Y/ED_B_T/ED_L_BYTES verificadas vs dalek-cryptography (5x51-bit limbs); wipe volatile-safe em todos os fe/ge/scalars/SHA-512 contexts intermediarios; threat model documentado (cofator 8 anti-torsion, S<L anti-malleability, canonicality check em decode); tests reformulados com 3 vetores oficiais RFC 8032 §7.1 + tampering rejection (sig[0]/sig[32]) + wrong-pk + non-canonical S (S=L, S>L) + NULL + round-trip + determinism + tamper-message; fundacao cripto canonica CapyOS completa (10 primitivas modernas em src/security/)
- `capyos-0.8.0-alpha.216+20260513.md`
  - X25519 (RFC 7748) ECDH canonico em src/security/x25519.c: primeira primitiva de key exchange nativa do CapyOS, independente do TLS stack BearSSL e usavel fora de TLS (key agreement local, futuros canais WireGuard-like, secure boot key exchange, fundacao para TLS 1.3 userland na Etapa 5); APIs publicas x25519(scalar, u_coord, shared) e x25519_base(scalar, public_key); implementacao constant-time via Montgomery ladder de 255 iteracoes; field arithmetic em representacao 5x51-bit limbs com __uint128_t multiplication; scalar clamping per RFC 7748 §5; u-coord top-bit masking per §5; small-subgroup detection per §6.1 (rejeita shared=0 fail-closed); fe_invert via cadeia ref10 (255 sq + 11 mul); fe_cswap constant-time via mask aritmetico; wipe volatile-safe em todos os intermediarios; tests com 25+ assertions cobrindo vetores RFC 7748 §5.2 (scalar mult) e §6.1 (Alice/Bob ECDH), small-order point rejection (u=0, u=1), fail-closed em NULL, top-bit masking, scalar clamping; triplet canonico ECDH→HKDF→AEAD agora completo (composicao com alpha.213 HKDF e alpha.215 AEAD)
- `capyos-0.8.0-alpha.215+20260513.md`
  - ChaCha20-Poly1305 AEAD (RFC 8439) canonico em src/security/: implementacao do zero auditavel, independente do TLS stack BearSSL, usavel fora de TLS (secure messaging local, key wrapping, channel binding, mensagens de IPC autenticadas); chacha20_block + chacha20_encrypt (com counter overflow fail-closed) + poly1305_mac (radix-26, clamping correto, reducao mod 2^130-5 constant-time) + chacha20_poly1305_encrypt/decrypt (orchestration AEAD com tag verification em tempo constante via crypt_constant_time_compare ANTES do decrypt); wipe volatile-safe em todos os exits; tests com vetores RFC 8439 §A.1/§A.3, round-trip, in-place, tampering rejection (ct/AAD/tag), wrong key/nonce, empty pt/AAD, fail-closed
- `capyos-0.8.0-alpha.214+20260513.md`
  - CSPRNG hardening profundo: corrige bug rdtsc "=A" mal-formed em x86_64; adiciona boot-time entropy diversa (TSC jitter loop, pool address mixing); reseed proativo automatico a cada 64 KiB com RDRAND/TSC frescos; novas APIs csprng_feed_entropy_buffer (buffer arbitrario) e csprng_reseed (manual); RDRAND com retry-loop de 10 attempts per Intel SDM; caller real no handler PS/2 mouse; 3 novos testes contratuais
- `capyos-0.8.0-alpha.213+20260513.md`
  - HKDF-SHA256 (RFC 5869) end-to-end: primitiva fundacional de derivacao de chaves contexto-bound; extract+expand+wrapper com HMAC-SHA256 streaming interno; fail-closed em L > 255*HashLen, PRK < HashLen, NULL inputs; wipe completo de PRK/T(i)/HMAC scratch; validado com os 3 test vectors oficiais do RFC 5869 Appendix A
- `capyos-0.8.0-alpha.212+20260513.md`
  - timing side-channel fechado em userdb_authenticate_with_policy: caminho LOCKED passa a executar PBKDF2 com mesmo custo do caminho not-locked (~50ms); a latencia da API publica de login deixa de distinguir locked vs not-locked e fecha enumeracao remota de accounts em lockout
- `capyos-0.8.0-alpha.211+20260513.md`
  - privacy hardening do auth: auth_policy_status agrega counts em vez de listar usernames; priv_log_emit emite actor_role=<role> em vez de actor=<username> nos logs [priv]; novas APIs auth_policy_tracked_count/locked_count publicas e auth_policy_is_tracked exposta apenas em UNIT_TEST
- `capyos-0.8.0-alpha.210+20260513.md`
  - update verifier fail-closed gate: ed25519_verify/sign/create_keypair viram placeholders explicitos (verify retorna -1, sign/keypair zeram saida); update_agent passa a rejeitar todas as atualizacoes em producao por design
- `capyos-0.8.0-alpha.209+20260513.md`
  - SHA-256 ctx wipe hygiene: PBKDF2/HMAC interno e publico, sha256_hash wrapper, compute_volume_key_hash zeram contextos apos final
- `capyos-0.8.0-alpha.208+20260513.md`
  - hardening cripto Etapa 2: CSPRNG wipe de pool snapshot, sha256_clear publico, auth_policy reads non-allocating + LRU eviction, userdb_set_password wipe completo, memory_zero volatile
- `capyos-0.8.0-alpha.207+20260513.md`
  - hardening Etapa 2: lockout integrado em userdb_authenticate_with_policy, codigos USERDB_AUTH_* publicos, system_setup deduplicado e build de testes desbloqueado
- `capyos-0.8.0-alpha.206+20260513.md`
  - hardening Etapa 2: userdb_authenticate em tempo constante, locais sensiveis zerados e log de bootstrap sem hash/salt
- `capyos-0.8.0-alpha.205+20260513.md`
  - patch Etapa 2: window input plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.204+20260513.md`
  - patch Etapa 2: window event plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.203+20260513.md`
  - patch Etapa 2: window vblank plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.202+20260513.md`
  - patch Etapa 2: window flip plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.201+20260513.md`
  - patch Etapa 2: window commit plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.200+20260513.md`
  - patch Etapa 2: window blit plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.199+20260513.md`
  - patch Etapa 2: window output plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.198+20260513.md`
  - patch Etapa 2: window display plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.197+20260513.md`
  - patch Etapa 2: window scanout plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.196+20260513.md`
  - patch Etapa 2: window vsync plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.195+20260513.md`
  - patch Etapa 2: window schedule plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.194+20260513.md`
  - patch Etapa 2: window present plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.193+20260513.md`
  - patch Etapa 2: window compositor plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.192+20260512.md`
  - patch Etapa 2: window surface plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.191+20260512.md`
  - patch Etapa 2: window plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.190+20260512.md`
  - patch Etapa 2: GUI plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.189+20260512.md`
  - patch Etapa 2: release plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.188+20260512.md`
  - patch Etapa 2: reclaim plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.187+20260512.md`
  - patch Etapa 2: compaction plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.186+20260512.md`
  - patch Etapa 2: tombstone plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.185+20260512.md`
  - patch Etapa 2: purge plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.184+20260512.md`
  - patch Etapa 2: expiry plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.183+20260512.md`
  - patch Etapa 2: retention plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.182+20260512.md`
  - patch Etapa 2: archive plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.181+20260512.md`
  - patch Etapa 2: journal plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.180+20260512.md`
  - patch Etapa 2: ledger plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.179+20260512.md`
  - patch Etapa 2: receipt plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.178+20260512.md`
  - patch Etapa 2: record plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.177+20260512.md`
  - patch Etapa 2: audit plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.176+20260512.md`
  - patch Etapa 2: seal plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.175+20260512.md`
  - patch Etapa 2: cleanup plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.174+20260512.md`
  - patch Etapa 2: retire plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.173+20260512.md`
  - patch Etapa 2: ack plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.172+20260512.md`
  - patch Etapa 2: completion plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.171+20260512.md`
  - patch Etapa 2: deadline plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.170+20260512.md`
  - patch Etapa 2: sync plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.169+20260512.md`
  - patch Etapa 2: timeline plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.168+20260512.md`
  - patch Etapa 2: fence plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.167+20260512.md`
  - patch Etapa 2: barrier plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.166+20260512.md`
  - patch Etapa 2: flush plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.165+20260512.md`
  - patch Etapa 2: framebuffer plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.164+20260512.md`
  - patch Etapa 2: blit plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.163+20260512.md`
  - patch Etapa 2: output plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.162+20260512.md`
  - patch Etapa 2: display plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.161+20260512.md`
  - patch Etapa 2: scanout plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.160+20260512.md`
  - patch Etapa 2: vsync plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.159+20260512.md`
  - patch Etapa 2: schedule plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.158+20260512.md`
  - patch Etapa 2: present plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.157+20260512.md`
  - patch Etapa 2: damage plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.156+20260512.md`
  - patch Etapa 2: compositor plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.155+20260512.md`
  - patch Etapa 2: surface plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.154+20260512.md`
  - patch Etapa 2: frame plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.153+20260511.md`
  - patch Etapa 2: activation plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.152+20260511.md`
  - patch Etapa 2: queue plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.151+20260511.md`
  - patch Etapa 2: dispatch plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.150+20260511.md`
  - patch Etapa 2: handoff plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.149+20260511.md`
  - patch Etapa 2: commit plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.148+20260511.md`
  - patch Etapa 2: mount plan seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.147+20260511.md`
  - patch Etapa 2: binding seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.146+20260511.md`
  - patch Etapa 2: presenter seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.145+20260511.md`
  - patch Etapa 2: controller seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.144+20260511.md`
  - patch Etapa 2: plano seguro de rotas de credenciais do loginwindow
- `capyos-0.8.0-alpha.143+20260511.md`
  - patch Etapa 2: evento UI seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.142+20260511.md`
  - patch Etapa 2: plano seguro de acoes de credenciais do loginwindow
- `capyos-0.8.0-alpha.141+20260511.md`
  - patch Etapa 2: plano seguro de renderizacao de credenciais do loginwindow
- `capyos-0.8.0-alpha.140+20260511.md`
  - patch Etapa 2: sessao one-shot segura da tela de credenciais do loginwindow
- `capyos-0.8.0-alpha.139+20260511.md`
  - patch Etapa 2: snapshot seguro de tela de credenciais do loginwindow
- `capyos-0.8.0-alpha.138+20260511.md`
  - patch Etapa 2: view model seguro de recuperacao de credenciais do loginwindow
- `capyos-0.8.0-alpha.137+20260511.md`
  - patch Etapa 2: sessao segura de UI de credenciais do loginwindow
- `capyos-0.8.0-alpha.136+20260511.md`
  - patch Etapa 2: pipeline composto de UI de credenciais do loginwindow
- `capyos-0.8.0-alpha.135+20260511.md`
  - patch Etapa 2: view model seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.134+20260511.md`
  - patch Etapa 2: evento auditavel redigido de credenciais do loginwindow
- `capyos-0.8.0-alpha.133+20260511.md`
  - patch Etapa 2: snapshot de prontidao de credenciais do loginwindow
- `capyos-0.8.0-alpha.132+20260511.md`
  - patch Etapa 2: pipeline seguro de interacao de credenciais do loginwindow
- `capyos-0.8.0-alpha.131+20260511.md`
  - patch Etapa 2: painel seguro de credenciais do loginwindow
- `capyos-0.8.0-alpha.130+20260511.md`
  - patch Etapa 2: snapshot mascarado do campo de credenciais do loginwindow
- `capyos-0.8.0-alpha.129+20260511.md`
  - patch Etapa 2: redutor puro de input de credenciais do loginwindow
- `capyos-0.8.0-alpha.128+20260511.md`
  - patch Etapa 2: tentativa de submit do loginwindow com wipe obrigatorio
- `capyos-0.8.0-alpha.127+20260511.md`
  - patch Etapa 2: gate fail-closed de submit de credenciais do loginwindow
- `capyos-0.8.0-alpha.126+20260511.md`
  - patch Etapa 2: buffer efemero de credenciais mascaradas do loginwindow
- `capyos-0.8.0-alpha.125+20260511.md`
  - patch Etapa 2: politica auditavel de retorno da recuperacao ao login normal
- `capyos-0.8.0-alpha.124+20260511.md`
  - patch Etapa 2: politica fail-closed de credenciais do loginwindow GUI
- `capyos-0.8.0-alpha.123+20260511.md`
  - patch Etapa 2: readiness de smokes GUI com diagnóstico de rotas do dispatcher
- `capyos-0.8.0-alpha.122+20260511.md`
  - patch Etapa 2: readiness de smokes GUI com resumo embutido de blockers
- `capyos-0.8.0-alpha.121+20260511.md`
  - patch Etapa 2: blockers de smokes GUI com resumo determinístico
- `capyos-0.8.0-alpha.120+20260511.md`
  - patch Etapa 2: blockers de smokes GUI com nomes estáveis para diagnóstico
- `capyos-0.8.0-alpha.119+20260511.md`
  - patch Etapa 2: prontidão de smokes GUI com unidade pura e cobertura host planejada
- `capyos-0.8.0-alpha.118+20260511.md`
  - patch Etapa 2: contrato de prontidão para smokes GUI futuros
- `capyos-0.8.0-alpha.117+20260511.md`
  - patch Etapa 2: snapshot operacional da sessão gráfica
- `capyos-0.8.0-alpha.116+20260511.md`
  - patch Etapa 2: contrato de rotas do dispatcher central GUI
- `capyos-0.8.0-alpha.115+20260511.md`
  - patch Etapa 2: right-click de janelas pelo dispatcher central GUI
- `capyos-0.8.0-alpha.114+20260511.md`
  - patch Etapa 2: click esquerdo de janelas pelo dispatcher central GUI
- `capyos-0.8.0-alpha.113+20260511.md`
  - patch Etapa 2: Start Menu com viewport/scroll e drag-and-drop seguro
- `capyos-0.8.0-alpha.112+20260511.md`
  - patch Etapa 2: hover de mouse pelo dispatcher central GUI
- `capyos-0.8.0-alpha.111+20260511.md`
  - patch Etapa 2: scroll de mouse pelo dispatcher central GUI
- `capyos-0.8.0-alpha.110+20260511.md`
  - patch Etapa 2: teclado de janelas pelo dispatcher central GUI
- `capyos-0.8.0-alpha.109+20260511.md`
  - patch Etapa 2: snapshot de saude do dispatcher central GUI
- `capyos-0.8.0-alpha.108+20260511.md`
  - patch Etapa 2: preview passivo do loginwindow GUI
- `capyos-0.8.0-alpha.107+20260511.md`
  - patch Etapa 2: view model fail-closed do loginwindow GUI
- `capyos-0.8.0-alpha.106+20260511.md`
  - patch Etapa 2: fallback seguro CTRL+ALT+F1 para TTY
- `capyos-0.8.0-alpha.105+20260511.md`
  - patch Etapa 2: contrato terminal-shell grafico
- `capyos-0.8.0-alpha.104+20260510.md`
  - patch Etapa 2: UX operacional do desktop e File Manager
- `capyos-0.8.0-alpha.103+20260510.md`
  - patch Etapa 2: contrato seguro do loginwindow GUI
- `capyos-0.8.0-alpha.102+20260510.md`
  - patch Etapa 2: tick gráfico explícito do desktop
- `capyos-0.8.0-alpha.101+20260510.md`
  - patch Etapa 2: frame pacing cooperativo do desktop
- `capyos-0.8.0-alpha.100+20260510.md`
  - fechamento Etapa 1: CapyUI Shell Polish v1
- `capyos-0.8.0-alpha.99+20260510.md`
  - patch Etapa 1: tray avançado NET/SND/SYS/USR
- `capyos-0.8.0-alpha.98+20260510.md`
  - patch Etapa 1: wallpaper e grid de ícones
- `capyos-0.8.0-alpha.97+20260510.md`
  - patch Etapa 1: decoração de janelas ativa/inativa
- `capyos-0.8.0-alpha.96+20260510.md`
  - patch Etapa 1: apps fixados e recentes no launcher
- `capyos-0.8.0-alpha.95+20260510.md`
  - patch Etapa 1: launcher filtrável e tray inicial
- `capyos-0.8.0-alpha.94+20260510.md`
  - patch Etapa 1: CapyUI Shell Polish visual inicial
- `capyos-0.8.0-alpha.93+20260510.md`
  - patch F4: libcapy-tls adiciona adaptador BearSSL metadata-only
- `capyos-0.8.0-alpha.92+20260510.md`
  - patch F4: libcapy-tls reserva estado BearSSL metadata-only
- `capyos-0.8.0-alpha.91+20260510.md`
  - patch F4: libcapy-tls adiciona backend plan BearSSL fail-closed
- `capyos-0.8.0-alpha.90+20260510.md`
  - patch F4: libcapy-tls materializa bundle userland metadata-only
- `capyos-0.8.0-alpha.89+20260510.md`
  - patch F2: promocao publica pos-smoke VMware
- `capyos-0.8.0-alpha.88+20260510.md`
  - patch F2: aceitacao publica da evidencia smoke VMware
- `capyos-0.8.0-alpha.87+20260510.md`
  - patch F2: evidencia publica pos-smoke VMware
- `capyos-0.8.0-alpha.86+20260510.md`
  - patch F2: gate de prontidão oficial do smoke VMware
- `capyos-0.8.0-alpha.85+20260510.md`
  - patch F4: libcapy-tls resume tamanhos metadata-only do trust store
- `capyos-0.8.0-alpha.84+20260510.md`
  - patch F4: libcapy-tls manifesta trust store metadata-only
- `capyos-0.8.0-alpha.83+20260510.md`
  - patch F4: libcapy-tls descreve trust anchors metadata-only
- `capyos-0.8.0-alpha.82+20260510.md`
  - patch F4: libcapy-tls materializa slots metadata-only
- `capyos-0.8.0-alpha.81+20260510.md`
  - patch F4: libcapy-tls fixa invariantes do catálogo TLS
- `capyos-0.8.0-alpha.80+20260510.md`
  - patch F4: libcapy-tls cataloga trust anchors userland
- `capyos-0.8.0-alpha.79+20260510.md`
  - patch F4: libcapy-tls adiciona fonte userland de trust anchors
- `capyos-0.8.0-alpha.78+20260510.md`
  - patch F4: libcapy-tls prepara metadados de trust anchors
- `capyos-0.8.0-alpha.77+20260510.md`
  - patch F4: libcapy-tls prepara estado backend
- `capyos-0.8.0-alpha.76+20260510.md`
  - patch F4: libcapy-tls adiciona stub de backend
- `capyos-0.8.0-alpha.75+20260510.md`
  - patch F4: libcapy-tls conecta connect ao slot
- `capyos-0.8.0-alpha.74+20260510.md`
  - patch F4: libcapy-tls gerencia slot de contexto
- `capyos-0.8.0-alpha.73+20260510.md`
  - patch F4: libcapy-tls limpa contexto interno
- `capyos-0.8.0-alpha.72+20260510.md`
  - patch F4: libcapy-tls prepara contexto interno
- `capyos-0.8.0-alpha.71+20260510.md`
  - patch F4: libcapy-tls normaliza configuração efetiva
- `capyos-0.8.0-alpha.70+20260510.md`
  - patch F4: libcapy-tls valida janela de timeout
- `capyos-0.8.0-alpha.69+20260510.md`
  - patch F4: libcapy-tls rejeita configuração sem peer verification
- `capyos-0.8.0-alpha.68+20260510.md`
  - patch F4: política TLS hostname compartilhada entre kernel e userland
- `capyos-0.8.0-alpha.67+20260510.md`
  - patch F4: hardening de hostname no TLS kernel-side
- `capyos-0.8.0-alpha.66+20260510.md`
  - patch F4: hardening de hostname em libcapy-tls userland
- `capyos-0.8.0-alpha.65+20260510.md`
  - patch F4: adaptador HTTPS fail-closed entre libcapy-net e libcapy-tls
- `capyos-0.8.0-alpha.64+20260510.md`
  - patch F4: API userland fail-closed de libcapy-tls
- `capyos-0.8.0-alpha.63+20260510.md`
  - patch F4: hardening de request-target HTTP em libcapy-net
- `capyos-0.8.0-alpha.62+20260510.md`
  - patch F2: manifesto oficial de handoff CI/release
- `capyos-0.8.0-alpha.61+20260510.md`
  - patch F2: contrato oficial de provisionamento CI/release
- `capyos-0.8.0-alpha.60+20260510.md`
  - patch F2: gate público de CI/tag de release
- `capyos-0.8.0-alpha.59+20260510.md`
  - patch F2: contrato público de CI para publicação
- `capyos-0.8.0-alpha.58+20260510.md`
  - patch F2: gate público agregado de publicação
- `capyos-0.8.0-alpha.57+20260510.md`
  - patch F2: verificador do manifesto público de publicação
- `capyos-0.8.0-alpha.56+20260510.md`
  - patch F2: manifesto público de publicação da release
- `capyos-0.8.0-alpha.55+20260510.md`
  - patch F2: conferência do pacote público de release
- `capyos-0.8.0-alpha.54+20260510.md`
  - patch F2: preflight valida manifesto público da chave
- `capyos-0.8.0-alpha.53+20260510.md`
  - patch F2: manifesto público da chave de release
- `capyos-0.8.0-alpha.52+20260510.md`
  - patch F2: helper de fingerprint da chave pública Ed25519
- `capyos-0.8.0-alpha.51+20260510.md`
  - patch F2: preflight CI de release
- `capyos-0.8.0-alpha.50+20260510.md`
  - patch F2: pinagem SHA-256 da chave pública Ed25519
- `capyos-0.8.0-alpha.49+20260510.md`
  - patch F2: self-test negativo do verificador Ed25519
- `capyos-0.8.0-alpha.48+20260510.md`
  - patch F2: harness VMware+E1000 DHCP versionado
- `capyos-0.8.0-alpha.47+20260510.md`
  - patch Network/F4: request builder rejeita porta zero
- `capyos-0.8.0-alpha.46+20260510.md`
  - patch Network/F4: limites de labels DNS no host
- `capyos-0.8.0-alpha.45+20260510.md`
  - patch Network/F4: request builder rejeita fragmentos no path
- `capyos-0.8.0-alpha.44+20260510.md`
  - patch Network/F4: host authority hardening no URL parser e request builder
- `capyos-0.8.0-alpha.43+20260510.md`
  - patch Network/F4: fragmentos URL nao vazam para request target
- `capyos-0.8.0-alpha.42+20260510.md`
  - patch Network/F4: blocos de header truncados falham fechado
- `capyos-0.8.0-alpha.41+20260510.md`
  - patch Network/F4: headers sem separador falham fechado
- `capyos-0.8.0-alpha.40+20260510.md`
  - patch Network/F4: terminador LF-only aceito no head HTTP
- `capyos-0.8.0-alpha.39+20260510.md`
  - patch Network/F4: status-line HTTP estrita no parser
- `capyos-0.8.0-alpha.38+20260510.md`
  - patch Network/F4: respostas informacionais 1xx falham fechado
- `capyos-0.8.0-alpha.37+20260510.md`
  - patch Network/F4: Content-Encoding fail-closed no HTTP client
- `capyos-0.8.0-alpha.36+20260510.md`
  - patch Network/F4: status HTTP sem corpo tratados como vazio conhecido
- `capyos-0.8.0-alpha.35+20260510.md`
  - patch Network/F4: validação de headers além do limite armazenado
- `capyos-0.8.0-alpha.34+20260510.md`
  - patch Network/F4: Content-Length resolvido no bloco bruto de headers
- `capyos-0.8.0-alpha.33+20260510.md`
  - patch Network/F4: headers HTTP dobrados/obs-fold rejeitados
- `capyos-0.8.0-alpha.32+20260509.md`
  - patch Network/F4: Content-Length zero distinto de header ausente
- `capyos-0.8.0-alpha.31+20260509.md`
  - patch Network/F4: EOF prematuro com Content-Length vira erro HTTP
- `capyos-0.8.0-alpha.30+20260509.md`
  - patch Network/F4: body_received separado de body_len no HTTP client
- `capyos-0.8.0-alpha.29+20260509.md`
  - patch Network/F4: Transfer-Encoding fail-closed no HTTP client
- `capyos-0.8.0-alpha.28+20260509.md`
  - patch Network/F4: Content-Length duplicado precisa ser consistente
- `capyos-0.8.0-alpha.27+20260509.md`
  - patch Network/F4: Content-Length estrito e overflow-safe no HTTP client
- `capyos-0.8.0-alpha.26+20260509.md`
  - patch Network/F4: parser de headers HTTP valida nomes e valores contra
    controles brutos
- `capyos-0.8.0-alpha.25+20260509.md`
  - patch Network/F4: HTTP request builder valida host/path contra
    controles brutos
- `capyos-0.8.0-alpha.24+20260509.md`
  - patch Network/F4: parser de URL do libcapy-net rejeita controles brutos
    antes do HTTP builder
- `capyos-0.8.0-alpha.23+20260509.md`
  - patch Update/F5: `update-prepare-explain` mostra gates locais de preparo
    sem efeitos persistentes
- `capyos-0.8.0-alpha.22+20260509.md`
  - patch Update/F5: `update-prepare-dry-run` valida catalogo/cache sem
    staging, arm ou apply
- `capyos-0.8.0-alpha.21+20260509.md`
  - patch Update/F5: `update-prepare` encadeia fetch/download/stage/arm sem
    aplicar boot slot
- `capyos-0.8.0-alpha.20+20260509.md`
  - patch Update/F5: `update-apply` usa `payload_cache_sha256` verificado por
    padrao e mantém digest manual como fallback explicito
- `capyos-0.8.0-alpha.19+20260509.md`
  - patch Update/F5: download de payload com SHA-256 real, cache binario
    e auditoria `payload_cache_sha256`
- `capyos-0.8.0-alpha.18+20260509.md`
  - patch Update/F5: manifestos assinados exigem `payload_url` validado
    e exposto no status, shell e historico
- `capyos-0.8.0-alpha.17+20260509.md`
  - patch Update/F5.4: política remota usa `refs/heads/develop` para develop
    e `refs/tags/v<major>.<minor>.<patch>` para stable
- `capyos-0.8.0-alpha.16+20260509.md`
  - patch Update/F5: `update-confirm-health` e `update-rollback-check`
    operacionalizam confirmacao de saude e rollback assistido pos-apply
- `capyos-0.8.0-alpha.15+20260509.md`
  - patch Update/F5: `update-apply <payload_sha256>` aplica staged update
    somente após digest real bater com o manifesto assinado
- `capyos-0.8.0-alpha.14+20260509.md`
  - patch Update/F5: `update-fetch` baixa manifesto remoto configurado e
    reutiliza validação local Ed25519/hash/downgrade antes de persistir catalogo
- `capyos-0.8.0-alpha.13+20260509.md`
  - patch Release/F2: tooling operacional para assinar e verificar
    `release-artifacts.sha256` com Ed25519, incluindo procedimento de
    chave offline e rotacao
- `capyos-0.8.0-alpha.12+20260509.md`
  - patch Update/F5: manifest local/staged/importado exige
    `signature_ed25519` hex128 e valida assinatura antes de expor
    update ou persistir cache
- `capyos-0.8.0-alpha.11+20260509.md`
  - patch Update/F5: manifest local do `update-agent` com protecao contra
    downgrade, `payload_sha256` obrigatorio para updates novos/staged e
    aplicacao direta bloqueada quando o staged exige verificacao
- `capyos-0.8.0-alpha.10+20260509.md`
  - patch Auth/F6: recovery reset-admin e first-boot admin alinhados ao
    fluxo robusto de usuarios com DB garantido, UID/GID reservados, home
    preparada por helper comum e limpeza de registros temporarios
- `capyos-0.8.0-alpha.9+20260509.md`
  - patch Auth/F6: ciclo de vida de usuarios alinhado no backend e nos
    caminhos UI/CLI, IDs regulares a partir de 1000, DB garantido antes de
    reservar UID/GID; recovery reutiliza macros comuns
- `capyos-0.8.0-alpha.8+20260509.md`
  - patch Settings/CapyUI: correcao da criacao de usuario via UI com
    privilegio admin explicito, sessao VFS de sistema, home/prefs iniciais
    resilientes e validacao alinhada ao CLI
- `capyos-0.8.0-alpha.7+20260509.md`
  - patch CapyUI/F6: prompts secretos e editaveis, taskbar/context menus com
    clamp de viewport e labels truncadas, isolamento modal de right-click,
    hover/cursor, scroll e `Esc`, mais documentacao consolidada
- `capyos-0.8.0-alpha.6+20260503.md`
  - etapa 2 de estabilidade pré-JS do browser ring-3: pipe kernel 64 KiB, janela 480x384,
    engine framebuffer 480x360, kill imediato libera FDs, debugcon recebe logs do engine,
    e a nomenclatura de planos foi consolidada em Etapa N / Seção a-e
- `capyos-0.8.0-alpha.4+20260429.md`
  - adocao profunda do `op_budget` no navegador, eventos `[audit]`
    estruturados (browser strict + capyfs journal auth + update payload),
    verificacao SHA-256 do payload no `update_agent`, round-trip sintetico
    de dirty-shutdown e split do `update_agent` em `update_agent_transact.c`
- `capyos-0.8.0-alpha.3+20260429.md`
  - fechamento do ciclo de robustez M5/M6/M8 com journal CAPYFS autenticado
    por volume, primitiva `op_budget` reutilizavel, API de privilegios
    centralizada, pacing do buffer cache, parse-budget no navegador e modo
    estrito de transicoes
- `capyos-0.8.0-alpha.2+20260424.md`
  - avanco do plano de robustez com DHCP automatico, gates de release
    endurecidos, metricas de performance, caches DNS/HTTP e smoke x64
    persistente validado
- `capyos-0.8.0-alpha.1+20260423.md`
  - refresh completo da trilha alpha com browser Fase 1 estabilizado,
    reorganizacao estrutural do codigo, snapshots versionados e documentacao
    alinhada
- `capyos-0.8.0-alpha.0+20260419.md`
  - correcao critica do checksum TCP, retransmissao de SYN, redirect HTTP,
    diagnostico e UX do `net-fetch` e do navegador HTML interno
- `capyos-0.8.0-alpha.0+20260411.md`
  - consolidacao em `develop` com desktop x64 estabilizado, trilha 32-bit removida,
    ampliacao de drivers e revisao da documentacao
- `capyos-0.8.0-alpha.0.md`
  - consolidacao do nome CapyOS, trilha oficial x64, auditoria de disco e
    reorganizacao completa da documentacao

## Historico recente

- `capyos-0.7.3-alpha.1.md`
- `capyos-0.7.3-alpha.0.md`
- `capyos-0.7.2-alpha.0.md`
- `capyos-0.7.1-alpha.1.md`
- `capyos-0.7.0-alpha.1.md`

## Nota

As releases anteriores a `0.8.0-alpha.0` pertencem ao periodo de transicao
entre os fluxos legados e a trilha atual `UEFI/GPT/x86_64`. Elas podem citar
identificadores tecnicos antigos de boot e layout de disco. Leia essas notas
como historico, nao como guia de setup atual.
