# CapyOS 0.8.0-alpha.207+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch fecha a integracao entre a autenticacao por senha
(`userdb_authenticate`) e o sistema de lockout/rate-limit
(`auth_policy_*`), expoe codigos de retorno estaveis para callers
distinguirem "conta bloqueada" de "credencial invalida", e desbloqueia o
build de testes host-side ao corrigir um typo do Makefile que mesclava o
caminho de dois arquivos. Continua a trilha de hardening de credenciais
iniciada em `alpha.206`, agora cobrindo o caminho de chamada e a
ergonomia da API, alem da primitiva subjacente.

## Entregas

- `src/auth/user.c` mantem a mitigacao de **user enumeration timing**:
  quando `userdb_find` falha, a funcao continua executando
  `crypt_pbkdf2_sha256` contra `k_userdb_dummy_salt`, igualando o tempo
  de parede entre "usuario inexistente" e "usuario existe mas senha
  errada". A constante `k_userdb_dummy_salt` e ABI publica (16 bytes
  determinismos, nao-secretos) e existe apenas para alimentar o PBKDF2
  fantasma.
- Novo `userdb_authenticate_with_policy` (`src/auth/user.c`) compoe:
  1. `auth_policy_check_allowed(username)` — refusa cedo se a conta
     esta bloqueada (sem queimar PBKDF2);
  2. `userdb_authenticate(username, password, out)` — caminho seguro
     em tempo constante;
  3. `auth_policy_record_success(username)` ou
     `auth_policy_record_failure(username)` conforme o resultado.
  Retorna `USERDB_AUTH_OK`, `USERDB_AUTH_LOCKED` ou
  `USERDB_AUTH_FAILED`, definidos em `include/auth/user.h`.
- `include/auth/user.h` ganha o prototipo de
  `userdb_authenticate_with_policy` e as constantes `USERDB_AUTH_*`
  estaveis, fechando a ABI publica que estava incompleta (a funcao
  estava implementada em `user.c` mas nao declarada, o que tornaria o
  build inconsistente em qualquer caller que tentasse incluir o
  header).
- `src/config/system_setup.c` (login interativo do wizard) deixa de
  repetir manualmente o trio `check_allowed` / `authenticate` /
  `record_*` e passa a chamar `userdb_authenticate_with_policy` em uma
  unica linha. A UI continua distinguindo conta bloqueada e credencial
  invalida via `switch` em `USERDB_AUTH_LOCKED` vs `USERDB_AUTH_FAILED`.
  O wipe de `password` agora ocorre em um unico ponto, imediatamente
  apos a chamada, reduzindo caminhos onde o buffer poderia escapar
  esquecido.
- `Makefile` linha 1109 (`TEST_SRCS`) tinha o segmento corrupto
  `tests/test_net_probe.c src/driverslogin_window_gui_layout.c
  src/auth//net/net_probe.c src/drivers/net/netvsc.c` (path mesclado
  sem `/`, duplo `/` antes de `net`). O patch restaura
  `tests/test_net_probe.c src/drivers/net/net_probe.c
  src/drivers/net/netvsc.c`, desbloqueando o build host-side dos
  testes.
- `tests/test_auth_policy.c` ganha um bloco de testes contratuais para
  `USERDB_AUTH_OK == 0`, `USERDB_AUTH_FAILED == -1` e
  `USERDB_AUTH_LOCKED == -2`, alem de checagens de unicidade. Drift
  desses valores faria callers existentes confundirem "bloqueado" com
  "invalido" silenciosamente, e o teste bloqueia essa regressao.

## Seguranca e privacidade

- **Lockout passa a ser obrigatorio no caminho interativo de login.**
  Antes de `alpha.207`, o wrapper existia parcialmente (implementacao
  em `user.c`) mas nao tinha as constantes declaradas e o `system_setup`
  duplicava a logica `check_allowed` + `record_*` em cada caller. Era
  trivial para um caller novo (por exemplo um futuro submit GUI da
  loginwindow) esquecer um dos tres passos e burlar o lockout. Com o
  wrapper publico, a integracao certa e a opcao mais simples.
