# Benchmark harness — contrato de integração com CapyOS

**Status:** referência para desenvolvimento apartado.
**Integração planejada:** Etapa 15 para benchmarks CapyLang; Etapa 16 para baseline regressivo 1.0.
**Repositório externo atual:** `CapyBenchmark`.

## Migração inicial

Nenhum harness de benchmark acoplado foi encontrado no código ativo do CapyOS.
`CapyBenchmark/src/harness/` foi inicializado com modelo de relatório e
comparador de baseline. `Snake` e `Asteroids` continuam planejados como
benchmarks CapyLang futuros.

## Fronteira

O projeto apartado pode desenvolver:

- formato de métricas;
- replay determinístico de input;
- comparação de baseline;
- relatório;
- score interno;
- jogos benchmark contra backend mock.

O CapyOS base fornece:

- relógio monotônico real;
- scheduler real;
- compositor/render backend;
- input real ou sintético autorizado;
- coleta de métricas de sistema;
- release gate e armazenamento de baseline.

## Métricas mínimas

- FPS médio;
- p95/p99 frame time;
- latência de input simulada;
- uso aproximado de CPU;
- memória máxima;
- eventos perdidos;
- checksum determinístico do estado final.

## Formato de relatório

O relatório deve conter:

- nome do benchmark;
- versão do benchmark;
- versão do runner;
- plataforma;
- seed/replay id;
- métricas;
- pass/fail contra baseline;
- motivo explícito em caso de falha.

## Regras

- Benchmark não é sucesso por apenas abrir a janela.
- Resultado precisa ser determinístico quando usando replay fixo.
- Baseline deve tolerar variação limitada de VM, mas falhar regressões grandes.
- Benchmark não pode exigir privilégio extra.

## Testes apartados obrigatórios

- replay deterministic tests;
- baseline comparator tests;
- report serialization tests;
- failure classification tests.

## Gate de integração CapyOS

Quando as etapas correspondentes estiverem ativas, recomendar execução externa de:

- `make smoke-x64-vmware-capylang-bench-games`;
- `make smoke-x64-vmware-capylang-benchmark-regression`.
