# CapyOS 0.8.0-alpha.287+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.287+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** fecho de etapa (Etapa 6) + hardening pos-fix (CI / processo)

## Resumo executivo

alpha.287+20260617: FECHA a Etapa 6 (Apps basicos do desktop maduros + CapyBrowse Text) -- os 2 gates externos VMware (smoke-x64-vmware-apps-basic-roundtrip + smoke-x64-vmware-capybrowse-text) foram validados pelo operador em VMware + UEFI + E1000, e a instalacao completa de modulos (corrigida em alpha.286) foi confirmada em runtime. 6/16 etapas concluidas; proxima e a Etapa 7 (browser usavel com web estatica moderna). Alem do fecho, este release adiciona o hardening pos-fix que fecha a lacuna que deixou o bug do alpha.286 escapar ~17 alphas ('CI verde != instalacao funciona'): (a) #2 -- gate de CI advisory de download REAL de modulos: novo alvo make smoke-x64-iso-modules-net (build + instalacao ISO completa com rede QEMU user-net real/SLIRP NAT + assercao 'Install complete'), opt-in --first-boot-net no smoke de instalacao (substitui o hack temporario revertido), e o workflow .github/workflows/modules-download.yml (workflow_dispatch + cron semanal, advisory); exercita o caminho DNS+TLS+redirect GitHub que o smoke-x64-iso da CI nunca tocou (profile=basic + SLIRP restrict=on, sem egress) -- exatamente o caminho do bug do alpha.286. (b) #3-guarda -- pin de modules-index single-sourced: novo bloco modules_index (pin/url) em VERSION.yaml + checagem em tools/scripts/audit_version_manifest.py que FALHA o version-audit se o CAPYOS_DEFAULT_MODULES_INDEX_URL em src/config/first_boot/modules.c divergir da url declarada; converte o pin C antes enterrado num valor explicito, single-sourced e CI-enforced, matando a classe 'pin manual esquecido'. Sequenciado para a Etapa 8 (como o plano ja preve): o resolve-at-publish completo (indice assinado endereçado por token de ABI + agregador resolvendo no publish) e o signer Ed25519 do CapyAgent (P0). Validado: make test verde; layout-audit limpo; version-audit verde (com a nova guarda; pin v2.13.0 casa com modules.c). Sem mudanca de runtime/ABI (so tooling/CI/docs + a guarda do audit); 6 irmaos sem bump.

## Mudancas

### #1 -- Fecho da Etapa 6
- **`docs/plans/active/capyos-master-plan.md`**: tabela sequencial (Etapa 5 -> Concluida (alpha.264), Etapa 6 -> Concluida (alpha.287), Etapa 7 -> Proxima/desbloqueada); cabecalho da secao da Etapa 6 -> concluida; criterios de aceite marcados `[x]`; "Gates externos recomendados" -> "validados" (VMware + UEFI + E1000, operador).
- **`docs/plans/STATUS.md`**: progresso 5/16 -> **6/16**; rotulo de etapa atual atualizado; nota `alpha.287` registrando o fecho + o hardening.

### #2 -- Gate de CI do download real de modulos (regressao do alpha.286)
- **`tools/scripts/smoke_x64_iso_install.py`**: novo opt-in `--first-boot-net` (liga rede QEMU user-net/SLIRP NAT real no boot de first-boot) -- substitui o hack temporario usado/revertido durante o diagnostico do alpha.286.
- **`Makefile`**: novo alvo `smoke-x64-iso-modules-net` (build ISO + instalacao completa networked + assercao `Install complete`) e knob `SMOKE_X64_MODULES_INDEX_URL`.
- **`.github/workflows/modules-download.yml`** (novo): workflow advisory (`workflow_dispatch` + cron semanal) que roda o gate -- exercita DNS+TLS+redirect GitHub, o caminho que o `smoke-x64-iso` (profile=basic + `restrict=on`) nunca tocou.

### #3 -- Guarda anti-drift do pin de modules-index
- **`VERSION.yaml`**: novo bloco `modules_index` (`pin`/`url`) como fonte unica do pin.
- **`tools/scripts/audit_version_manifest.py`**: checagem que extrai `CAPYOS_DEFAULT_MODULES_INDEX_URL` de `modules.c` e FALHA o `version-audit` se divergir de `modules_index.url`.

### Versao
- Bump `0.8.0-alpha.286` -> `0.8.0-alpha.287` (via `make bump-alpha`).

## Validacao

- `make test` -- **verde** ("Todos os testes passaram").
- `make layout-audit` -- **limpo**. `make version-audit` -- **verde** com a nova guarda ativa (`modules_index.url` == `CAPYOS_DEFAULT_MODULES_INDEX_URL` = `v2.13.0`).
- `make -n smoke-x64-iso-modules-net` -- expande corretamente (recipe + `--first-boot-net`). O download networked real subjacente foi provado em runtime na confirmacao do `alpha.286` (instalacao ISO completa sobre SLIRP: 18/18 SYN-ACK, 7 modulos `Completed: 7 Failed: 0`, "Install complete"). O novo workflow advisory roda esse caminho em `dispatch`/semanal.
- Sem mudanca de C de runtime nesta release; ISO/all64 inalterados (a CI de release reconstroi e revalida).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.286+20260617` | `0.8.0-alpha.287+20260617` | Fecho de Etapa 6 + hardening de CI/processo; sem mudanca de ABI. |

Sem mudanca de ABI nem de contrato cross-repo. Os 6 repos irmaos permanecem
inalterados (o pin de modules-index segue `CapyUI v2.13.0`, agora single-sourced
e auditado; o resolve-at-publish e o signer Ed25519 ficam para a Etapa 8).

_Build: `0.8.0-alpha.287+20260617`_
