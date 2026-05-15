# CapyOS 0.8.0-alpha.210+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch fecha um vetor de comprometimento critico: o caminho de
verificacao de assinatura de atualizacoes (`update_agent`) era ancorado
em uma rotina `ed25519_verify` matematicamente quebrada que aceitava
qualquer assinatura forjada. A funcao agora fail-closes (retorna -1
incondicionalmente), e `ed25519_sign`/`ed25519_create_keypair` zeram
suas saidas para impedir que qualquer codigo construa material
criptografico "real" a partir do placeholder. Header e callers ganham
SECURITY WARNING explicito. Testes contratuais novos bloqueiam
regressao.

Apos esta entrega, o pipeline de updates rejeita 100% dos manifests em
producao por design, ate uma implementacao real de RFC 8032 ser
plugada. Test builds com `UNIT_TEST` continuam usando o hook
`g_update_manifest_verifier`, preservando toda a cobertura existente.

## Anatomia do bug fechado

`src/security/ed25519.c::ed25519_verify` em versoes <= alpha.209 fazia:

```c
hram = SHA-256(signature[0..31] || public_key || message)
hash = ed25519_hash(public_key)  /* "ed25519_hash" = double SHA-256 */
check[i] = signature[i] + (hram[i] XOR hash[i])
return crypt_constant_time_compare(check, signature + 32, 32)
```

Verificacao passa quando `check[i] == signature[32+i]`. Qualquer um
pode satisfazer essa equacao sem private key:

1. Pegue qualquer 32 bytes para `signature[0..31]` (a "metade R").
2. Compute `hram = SHA-256(signature[0..31] || public_key || message)`
   — todos os inputs sao publicos.
3. Compute `hash = ed25519_hash(public_key)` — public_key e publico.
4. Defina `signature[32+i] = signature[i] + (hram[i] XOR hash[i])`.

Nenhuma multiplicacao escalar na curva, nenhum private key
consultado, nenhuma propriedade de Ed25519 envolvida. A "verificacao"
e um cheque de uma equacao algebrica solucionavel por qualquer
atacante.

`src/services/update_agent.c::manifest_signature_ed25519_valid` usava
essa funcao como **unico** gate criptografico em 7 paths:

1. `update_agent_poll` aceitando manifest disponivel.
2. Validando manifest staged.
3. Validando manifest importado.
4. Validando manifest pre-install (caminho A).
5. Validando manifest pre-install (caminho B).
6. Reportando `signature_ready` no status.
7. Validando payload cache contra manifest.

Resultado: qualquer atacante que conseguisse substituir o manifest
(MitM no canal de releases, comprometimento do mirror, escrita
arbitraria no storage local de updates) podia produzir um manifest
com payload arbitrario e uma "assinatura" computada via formula
acima. O update_agent aceitaria como autentico, baixaria o payload,
verificaria o SHA-256 (interno, do proprio manifest atacante), e
instalaria o codigo do atacante como atualizacao oficial.

## Decisao: fail-closed em vez de fix

Implementar Ed25519 real (RFC 8032) envolve ~2000 linhas de
aritmetica de campo em tempo constante, compressao/descompressao de
pontos, multiplicacao escalar na curva e SHA-512 verdadeiro. Esta
muito alem do escopo de um slice. Adicionalmente, o sistema esta em
alpha pre-release sem manifests produtivos assinados — nenhum usuario
final depende do path de update hoje.

A decisao certa nesse cenario e **falhar fechado**: aceitar zero
manifests em producao ate que um verificador real esteja disponivel,
ao inves de continuar aceitando forjados. O custo da fail-closure e
"nenhuma atualizacao automatica funciona". O custo de continuar com o
verificador quebrado e "qualquer atacante pode instalar codigo".

## Entregas

### `src/security/ed25519.c`

- **`ed25519_verify`**: corpo substituido por `return -1;` com
  comentario SECURITY de ~30 linhas detalhando a equacao quebrada, o
  ataque trivial que ela permitia, e o impacto no update_agent.
