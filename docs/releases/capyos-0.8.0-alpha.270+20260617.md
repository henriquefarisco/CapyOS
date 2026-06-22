# CapyOS 0.8.0-alpha.270+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.270+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Correcao de memoria nao inicializada no first-boot (reliability/seguranca)

## Resumo executivo

Corrige uma leitura de memoria de pilha nao inicializada no caminho de
primeiro boot: o campo `browser_homepage` era gravado em
`/system/config.ini` com lixo da pilha no lugar da homepage padrao. **A
Etapa 6 NAO esta fechada**: os gates externos VMware
`smoke-x64-vmware-capybrowse-text` e `smoke-x64-vmware-apps-basic-roundtrip`
seguem pendentes (QEMU = feedback de dev; VMware = aceite oficial).

## Root cause + fix

- **Root cause (`src/config/first_boot/program.c`):**
  `setup_write_settings_and_mark()` monta a struct `system_settings` campo
  a campo (hostname, theme, keyboard, language, update_channel,
  network_mode, service_target, ipv4_*, splash, diagnostics) mas **nunca
  setava `browser_homepage`**, deixando-o como memoria de pilha nao
  inicializada. `config_write_settings_file()` emite a linha
  `browser_homepage=` sempre que o primeiro byte do campo e nao-zero,
  entao o `config.ini` do primeiro boot podia ser persistido com lixo da
  pilha (observado como `browser_homepage=<lixo>` no dump de config do
  smoke ISO QEMU; os demais campos estavam corretos, isolando o campo).
- **Fix:** inicializa `browser_homepage` com o default
  `https://wikipedia.org`, espelhando `system_settings_set_defaults()` e
  `apply_default_settings()`. Para de vazar memoria de pilha nao
  inicializada para um arquivo persistido e mantem o valor deterministico.

## Validacao

- `src/config/first_boot/program.c` compila limpo (objeto isolado).
- `make test` + `make smoke-x64-iso` rodam nos gates de CI (host Linux).
- `make version-audit` -- verde.

## Escopo pendente

- Gates externos VMware da Etapa 6 (`smoke-x64-vmware-capybrowse-text` +
  `smoke-x64-vmware-apps-basic-roundtrip`) seguem como aceite oficial.
- Resolver DNS ativo (hoje cache-only) para navegacao por hostname real.
- Assinatura Ed25519 (CapyAgent) segue P0.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.269+20260617` | `alpha.270+20260617` | Fix de memoria nao inicializada (browser_homepage) no first-boot |

Sem mudanca de ABI base do kernel nem de contrato cross-repo; os 6 repos irmaos
permanecem nas versoes de `alpha.266` (sem bump).

_Build: `0.8.0-alpha.270+20260617`_
