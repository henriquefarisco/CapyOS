# CapyOS 0.8.0-alpha.211+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch fecha **duas fugas de privacidade reais** na trilha de
autenticacao: (1) `auth_policy_status`, alcancado pelo comando shell
`auth-status` sem nenhum check de privilegio, emitia uma lista
completa dos usernames rastreados pelo policy table junto com a
contagem de falhas e o estado de lockout de cada um — qualquer sessao
local conseguia enumerar usuarios reais, ler tentativas de login
falhadas alheias e identificar contas bloqueadas; (2)
`privilege_log_emit` em `[priv] denied:` e `[priv] granted:`
adicionava `actor=<username>` em todo log, vazando o nome do
principal em qualquer denial de UI/shell, e essa string chegava ao
klog ring (visivel a qualquer leitor do log + persiste em crash
buffers + dumps de panic). Ambos os comportamentos foram substituidos
por equivalentes que preservam o sinal de auditoria (counts
agregados; role do ator) sem expor PII.

## Anatomia das fugas fechadas

### Fuga A: `auth_policy_status` listava usernames

`src/auth/auth_policy.c::auth_policy_status` antes:

```c
print("\nTracked accounts:\n");
for (int i = 0; i < AUTH_MAX_TRACKED_USERS; i++) {
  if (!g_attempts[i].username[0]) continue;
  print("  "); print(g_attempts[i].username);
  print(" fails="); ap_print_u32(print, g_attempts[i].failed_count);
  print(g_attempts[i].locked ? " LOCKED" : " ok");
  print("\n");
}
```

Esta funcao e o backend de `src/shell/commands/extended.c::cmd_auth_status`
(comando `auth-status`), que **nao verifica privilegio**. Qualquer
usuario com shell — incluindo `user` comum, conta de recuperacao e
sessao guest — podia rodar `auth-status` e obter:

1. **Lista de todas as contas com falha de login**: enumeracao
   passiva de usuarios reais.
2. **Contagem exata de falhas por conta**: sinal de strategy
   (atacante sabe quais alvos estao no limiar do lockout).
3. **Estado de lockout por conta**: atacante pode aguardar o
   janelamento expirar antes de retomar credential stuffing contra
   o alvo especifico.

### Fuga B: `priv_log_emit` anexava username em `[priv]` logs

`src/auth/privilege.c::priv_log_emit` antes:

```c
if (actor && actor->username[0]) {
    const char *sep = " actor=";
    ...
    while (actor->username[u] && pos + 1 < sizeof(line)) {
        line[pos++] = actor->username[u++];
    }
}
...
klog(level, line);
```

Cada chamada de `privilege_log_denied`/`privilege_log_granted`
disparava com `actor = session_active()->user`, ou seja: TODO denial
de UI/shell que invoca o helper anexa o username completo do usuario
ativo. Sites concretos no codigo:

- `src/apps/settings.c:151` — `settings-add-user` denied quando user
  nao-admin abre Settings.
- `src/shell/commands/user_manage.c:174` — denied generico para
  acoes administrativas tentadas por nao-admin.
- `src/shell/commands/user_manage.c:375` — `set-pass:other` denied
  quando user tenta mudar senha de outro sem ser admin.

`klog` e:
- consumido por qualquer ferramenta com acesso ao log ring;
- dumped verbatim em panic e crash buffers;
- forwardado por `src/services/logger`-style consumers.

Resultado: o username vazava bem alem da fronteira de auth.

## Mudancas

### `src/auth/auth_policy.c::auth_policy_status`

- Substitui o loop que imprimia `<username> fails=<n> LOCKED|ok` por
  duas linhas agregadas: `Tracked accounts: <N> (locked: <M>)`.
- Adiciona comentario PRIVACY de ~25 linhas detalhando o threat
  model (enumeracao de usuarios, sinais de strategy, lockout escape)
  e por que counts agregados preservam o valor de operacao sem PII.