- **User enumeration timing permanece mitigado.** O salt fictício
  garante que o tempo de `userdb_authenticate` nao revele se o
  username existe; somente o codigo de retorno o faz, e apenas para
  callers in-process (que ja sao confiaveis).
- **Wipe de senha mais facil de auditar.** O `memory_zero(password,
  sizeof(password))` em `system_setup.c` agora ocorre em um unico
  ponto apos a chamada do wrapper, em vez de tres pontos
  (lockout-branch, success-branch, fail-branch). Auditoria estatica e
  manutencao ficam mais simples.
- **Lockout aplicado antes do PBKDF2.** O wrapper invoca
  `auth_policy_check_allowed` antes do PBKDF2, garantindo que contas
  bloqueadas nao consomem ~64000 iteracoes de HMAC-SHA256 por
  tentativa, o que sustenta CPU contra ataques de brute force quando o
  bloqueio ja foi ativado.

## Desempenho e escalabilidade

- O wrapper adiciona uma checagem `auth_policy_check_allowed` + uma
  ramificacao no inicio do caminho de auth. Custo: ~uma busca linear em
  `AUTH_MAX_TRACKED_USERS` (atualmente pequeno) por chamada — desprezivel
  frente aos ~ms do PBKDF2.
- O fix do Makefile nao afeta o binario do kernel; afeta apenas o build
  host-side dos testes, que agora volta a compilar.
- Nenhuma alocacao dinamica, IO bloqueante ou estrutura global
  adicional foi introduzida.

## Validacao

Validado por revisao estatica, sem execucao de terminal, build ou
testes em runtime. Os pontos cobertos pela revisao:

- `include/auth/user.h` declara `USERDB_AUTH_OK`, `USERDB_AUTH_FAILED`,
  `USERDB_AUTH_LOCKED` e o prototipo de
  `userdb_authenticate_with_policy`; `src/auth/user.c` implementa o
  contrato e inclui `auth_policy.h` para resolver os simbolos
  `auth_policy_check_allowed/record_success/record_failure`.
- `src/config/system_setup.c` consome o wrapper exclusivamente, sem
  chamadas remanescentes de `auth_policy_check_allowed`,
  `auth_policy_record_success` ou `auth_policy_record_failure`. A UI
  preserva as tres mensagens (credenciais ausentes, conta bloqueada,
  credencial invalida).
- `Makefile` linha 1109 lista os tres arquivos corretos
  (`tests/test_net_probe.c`, `src/drivers/net/net_probe.c`,
  `src/drivers/net/netvsc.c`) sem paths mesclados.
- `tests/test_auth_policy.c` agora inclui `auth/user.h` e registra os
  testes contratuais dentro de `run_auth_policy_tests`, que ja era
  registrado em `tests/test_runner.c`.

## Compatibilidade

- ABI inalterada para `userdb_authenticate` (mesmo retorno 0/-1, mesma
  semantica observavel). Callers internos (`kernel_shell_runtime.c`,
  `config/first_boot/program.c`) que verificam credenciais armazenadas
  sem trigger de lockout continuam usando-a diretamente — comportamento
  preservado.
- Caller interativo (`system_setup.c::login`) migra para o wrapper, com
  UX equivalente.
- Formato de `/etc/passwd` inalterado.

## Limites

- Nao introduz Argon2id, scrypt ou aumento de iteracoes PBKDF2; segue
  no piso de `USER_ITERATIONS=64000`.
- Nao corrige a possibilidade de exaustao da tabela
  `g_attempts[AUTH_MAX_TRACKED_USERS]` por usernames forjados (atacante
  preenche todos os slots; ataque eficaz apenas em uniprocessador com
  acesso massivo). Solucao envolve LRU/hash-table e fica como fatia
  futura.
- Nao destrava ainda os entregaveis pendentes da Etapa 2 (loginwindow
  GUI desenhando pixels reais; smokes `gui-session` e `mouse-events`),
  mas eleva o piso de seguranca do backend que esses entregaveis
  consumirao.
- Nao adiciona testes host-side de `userdb_authenticate_with_policy`
  exercitando vfs/kalloc — isso exigiria stubs significativos e fica
  como fatia futura. O contrato de codigos de retorno e a regressao da
  semantica fica protegido pelos testes contratuais novos.
