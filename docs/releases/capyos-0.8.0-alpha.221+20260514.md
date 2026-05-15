# CapyOS 0.8.0-alpha.221+20260514

Release de hardening criptografico: **on-disk volume header module** com
algorithm marker, per-volume random salt, HMAC-SHA256 check tag e CRC32
fast bit-rot gate. Primeira primitiva canonica do projeto a expor
descriptor on-disk para AES-XTS volume keys, preparando o consumo da
primitiva Argon2id entregue em alpha.220 e fechando o gap conhecido
desde alpha.218 sobre `g_disk_salt` hardcoded.

## ANTES (estado em alpha.220)

Toda instalacao CapyOS gravava o filesystem CAPYFS comecando no LBA 0
raw da particao DATA. Chaves AES-XTS eram derivadas implicitamente via
`PBKDF2-SHA256(16000 iter)` sobre `g_disk_salt = 'NoirOS-FS-Salt!'`
(16 bytes hardcoded em `src/installer/installer_main.c:39-41`) e o
mesmo valor replicado por valor em `src/arch/x86_64/kernel_runtime_ops.c:287`,
`src/arch/x86_64/kernel_volume_runtime/key_storage_probe.c:75` e
`src/core/kernel.c:392`. Tres problemas residuais conviviam no
codigo:

1. **Volume key continua PBKDF2 sem memory-hardness.** Atacante com
   disco roubado offline brute-forcava a senha pagando speedup
   GPU/ASIC tipico >10000x sobre CPU. Documentado em alpha.218 Limites
   como gap conhecido de menor prioridade.
2. **Salt nao e per-install.** Rainbow tables pre-computadas contra a
   string literal `NoirOS-FS-Salt!` acelerariam ataque contra qualquer
   instalacao CapyOS simultaneamente — uma unica tabela compromete a
   frota.
3. **Primitiva `crypt_derive_xts_keys_argon2id` entregue em alpha.220
   ficou sem caller real.** Trocar o KDF sem um header on-disk
   quebraria a leitura de qualquer volume existente: kernel novo
   derivaria chave diferente e decifraria basura.

## AGORA

`include/security/volume_header.h` define struct on-disk fixa de **512
bytes** (ocupa 1 LBA dentro do bloco 4 KiB da camada inferior; padding
zero-fill nos 3584 bytes restantes do LBA fica disponivel para versoes
futuras). Layout (offsets em bytes):

| Offset | Tamanho | Campo                   | Descricao                                                  |
|-------:|--------:|-------------------------|-------------------------------------------------------------|
|     0  |     4   | `magic0`                | "CAPY" little-endian (0x59504143)                            |
|     4  |     4   | `magic1`                | "VHDR" little-endian (0x52444856)                            |
|     8  |     4   | `version`               | `CAPYOS_VOLUME_HEADER_VERSION = 1`                          |
|    12  |     4   | `flags`                 | Reservado v1 (deve ser 0)                                    |
|    16  |     4   | `kdf_algo_id`           | 0 = PBKDF2-SHA256, 1 = Argon2id                              |
|    20  |     4   | `kdf_t_cost`            | iter PBKDF2 ou `t_cost` Argon2id                             |
|    24  |     4   | `kdf_m_cost`            | KiB para Argon2id (0 para PBKDF2)                            |
|    28  |     4   | `kdf_salt_len`          | em [8, 64]                                                    |
|    32  |    64   | `kdf_salt[64]`          | zero-padded apos `salt_len`                                  |
|    96  |     4   | `data_offset_lba`       | onde o FS comeca (>=1)                                       |
|   100  |     4   | `reserved_lba_count`    | LBAs reservados para o header (>=1)                          |
|   104  |    32   | `kdf_check_tag[32]`     | HMAC-SHA256(K1‖K2, context ‖ prefix[0..104])                 |
|   136  |     8   | `creation_timestamp_ns` | Forense, opaco para o kernel                                 |
|   144  |    32   | `creator_version[32]`   | "CapyOS-0.8.0-alpha.221" null-padded                         |
|   176  |   332   | `reserved[332]`         | Zero em v1; qualquer byte != 0 rejeita parse                 |
|   508  |     4   | `header_crc32`          | IEEE 802.3 reflected sobre bytes [0..508)                    |

### Modulo `src/security/volume_header.c` (~620 LOC)

- **`vh_serialize_prefix`** e autoridade unica do layout dos primeiros
  104 bytes. Tanto o serializer disco-side quanto o computador de
  HMAC tag consomem essa funcao — impede drift entre o que vai a
  disco e o que e autenticado pelo `kdf_check_tag`.
- **Endianness little-endian explicita** via `vh_put_u32_le` /
  `vh_get_u32_le` / `vh_put_u64_le` / `vh_get_u64_le`. CapyOS so
  roda em x86_64 hoje, mas a portabilidade host-side dos testes
  contra big-endian fica garantida.
