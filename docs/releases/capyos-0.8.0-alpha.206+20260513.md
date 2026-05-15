# CapyOS 0.8.0-alpha.206+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch fecha duas regressoes de seguranca encontradas em revisao estatica
do caminho real de autenticacao por senha do CapyOS. Diferente das fatias
declarativas anteriores da Etapa 2, que reservam tickets fail-closed para a
sessao grafica, este patch corrige o **codigo real** que valida senhas e o
**log real** de bootstrap, mantendo o sistema utilizavel (login textual,
recuperacao textual e first-boot continuam funcionais) e elevando o piso de
seguranca da plataforma sem mudar contrato publico ou superficie de API.

## Entregas

- `userdb_authenticate()` (`src/auth/user.c`) passa a comparar o hash
  PBKDF2 derivado com o hash armazenado usando `crypt_constant_time_compare`
  (`src/security/crypt.c`) ao inves do laco byte-a-byte com saida antecipada
  que ja existia. A funcao agora tambem zera os buffers locais
  `hash[USER_HASH_SIZE]` e `struct user_record rec` antes de retornar, em
  qualquer caminho de erro ou sucesso.
- `config_log_user_record_state()`
  (`src/config/first_boot/storage_users.c`) deixa de imprimir a
  representacao hexadecimal do salt e do hash PBKDF2 no log de bootstrap.
  No lugar emite marcadores redigidos `salt=[redacted size=N present=0|1]`
  e `hash=[redacted size=N present=0|1]`. O helper `bytes_to_hex_str` local,
  que so era utilizado por esse log, foi substituido por
  `bytes_have_nonzero`, que reporta presenca de bytes nao-zero sem expor
  qualquer valor.
- `tests/test_crypt_vectors.c` ganhou `test_constant_time_compare_semantics`,
  registrado automaticamente em `run_crypt_vector_tests`. O teste cobre
  buffers iguais, divergencia no primeiro byte, divergencia no ultimo byte
  e tamanho zero, fechando a semantica que o caminho de autenticacao agora
  consome.

## Seguranca e privacidade

- **Timing side-channel eliminado.** O laco antigo retornava `-1` no primeiro
  byte divergente, deixando o tempo total da chamada proporcional ao prefixo
  comum entre o hash derivado da senha digitada e o hash armazenado. Um
  atacante com chamadas repetidas a `userdb_authenticate` poderia, em
  principio, recuperar os primeiros bytes do hash PBKDF2 byte-a-byte. A nova
  versao percorre toda a faixa em volume constante usando OR sobre XOR,
  fechando o oraculo.
- **Wipe defensivo apos comparacao.** Mesmo apos remover o vazamento de
  tempo, `hash[]` e `rec` (que contem `salt`, `hash` e metadados de conta)
  permaneciam em stack ate o frame sair de escopo. O patch agora chama
  `memory_zero` em ambos antes de cada `return`, reduzindo a janela em que o
  material sensivel fica acessivel a primitivas de read-after-free ou
  remanencia de memoria.
- **Log de bootstrap deixa de carregar material critico.** O log de first
  boot escrevia o salt e o hash PBKDF2 do administrador em hex completo.
  Mesmo PBKDF2 com 64000 iteracoes deixa de proteger uma senha fraca quando
  o atacante captura salt + hash; o log de bootstrap, que e gravado em texto
  e pode ser lido por qualquer ferramenta com acesso ao volume, tornava o
  vetor offline trivial. O log agora confirma apenas que os campos foram
  populados (presenca) e o tamanho esperado (constante publica de ABI).
- **Cobertura preserva contrato.** O novo teste estatico bloqueia regressoes
  futuras na semantica de `crypt_constant_time_compare`, da qual o caminho de
  autenticacao agora depende.

## Desempenho e escalabilidade

- `crypt_constant_time_compare` continua O(USER_HASH_SIZE) (32 bytes),
  identico em ordem ao laco anterior. Como agora roda ate o fim em vez de
  parar no primeiro byte divergente, o pior caso e identico ao melhor caso:
  ~32 operacoes XOR/OR. PBKDF2 com 64000 iteracoes domina o custo total da
  funcao (~ms), logo a mudanca e neutra do ponto de vista de throughput.
- `memory_zero` de 32 + ~200 bytes adiciona algumas dezenas de ciclos por
  autenticacao, irrelevante em frente ao PBKDF2.
- `config_log_user_record_state` emite agora ~6 fragmentos curtos a menos
  por chamada (apenas durante first boot) e nao gera mais
  `salt_hex[USER_SALT_SIZE * 2 + 1]` nem `hash_hex[USER_HASH_SIZE * 2 + 1]`
  em stack.
- Nenhuma alocacao dinamica, IO bloqueante, sincronizacao ou estado global
  foi adicionado.

## Validacao

Validado por revisao estatica de codigo, sem execucao de terminal, build,
`make`, `git` ou testes em runtime. A cobertura estatica em
`tests/test_crypt_vectors.c` foi expandida com
`test_constant_time_compare_semantics`, registrado em
`run_crypt_vector_tests`. O teste exercita:

- buffers identicos (deve retornar 0)
- divergencia no primeiro byte (deve retornar != 0)
- divergencia no ultimo byte (deve retornar != 0)
- comprimento zero (deve retornar 0)

A cobertura existente de `crypt_pbkdf2_sha256` (`pbkdf2-iter1`, `iter2`,
`iter4096`) permanece intacta e continua a garantir que o lado de derivacao
do hash nao mudou.

## Compatibilidade

- ABI publica de `userdb_authenticate` inalterada: mesma assinatura, mesmos
  codigos de retorno, mesma semantica observavel para chamadores corretos.
- ABI publica de `config_log_user_record_state` inalterada.
- Formato de `/etc/passwd` inalterado: salt continua armazenado em hex no
  arquivo, hash continua PBKDF2-SHA256/64000.
- Releases anteriores continuam capazes de autenticar contra a userdb gerada
  por esta release (e vice-versa), pois apenas a comparacao foi trocada e
  nao a derivacao.
- Logs de bootstrap antigos que ainda contenham salt/hash em hex devem ser
  rotacionados/expurgados manualmente por operadores.

## Limites

- Nao altera a politica de iteracoes do PBKDF2 (64000); upgrade para
  Argon2id permanece um trabalho de fatia futura.
- Nao adiciona rate-limit ou backoff sobre tentativas de senha; isso fica
  ligado ao `auth_policy` (`max_attempts`) ja existente.
- Nao reescreve `/etc/passwd` retroativamente; o efeito do log redigido
  vale para boots novos.
- Nao substitui os smokes reais de GUI nem destrava os entregaveis
  pendentes da Etapa 2 (`Loginwindow GUI com senha e recuperacao segura`,
  `Smokes gui-session e mouse-events`); estes seguem como proximos passos
  do plano.
- Nao adiciona testes integrados de `userdb_authenticate` em runtime: a
  cobertura aqui e estatica/unitaria sobre `crypt_constant_time_compare`.