- **`ed25519_sign`**: corpo substituido por wipe via `volatile uint8_t *`
  do buffer `signature`. Inputs (`message`, `public_key`,
  `private_key`) intocados. NULL signature buffer tolerado.
- **`ed25519_create_keypair`**: corpo substituido por wipe via
  `volatile uint8_t *` de ambos `public_key` e `private_key`. Seed
  intocado. NULL output buffers tolerados.
- **`ed25519_hash`** (static helper): marcado `__attribute__((unused))`
  com comentario explicando que esta dormente ate a implementacao
  real e que deve ser substituida por SHA-512 verdadeiro antes de
  ser reativada.
- **Header do arquivo**: comentario expandido explicando que o
  arquivo e placeholder, que o esqueleto de aritmetica de curva
  (fe25519, ge25519, fe_mul, fe_pow2523, etc.) e preservado para
  implementacao futura, e que `security/crypt.h` foi removido do
  include pois `crypt_constant_time_compare` nao e mais usado neste
  arquivo.

### `include/security/ed25519.h`

- Adicionado bloco SECURITY WARNING no topo do header explicando que
  todas as tres entradas sao placeholders fail-closed.
- Cada funcao recebe comentario `/* Placeholder: ... */` na
  declaracao, documentando o comportamento atual.

### `src/services/update_agent.c::manifest_signature_ed25519_valid`

- Comentario novo antes da chamada de `ed25519_verify` explicando que
  o gate e intencionalmente fail-closed em producao, e que o path
  UNIT_TEST via `g_update_manifest_verifier` continua sendo a unica
  forma suportada de aceitar manifests em testes.
- Sem mudanca de codigo: o callsite ja chamava `ed25519_verify` e
  comparava `== 0`. Agora a comparacao sempre falha, o que produz
  rejeicao automatica.

### `tests/test_crypt_vectors.c::test_ed25519_failclosed_contract`

- Teste contratual novo cobrindo:
  - `ed25519_verify` retorna -1 para inputs nao-zero, zero, e
    mensagem vazia (NULL pointer + length=0).
  - `ed25519_sign` zera os 64 bytes do buffer signature mas nao
    escreve alem dele (sentinel 0x77 em 8 bytes posteriores
    preservado).
  - `ed25519_create_keypair` zera ambos os 32 bytes do public_key e
    64 bytes do private_key.
  - `ed25519_sign(NULL, ...)` e `ed25519_create_keypair(NULL, NULL,
    ...)` sao tolerados sem crash.
- Adicionado ao `run_crypt_vector_tests` para execucao automatica.

## Segurança e privacidade

- **Update channel: forjamento de assinatura fechado.** Anteriormente,
  qualquer atacante com escrita no canal de update (MitM, repositorio
  comprometido, escrita local) podia instalar codigo arbitrario via
  forjamento trivial. Agora, nenhum manifest e aceito em producao.
- **Material chave: zerado em vez de garbage.** As funcoes
  sign/create_keypair antes produziam bytes determinsticos derivados
  do private_key/seed sem qualquer propriedade criptografica. Se um
  caller futuro mistakenly trustasse a saida como "chave Ed25519
  real", estaria deployando lixo. Agora a saida e zero — obviamente
  invalida, falhara qualquer verificacao posterior.
- **Wipe na escrita.** As funcoes que zeram saidas usam
  `volatile uint8_t *` para o loop de zeragem, resistindo a
  dead-store elimination do compilador. Consistente com o padrao
  estabelecido em `sha256_clear` (alpha.208) e `memory_zero`
  (alpha.208) para outros wipes de seguranca.
- **Stack hygiene preservada.** A versao anterior do `ed25519_sign`
  carregava o private_key na stack via `ed25519_hash(private_key)`
  sem wipar o contexto SHA-256 (mesmo problema que ja era resolvido
  em alpha.209 para os outros sites SHA-256). Como o corpo da funcao
  agora nao processa o private_key, esse vazamento secundario
  desaparece automaticamente.

## Desempenho e escalabilidade