- **CRC32 no-table branchless** (~10 LOC): poly 0xEDB88320 reflected,
  init/post-xor 0xFFFFFFFF, mask aritmetico `mask = -(int32_t)(crc & 1u)`
  para evitar branch sobre bits secretos (CRC32 ja nao e secreto,
  mas a higiene cabe). Trade-off explicito: throughput de ~33000
  cycles por header e completamente dominado pelos 8 MiB da
  derivacao Argon2id que segue.
- **`vh_validate_params` strict**:
  - PBKDF2: `t_cost >= 1000` (piso de sanidade contra downgrade
    tampered) **e** `m_cost == 0` (qualquer valor diferente sinaliza
    parser confuso).
  - Argon2id: `t_cost >= 1` e `m_cost >= 8` (RFC 9106 §3.1).
  - `salt_len` em [8, 64].
  - `data_offset_lba >= 1` (header ocupa LBA 0).
  - `reserved_lba_count` em [1, `data_offset_lba`].
- **`_parse` fail-safe**: wipe `out` ANTES de qualquer validacao;
  depois verifica CRC -> magic -> version (sequencia barata) antes
  de param validation + reserved-all-zero (carrego). Qualquer falha
  retorna a struct zerada, nao residuo do disco.
- **`_derive_keys` dispatcher**: wipe `key1`/`key2` first, valida
  params, executa `crypt_derive_xts_keys` ou
  `crypt_derive_xts_keys_argon2id` conforme `algo_id`, depois
  `_verify_check_tag`, wipe em qualquer falha. Caller que esquece
  return code lida com sentinela zero, nao com stack residue.

### Threat model two-tier

Documentado inline (170+ linhas de comentarios em
`include/security/volume_header.h`):

- **`header_crc32` e fast bit-rot gate, NAO seguranca.** Atacante
  com acesso ao disco recomputa CRC32 trivialmente. Seu unico papel
  e abortar parse cedo em corrupcao silenciosa antes de pagar 8 MiB
  de Argon2id por um header 50%-certo invalido.
- **`kdf_check_tag` e o binding criptografico.** HMAC-SHA256 sob
  `key1‖key2` autentica `context‖prefix[0..104]`. Atacante que
  altera salt/algo/t_cost/m_cost/data_offset/reserved_lba forca o
  usuario a derivar chave diferente, o recomputo do HMAC nao bate
  com o tag stored, mount recusa.

**Decisao deliberada de UX/seguranca**: o boot path NAO distingue
"wrong password" de "tampered header" no retorno publico. Ambos
sao `CAPYOS_VOLUME_HEADER_ERR_CHECK_TAG`. Distinguir-los geraria
um oracle: atacante que copia um header tampered para um disco
roubado mediria diferenca de mensagem entre "senha errada" e
"tag mismatch" e descobriria se a sua tampering passou pelo
parser. A indistinguishability aqui e equivalente ao timing-
constant compare ja aplicado em outros lugares do auth stack.

### Tests host-side (`tests/test_volume_header.c`, ~620 LOC)

13 funcoes / ~70 assertions cobrindo:

1. **CRC32 known-answer**: RFC 3309 vetores `""` -> 0x00000000,
   `"a"` -> 0xE8B7BE43, `"123456789"` -> 0xCBF43926; NULL guard
   retorna 0; zero-buffer de 64 bytes confirma que o loop nao
   para no primeiro byte zero.
2. **`_init` happy paths PBKDF2 e Argon2id**: magic/version/algo/
   t/m/salt_len/salt-bytes/salt-tail-zero/data_offset/reserved_lba/
   check_tag-zero/timestamp/creator_version/creator-null-pad/
   reserved-all-zero. Inclui caso de `creator_version = NULL`.
3. **`_init` fail-closed (13 vetores)**: NULL out, NULL salt,
   algo desconhecido, PBKDF2 t<1000, PBKDF2 m!=0, Argon2id t=0,
   Argon2id m<8, salt<8, salt>64, data_offset=0, reserved=0,
   reserved>data_offset.
4. **serialize/parse roundtrip com endianness explicit**: bytes
   0..7 do buffer serializado sao lidos diretamente como ASCII
   `'C','A','P','Y','V','H','D','R'` — assertion endurece a
   garantia de little-endian on-disk.
5. **`_parse` fail-closed**: NULL inputs, magic tampered com CRC
   refixada -> `ERR_MAGIC`, version tampered com CRC refixada ->
   `ERR_VERSION`, body tampered sem fix de CRC -> `ERR_CRC`,
   algo tampered -> `ERR_ALGO`, flags!=0 -> `ERR_FLAGS`, reserved!=0
   -> `ERR_RESERVED`.
