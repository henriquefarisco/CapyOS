# CapyLang — contrato de integração com CapyOS

**Status:** referência para desenvolvimento apartado.
**Integração planejada:** Etapa 15, após CapyDisplay, scheduler, sandbox, package/ABI e APIs de app estarem maduros.
**Repositório externo atual:** `CapyLang`.

## Migração inicial

Nenhuma implementação CapyLang acoplada foi encontrada no código ativo do
CapyOS. O repositório externo foi inicializado como owner do parser, bytecode,
VM, host ABI mock e benchmarks determinísticos futuros.

## Fronteira

O projeto CapyLang apartado deve entregar:

- parser e validação sintática;
- bytecode ou IR versionado;
- VM/interpreter sem JIT obrigatório;
- standard library mínima independente de OS;
- runner host;
- suite de testes e golden outputs.

O CapyOS base deve entregar:

- loader sandboxed de bytecode;
- host ABI versionada;
- permissões por app/script;
- bindings para tempo, log, FS permitido, config, 2D, input e métricas;
- integração com package manager e launcher;
- gates externos de execução em VM oficial.

## Host ABI inicial

A VM não deve chamar syscalls CapyOS diretamente. Toda interação com o sistema passa por uma tabela versionada de host functions.

Módulos mínimos:

- `time`: monotonic time, delta frame, sleep/yield com budget.
- `log`: logs redigidos e sem segredos.
- `fs`: leitura/escrita somente em sandbox de app.
- `config`: leitura/escrita de preferências do app.
- `gfx2d`: clear, rect, line, text, sprite/bitmap handle.
- `input`: teclado, mouse, game-loop input snapshot.
- `metrics`: counters de frame, CPU budget aproximado e eventos de benchmark.

## Artefatos

- Bytecode deve incluir magic, versão, flags, target ABI e checksum.
- Scripts fonte não são requisito para execução no CapyOS release.
- Pacotes devem declarar permissões e versão mínima da host ABI.

## Segurança

- Sem JIT na primeira integração.
- Sem ponteiros crus entre bytecode e host.
- Recursos host representados por handles opacos.
- Budget de instruções/tempo por frame.
- Erro de script encerra o app/script, não o desktop.
- Logs não podem expor caminho privado, credenciais ou conteúdo sensível por padrão.

## Benchmarks Snake/Asteroids

`Snake` e `Asteroids` devem existir em dois modos:

- modo interativo para usuário;
- modo determinístico para benchmark com input replay fixo.

Métricas mínimas:

- FPS médio e p95 frame time;
- latência de input simulada;
- contagem de instruções bytecode por frame;
- memória máxima observada;
- resultado determinístico final do replay.

## Testes apartados obrigatórios

- parser/bytecode golden tests;
- VM arithmetic/control-flow tests;
- host ABI mock tests;
- sandbox denial tests;
- benchmark deterministic replay tests.

## Gate de integração CapyOS

Quando a Etapa 15 estiver ativa, recomendar execução externa de:

- `make smoke-x64-vmware-capylang-automation`;
- `make smoke-x64-vmware-capylang-bench-games`;
- `make smoke-x64-vmware-capylang-benchmark-regression` quando entrar no baseline 1.0.
