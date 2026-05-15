# CapyOS 0.8.0-alpha.212+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch fecha um **timing side-channel critico** em
`userdb_authenticate_with_policy`, a API publica de login do sistema.
Antes, o wrapper retornava `USERDB_AUTH_LOCKED` IMEDIATAMENTE quando
`auth_policy_check_allowed` indicava que o username estava em
lockout, sem executar o PBKDF2 que normalmente custa ~50 ms. Um
atacante remoto observando a latencia da resposta podia distinguir
accounts locked (microssegundos) de accounts not-locked (~50 ms)
com **um unico probe por username** — reabrindo via timing as
informacoes que `alpha.211` removeu de `auth_policy_status` (quais
contas estao em lockout) e que `alpha.206` removeu de
`userdb_authenticate` (existencia da conta). Pior, esse leak nao
dependia de acesso a `klog`, comando shell privilegiado ou qualquer
canal local: era observavel puramente pelo wall-clock da API publica
de login. Agora o wrapper sempre paga o custo de PBKDF2 antes de
verificar lockout, eliminando o sinal.

## Anatomia do timing leak

`src/auth/user.c::userdb_authenticate_with_policy` antes:

```c
int userdb_authenticate_with_policy(const char *username,
                                    const char *password,
                                    struct user_record *out) {
    if (!username || !password) {
        return USERDB_AUTH_FAILED;
    }
    if (!auth_policy_check_allowed(username)) {
        return USERDB_AUTH_LOCKED;  // ← retorna em microssegundos
    }
    int rc = userdb_authenticate(username, password, out); // ← ~50 ms
    ...
}
```

Latencias observaveis por probe:

| Caminho                                  | Latencia |
| ---------------------------------------- | -------- |
| Username locked                          | ~1 µs    |
| Username not-locked, existe, pwd correto | ~50 ms   |
| Username not-locked, existe, pwd errado  | ~50 ms   |
| Username not-locked, NAO existe          | ~50 ms   |

Os tres ultimos sao indistinguiveis entre si (alpha.206 introduziu
o dummy salt fallback em `userdb_authenticate` justamente para
fechar enumeracao de existencia). Mas o primeiro caminho saltava aos
olhos: **50000x mais rapido**. Atacante mediu o RTT da resposta e
fez:

- **Enumeracao passiva de locked accounts**: um probe por username
  candidato → response rapida = locked.
- **Lockout escape**: monitorar timing ate uma conta especifica
  passar a responder lentamente → janelamento expirou, retomar
  credential-stuffing.
- **Triangulacao com `alpha.211`**: mesmo apos `auth-status` parar de
  listar usernames trackados e `[priv]` logs pararem de vazar
  username, o atacante reconstruia o conjunto de contas locked via
  timing da API publica.

## Mudancas

### `src/auth/user.c::userdb_authenticate_with_policy`

- Inverte a ordem entre `auth_policy_check_allowed` e
  `userdb_authenticate`: o wrapper agora sempre executa o PBKDF2
  primeiro e SOMENTE depois testa o flag de allowed.
- Quando o account esta locked, o resultado de `userdb_authenticate`
  e descartado e `user_record_clear(out)` zera o buffer de saida
  (incluindo o caso raro em que o atacante adivinha a senha correta
  de uma conta locked — o record NAO pode vazar para o caller).
- `auth_policy_record_failure` intencionalmente NAO e invocado
  quando o account ja esta locked, porque o `lockout_until_tick` e
  ancorado no momento em que o threshold foi atingido inicialmente
  (mais falhas nao estendem o janelamento) e o `failed_count`
  adicional ficaria poluido por ruido do atacante.
- Adiciona comentario SECURITY de ~40 linhas explicando o threat
  model, a relacao com alpha.211 e alpha.206, e o rate-limiting
  inerente.

### `tests/test_auth_policy.c`

- Adiciona bloco contratual alpha.212 cobrindo o lado policy do
  contrato: `auth_policy_check_allowed` deve continuar retornando 0
  para accounts locked e 1 para accounts fresh, para que o wrapper
  consiga detectar o lockout APOS pagar o custo de PBKDF2.