- A configuracao publica (`max_attempts`, `min_password_length`,
  `audit`) continua sendo emitida — sao parametros, nao PII.

### `src/auth/auth_policy.c::auth_policy_tracked_count`

- Nova funcao publica que retorna o numero de slots ocupados em
  `g_attempts[]`. Util para diagnostico e para tests que precisam
  validar nao-poluicao da tabela.

### `src/auth/auth_policy.c::auth_policy_locked_count`

- Nova funcao publica que retorna o numero de slots com `locked=1`.
  Util para metricas e dashboards sem revelar quais contas estao
  bloqueadas.

### `src/auth/auth_policy.c::auth_policy_is_tracked`

- Exposta apenas em builds com `UNIT_TEST` definido (guarda
  `#if defined(UNIT_TEST)` no `.c` e no header). Retorna 1 se o
  username tem slot alocado, 0 senao. Permite que tests validem
  rastreamento de username especifico sem expor essa capacidade em
  producao (onde poderia ser vetor de enumeracao similar a
  `auth_policy_status` original).

### `src/auth/privilege.c::priv_log_emit`

- Substitui o trecho que anexava `actor=<username>` pelo equivalente
  `actor_role=<role>`. O role e um identificador de classe (admin,
  user, ...) ligado a politica de privilegio do sistema, nao um
  identificador unico do principal.