6. **`_looks_valid` quick gate**: valid -> 1, corrupt -> 0,
   NULL -> 0, all-zero buffer (boundary com FS unencrypted) -> 0.
7. **`_derive_keys` success em ambos KDFs**: dispatcher produz as
   mesmas keys que `crypt_derive_xts_keys{,_argon2id}` direto;
   `key1 != key2` (anti-split-bug).
8. **Wrong password**: sentinela 0xA5 plantada em `key1`/`key2`
   antes da chamada; apos `derive_keys` retornando
   `ERR_CHECK_TAG`, ambos os buffers DEVEM estar wiped a zero.
9. **Tampered salt detectado mesmo com password correto**: 1-byte
   flip no `kdf_salt[0]` faz `derive_keys` produzir chave
   diferente; HMAC recomputado nao bate; rejeita.
10. **Algo downgrade attempt**: header Argon2id legitimo + tag
    computed; atacante reescreve `algo_id=PBKDF2`, `t_cost=1000`,
    `m_cost=0` para passar o param validator. `derive_keys`
    dispatcha PBKDF2 (passa validador), produz keys diferentes
    das que originalmente assinaram o tag, HMAC mismatch, rejeita.
11. **Fail-closed NULL hdr/pwd/k1/k2**: cada `NULL` retorna
    `ERR_NULL` com sentinela de 0xA5 wiped a zero.

### Wiring

- `Makefile`: `CAPYOS64_OBJS` adiciona `$(BUILD)/x86_64/security/volume_header.o`
  apos `crypt.o`; `TEST_SRCS` inclui `tests/test_volume_header.c
  src/security/volume_header.c`.
- `tests/test_runner.c`: declara `run_volume_header_tests` e chama
  apos `run_crypt_vector_tests`.

### Mapa de entrega (alpha.221 — alpha.223)

| Slice         | Escopo                                                                 | Status     |
|---------------|------------------------------------------------------------------------|------------|
| **alpha.221** | Primitiva entregue: header module + dispatcher `derive_keys` + tests.  | **DONE**   |
| alpha.222     | Wiring write-side: installer escreve header no LBA 0 raw + FS no LBA 1+ via `block_offset_wrap`; boot path tenta header read primeiro, fallback para legacy PBKDF2 + `g_disk_salt` em volumes pre-alpha.222. | Planejado |
| alpha.223     | Ferramenta de re-keying in-place: volumes legacy migram para header-managed sem reformatar (read com legacy keys, write com keys novas + header novo). | Planejado |

## NAO altera

- Installer (`src/installer/installer_main.c`) continua escrevendo o
  filesystem em LBA 0 raw com PBKDF2 + `g_disk_salt`. Volumes
  instalados em alpha.221 sao bit-identicos aos de alpha.220.
- Boot path (`src/arch/x86_64/kernel_volume_runtime/public_mount_api.c`)
  continua chamando `open_crypt_volume_with_password` que deriva via
  `crypt_derive_xts_keys` (PBKDF2 + `state->disk_salt`).
- `g_disk_salt` permanece em quatro callsites; alpha.222 e o slice que
  o aposenta.

## Composicao com primitivas previas

- **alpha.220** (`crypt_derive_xts_keys_argon2id`): backend do
  dispatcher Argon2id de `_derive_keys`.
- **alpha.218** (`argon2id_hash`, `blake2b_*`): primitiva Argon2id que
  alpha.220 consome.
- **alpha.214** (CSPRNG hardening): fornecera `kdf_salt` per-install
  via `csprng_get_bytes` quando alpha.222 acionar.
- **alpha.213** (HKDF-SHA256): ortogonal; serve outros derivative
  contexts (TLS userland futuro, journal root secrets), nao
  participa do volume header.
- **alpha.209** (`sha256_clear` hygiene): propaga via
  `crypt_hmac_sha256` para `vh_compute_tag_internal` — todos os
  contextos SHA-256 intermediarios sao zerados pelo wipe interno
  da implementacao HMAC.

## ABI publica

Preservada. Todos os simbolos novos sao aditivos
(`capyos_volume_header_*` + macros `CAPYOS_VOLUME_*`). Nenhum
prototipo existente foi alterado.

## Limites residuais

- **Nenhum installer/boot path consome o header ainda.** Alpha.222
  fechara este gap. Ate la, a primitiva e dormente em producao,
  testada apenas no host-side runner.
- **CRC32 acidental-only**: bit rot e disco-de-mesa cobertos; ataques
  ativos com escrita no disco passam pelo CRC mas batem no
  `kdf_check_tag`. Nao e fraqueza — design intencional.
- **Sem revocation ou versionamento dinamico do `context_label`**.
  Mudanca do label exige bump de `version` e logica de migracao em
  alpha.222+. Documentado em
  `CAPYOS_VOLUME_HEADER_CHECK_CONTEXT_LEN` comment.
