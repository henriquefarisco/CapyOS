# CapyOS 0.8.0-alpha.2+20260424

Data: 2026-04-24
Canal: develop
Base: robustez da trilha `UEFI/GPT/x86_64`

## Destaques

- plano mestre de robustez criado como documento vivo com matriz de status
- novas instalacoes passam a usar DHCP por padrao, com bootstrap e retry pelo
  `networkd`
- `release-check` virou gate de release endurecida com toolchain
  `TOOLCHAIN64=elf`, stack protector, auditorias e checksums
- comandos `perf-boot`, `perf-net`, `perf-fs` e `perf-mem` foram adicionados
  ao CapyCLI
- DNS cache ganhou TTL e o navegador recebeu cache HTTP com budget e paginas
  `about:network` e `about:memory`
- smoke x64 completo valida boot, login, persistencia, update local, reboot,
  usuario comum, desktop autostart e poweroff

## Robustez e release

- `docs/plans/capyos-robustness-master-plan.md` passa a ser a fonte viva de
  acompanhamento do plano M0-M8
- `make release-check` executa testes, auditoria de layout, auditoria de
  versao, baseline self-test de boot, build `TOOLCHAIN64=elf`, ISO UEFI e
  verificacao SHA-256 dos artefatos
- `include/string.h` e `include/stdlib.h` fornecem shims freestanding para a
  trilha oficial com `x86_64-elf-*`

## Rede e internet

- `network_mode=dhcp` virou o default de nova instalacao
- o bootstrap tenta DHCP automaticamente e o `networkd` faz retry com backoff
- `net-status` e `net-dump-runtime` expoem diagnosticos de lease, tentativas e
  ultimo erro DHCP
- DNS cache aplica TTL e registra estatisticas
- o navegador controla budget de cache HTTP e expoe telemetria de rede/memoria

## Performance

- `perf-boot` mostra estagios de boot e tempo ate login
- `perf-fs` mostra estatisticas do buffer cache
- `perf-net` e `perf-mem` consolidam diagnosticos operacionais de rede e
  memoria
- `docs/performance/boot-baseline.json` estabelece baseline inicial de boot

## Seguranca e update

- `update-import-manifest` persiste catalogo local por escritor de runtime
  privilegiado controlado, sem afrouxar permissoes VFS para usuarios comuns
- artefatos de release recebem checksums verificaveis em
  `build/release-artifacts.sha256`

## Validacao

```bash
make smoke-x64-cli SMOKE_X64_CLI_ARGS='--step-timeout 30 --require-shell'
make release-check
make version-audit
```

Versao alinhada: `0.8.0-alpha.2+20260424`