- Adiciona comentario PRIVACY de ~20 linhas detalhando os callsites
  afetados (settings, user-manage, set-pass:other) e o por que do
  trade-off (sinal de auditoria preservado: "denied spike from
  non-admin role" continua acionavel; PII removido).
- Defensive: quando `actor->role[0]` e vazio, omite a suffix; quando
  `actor == NULL`, omite a suffix sem crash.

### `include/auth/auth_policy.h`

- Declara `auth_policy_tracked_count` e `auth_policy_locked_count`.
- Declara `auth_policy_is_tracked` sob `#if defined(UNIT_TEST)`.
- Documenta no header o contrato privacy-preserving do
  `auth_policy_status`.

### `tests/test_auth_policy.c`

- Substitui as asserts que grepavam o status output por `"newcomer"`
  / `"fill-00"` (que agora nao aparecem no status output) por
  `auth_policy_is_tracked("newcomer") == 1` e `auth_policy_is_tracked("fill-00") == 0`.
- Adiciona asserts adicionais validando o **contrato negativo** do
  status output: `strstr(g_status_capture, "newcomer") == NULL` e
  `strstr(g_status_capture, "fill-") == NULL`.
- Adiciona asserts dos counts agregados: `auth_policy_tracked_count`
  e `auth_policy_locked_count` retornam valores esperados apos
  saturacao da tabela.

### `tests/test_privilege.c`

- Adiciona `test_privilege_log_omits_username` que captura o klog
  ring via `capture_klog` e valida:
  - `denied/granted` nao contem `alice` nem `carol` (usernames
    testados).
  - `denied/granted` contem `actor_role=admin` quando role esta
    populado.
  - `denied` com role vazio omite `actor_role=` mas mantem
    `verdict: action`.
  - `denied` com `actor == NULL` nao crasha e emite verdict/action.
- Adiciona include de `kernel/log/klog.h`.

## Segurança e privacidade

- **Enumeracao de usuarios via `auth-status` fechada.** Sessoes
  locais nao-admin nao conseguem mais listar quem usa o sistema.
- **Sinal de strategy removido.** Atacantes nao conseguem mais ler
  failure_count das contas alvo via comando publico.
- **Lockout escape neutralizado.** Atacantes nao conseguem mais ver
  quando o janelamento de lockout de uma conta especifica vai
  expirar.
- **klog/crash dumps sem username.** Qualquer ferramenta que le o
  log ring ou inspeciona crash buffers nao recebe mais o username
  via `[priv]` lines.
- **Audit signal preservado.** O par `verdict: action` (denied/granted +
  acao) continua sendo emitido. O role do ator e adicionado quando
  presente. Spike de denials de um role nao-admin permanece visivel
  como indicador.

## Desempenho e escalabilidade

- `auth_policy_status`: O(AUTH_MAX_TRACKED_USERS) iteracoes para os
  dois novos counters. O loop antigo era O(AUTH_MAX_TRACKED_USERS)
  tambem mas com 4 prints por linha. Custo agora e menor.
- `auth_policy_tracked_count` / `auth_policy_locked_count`: cada uma
  e O(AUTH_MAX_TRACKED_USERS = 32). Custo desprezivel.
- `priv_log_emit`: mesma O(line length) que antes. Sem mudanca.
- ABI inalterada para callers fora de auth/auth_policy/privilege.

## Validação

Validado por revisão estática. Pontos cobertos:

- `src/auth/auth_policy.c::auth_policy_status` agora emite
  `Tracked accounts: <N> (locked: <M>)` em vez do loop por
  username; comentario PRIVACY explicito.
- `auth_policy_tracked_count` e `auth_policy_locked_count`
  implementadas e expostas em `include/auth/auth_policy.h`.
- `auth_policy_is_tracked` exposta apenas sob `#if defined(UNIT_TEST)`.
- `src/auth/privilege.c::priv_log_emit` substitui `actor=<username>`
  por `actor_role=<role>`; comentario PRIVACY explicito.
- `tests/test_auth_policy.c` atualizado para usar
  `auth_policy_is_tracked` e os counts; asserts negativos validam
  o contrato de privacidade.
- `tests/test_privilege.c::test_privilege_log_omits_username`
  cobre denied/granted com role populado/vazio e com `actor=NULL`.
- Includes consistentes (`kernel/log/klog.h` adicionado em
  `test_privilege.c`).

## Compatibilidade

- ABI publica de `auth_policy_check_allowed`, `auth_policy_record_success`,
  `auth_policy_record_failure`, `auth_policy_is_locked`,
  `auth_policy_unlock`, `auth_policy_validate_password`,
  `auth_policy_init`, `auth_policy_set_config` **inalterada**.
- ABI publica de `auth_policy_status` **inalterada** na assinatura;
  output do callback `print` muda (linhas por usuario substituidas
  por uma linha agregada). Consumidores que parseiam o output
  textual de `auth-status` precisam adaptar.
- ABI publica de `privilege_log_denied`/`privilege_log_granted`
  **inalterada** na assinatura. Output do klog muda
  (`actor=<username>` -> `actor_role=<role>`). Consumidores que
  parseiam `[priv]` log lines precisam adaptar.
- Tests existentes (`test_audit_events`, `test_update_*`,
  `test_user_home`, `test_login_runtime`) nao tocam nesses
  campos especificos e continuam funcionais sem mudanca.

## Limites

- Nao adiciona controle de acesso ao comando shell `auth-status`. O
  comando continua disponivel para qualquer sessao, mas agora o
  output ja nao revela PII. Adicionar restricao admin em
  `cmd_auth_status` seria estritamente mais conservador, mas mudaria
  a UX de diagnostico para usuarios legitimos sem aumentar a postura
  de seguranca (o output ja nao tem PII).
- Nao remove o uso de `klog` para eventos `[priv]`. O canal de log
  continua sendo usado, agora apenas com payloads sem PII. Um audit
  log separado, com controle de acesso proprio e politica de
  retencao distinta, e um slice futuro.
- Nao altera o tracking interno em `g_attempts[]`. A tabela continua
  guardando username em memoria para o algoritmo de lockout
  funcionar; a mudanca e apenas no que sai pelos canais de
  observabilidade.
- Nao destrava entregaveis pendentes da Etapa 2 (loginwindow GUI
  real, smokes `gui-session`/`mouse-events`).