- A verificacao do timing equalization em si fica em revisao de
  codigo (presenca do `userdb_authenticate(..., out)` ANTES do
  `if (!allowed)` em `src/auth/user.c`) porque `src/auth/user.c`
  nao esta no host-side test binary atual (depende de VFS/kmem).

## Seguranca

- **Timing side-channel fechado.** Probes contra contas locked e
  not-locked agora retornam na mesma janela de latencia (~50 ms).
  Atacante remoto perde a capacidade de distinguir essas duas
  classes via wall-clock.
- **Composicao com alpha.211 mantida.** A privacy hardening do
  `auth_policy_status` e do `priv_log_emit` continua valendo: nao
  ha caminho via log/comando para enumeracao. Combinada com este
  patch, a unica via que o atacante tem para descobrir lockouts e
  esgotar tentativas em cada conta candidata — e cada tentativa
  custa ~50 ms.
- **Composicao com alpha.206 mantida.** A equalizacao
  existent-vs-nonexistent ja era feita por `userdb_authenticate` via
  `k_userdb_dummy_salt`. Este patch herda essa equalizacao
  automaticamente porque `userdb_authenticate_with_policy` chama
  `userdb_authenticate` em todo caminho.
- **Wipe defensivo de `out`.** Mesmo no caso em que o atacante
  adivinha a senha correta de uma conta locked, `out` e zerado
  antes do retorno. Nenhum dado de record vaza enquanto a conta
  esta em lockout.

## Desempenho

- **Custo adicional**: ~50 ms por probe contra account locked
  (antes era ~1 µs). Isto e **vantajoso** em duas dimensoes: (1)
  rate-limita credential-stuffing contra contas locked com a mesma
  janela do path normal, (2) torna enumeracao de locked targets
  impraticavel em escala (32 accounts × 50 ms = 1.6 s para varrer a
  tabela inteira de tracking, e ainda o atacante nao distingue
  locked de not-locked).
- **Custo legitimo**: usuarios reais que tentam login com conta
  propria locked esperam ~50 ms a mais que antes para a mensagem
  "conta bloqueada". Imperceptivel.
- Sem alocacao adicional, sem mudanca de estrutura de dados.

## Validação

Validado por revisão estática:

- `src/auth/user.c::userdb_authenticate_with_policy` linha
  617: chamada `userdb_authenticate(username, password, out)`
  agora aparece ACIMA do `if (!allowed)`.
- `src/auth/user.c::userdb_authenticate_with_policy` linhas
  618-623: quando locked, `out` e zerado via `user_record_clear` e
  o retorno e `USERDB_AUTH_LOCKED`.
- `src/auth/user.c::userdb_authenticate_with_policy` linhas
  574-615: comentario SECURITY de ~40 linhas explicita threat
  model, mitigacao, custo e relacao com alpha.211/alpha.206.
- `tests/test_auth_policy.c` linhas 94-115: novo bloco contratual
  alpha.212 valida `auth_policy_check_allowed`/`is_locked` para
  accounts locked.

## Compatibilidade

- ABI publica de `userdb_authenticate_with_policy` **inalterada**
  (mesma assinatura, mesmos codigos de retorno `USERDB_AUTH_OK`,
  `USERDB_AUTH_FAILED`, `USERDB_AUTH_LOCKED` que sao locked por
  contrato em `test_auth_policy.c`).
- Callers que dependem de codigos de retorno (system_setup.c,
  futuro GUI submit) continuam funcionando sem mudanca.
- Latencia observavel mudou: callers que cronometravam o wrapper
  para distinguir locked de not-locked (caso existisse algum, o que
  e estritamente atacante) deixam de funcionar — comportamento
  desejado.

## Limites

- Nao implementa Ed25519 real (RFC 8032).
- Nao implementa Argon2id opt-in.
- Nao destrava entregaveis pendentes da Etapa 2 (loginwindow GUI
  real, smokes `gui-session`/`mouse-events`).
- Nao adiciona controle de acesso ao comando shell `auth-status`
  (sigue acessivel a todos, agora com payload sem PII desde
  alpha.211).
