# CapyOS 0.8.0-alpha.224+20260514

## Entrega

`alpha.224` entrega o planner transacional read-only para migracao de volumes cifrados legacy para o layout header-managed.

## Antes

- `alpha.223` ja classificava volumes header-managed versus legacy e bloqueava a migracao destrutiva ingenua.
- Ainda faltava transformar o preflight em um plano operacional com ranges de LBA, direcao de copia, scratch transacional e blockers claros.
- Uma copia in-place sem planejamento poderia sobrescrever blocos source ainda necessarios ou deixar o volume irrecuperavel apos power loss.

## Agora

- `include/security/volume_provider.h` adiciona `struct volume_provider_rekey_plan`, status de plano, direcao de copia reversa e blocker de scratch transacional.
- `src/security/volume_provider.c::volume_provider_rekey_plan` consome o preflight de `alpha.223` e retorna:
  - no-op seguro para volumes ja header-managed;
  - blocked shrink quando CAPYFS consome o raw device inteiro;
  - blocked scratch quando nao ha bloco livre apos o range alvo;
  - ready quando ha source `0..N-1`, target `1..N`, scratch dedicado, copia reversa e estimativas de I/O conservadoras incluindo checkpoint em scratch e commit final.
- `tests/test_volume_provider.c` sobe para 17 funcoes cobrindo no-op moderno, ready com scratch, blocked sem scratch e blocked por shrink.

## Impacto

- Usuario final: nenhuma migracao destrutiva e executada; volumes legacy continuam montando pelo fallback.
- Sistema: o executor futuro recebe um contrato deterministico que evita aplicar writes quando nao ha condicao de rollback/abort seguro.
- Segurança: o planner falha fechado por depender do preflight autenticado/validado e nao escreve blocos.
- Escalabilidade: o plano expoe contagem de blocos e estimativas de I/O para permitir batching/checkpoint no executor futuro.

## Proximo passo

`alpha.225` deve implementar o executor transacional que aplica relocation/re-encryption com checkpoint em scratch, rollback/abort seguro e commit do header somente no final.

## Validação recomendada fora desta máquina

- `make test`
- `make all64`