- Caminho de update: rejeita manifests em O(1) (return -1 imediato).
  Custo desprezivel comparado a versao anterior, que executava
  multiplos `sha256_init/update/final` para chegar a "valid".
- ABI de `ed25519_verify`, `ed25519_sign`, `ed25519_create_keypair`
  inalterada (mesmas assinaturas, mesmos tipos).
- Sem alocacao dinamica nova, sem novas dependencias.

## Validação

Validado por revisão estática. Pontos cobertos:

- `src/security/ed25519.c::ed25519_verify` retorna -1 incondicionalmente
  com cast `(void)` em todos os parametros.
- `src/security/ed25519.c::ed25519_sign` zera o buffer signature via
  `volatile uint8_t *`, com guard `if (signature)` para NULL.
- `src/security/ed25519.c::ed25519_create_keypair` zera public_key e
  private_key via `volatile uint8_t *`, com guards para NULL.
- `src/security/ed25519.c::ed25519_hash` marcado
  `__attribute__((unused))`.
- `include/security/ed25519.h` documenta o placeholder com SECURITY
  WARNING explicito.
- `src/services/update_agent.c::manifest_signature_ed25519_valid`
  ganhou comentario explicando o gate fail-closed.
- `tests/test_crypt_vectors.c::test_ed25519_failclosed_contract`
  invocado via `run_crypt_vector_tests`.
- Tests existentes de `update_agent`/`update_transact`/`audit_events`
  usam o hook UNIT_TEST `g_update_manifest_verifier` (verificado via
  grep) e nao chamam `ed25519_verify` diretamente — continuam
  funcionais sem mudanca.
- Makefile linha 1129 ja inclui `src/security/ed25519.c` em
  TEST_SRCS, garantindo que o teste novo linka.

## Compatibilidade

- ABI publica de `ed25519_verify`, `ed25519_sign`,
  `ed25519_create_keypair` preservada (mesmas assinaturas, mesmos tipos).
- Comportamento observavel diferente:
  - `ed25519_verify` antes podia retornar 0 (aceitar) ou nao-zero
    (rejeitar) dependendo da entrada; agora sempre retorna -1
    (rejeitar).
  - `ed25519_sign` antes preenchia 64 bytes com bytes derivados do
    private_key; agora preenche com zeros.
  - `ed25519_create_keypair` antes preenchia public_key/private_key
    com bytes derivados do seed; agora preenche com zeros.
- Manifests produtivos: nenhum existe (alpha pre-release), entao a
  rejeicao de 100% nao quebra usuarios reais.
- Test paths: o hook UNIT_TEST continua autoritativo para testes;
  todos os testes existentes que usavam o hook permanecem funcionais.

## Limites

- Nao implementa Ed25519 real (RFC 8032). O esqueleto de aritmetica
  de curva (`fe25519`, `ge25519`, `fe_mul`, `fe_pow2523`, etc.) e
  preservado como ponto de partida para implementacao futura, mas
  todas as funcoes estao marcadas `__attribute__((unused))`.
- Nao adiciona um KDF/MAC alternativo para autenticar updates. O path
  apropriado e plugar Ed25519 real; alternativas (HMAC com chave
  compartilhada, RSA, etc.) tem trade-offs piores em distribuicao de
  chave e ja existem usuarios futuros do `RELEASE_PUBLIC_KEY` (chave
  Ed25519 em PEM) atraves dos targets do Makefile.
- Nao adiciona um mecanismo de "manual override" para aceitar
  manifests em producao quando o verificador esta fail-closed — isso
  seria contra-producente, pois normalizaria o uso do placeholder.
  Quem precisar testar updates pode usar UNIT_TEST builds com o hook,
  ou esperar pela implementacao real.
- Nao destrava entregaveis pendentes da Etapa 2 (loginwindow GUI
  real, smokes `gui-session`/`mouse-events`).
- Esta entrega NAO altera a pipeline declarativa
  (`window_surface_plan` ... `window_input_plan`) nem a trilha de
  autenticacao por senha (`userdb_authenticate_with_policy`).
