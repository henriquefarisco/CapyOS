# CapyOS 0.8.0-alpha.273+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.273+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Etapa 6 / Slice 6.7 -- idioma de sessao para apps ring-3 (i18n do CapyBrowse Text)

## Resumo executivo

Novo syscall `SYS_GET_SESSION_LANG` (=44; `SYSCALL_COUNT` 44->45) expoe o
idioma da sessao logada ao ring-3. Com ele, o **CapyBrowse Text** passa a
mostrar seus diagnosticos de rede no idioma do usuario (PT-BR/ES) em vez de
ficar fixo na base EN -- fechando o criterio de i18n da Etapa 6 para o
caminho de erro do navegador e atendendo o publico alvo PT-BR/ES. O proprio
`userland/bin/capybrowse/main.c` ja documentava este gap ("the language
defaults to the mandatory EN base until a ring-3 current session language
syscall exists"). **A Etapa 6 NAO esta fechada**: os gates externos VMware
seguem pendentes.

## Mudancas

- **Kernel -- `SYS_GET_SESSION_LANG` (`src/kernel/syscall.c`,
  `include/kernel/syscall_numbers.h`):** handler `sys_get_session_lang` le
  `session_active()`/`session_language()` (auth/session, sempre linkado --
  nao depende do `app_language` so-FULL) e devolve um codigo estavel
  `CAPY_SESSION_LANG_*` (0=pt-BR, 1=en, 2=es). Sem sessao ou idioma vazio ->
  pt-BR (espelha `app_current_language` e o invariante "selecao padrao"
  travado em `test_localization.c`); idioma nao reconhecido -> en (base de
  fallback universal). Sem argumentos. ABI **aditiva** de `capyos-base`.
- **Userland -- stub + mapeador:** `capy_get_session_lang` (stub asm em
  `userland/lib/capylibc/syscall_stubs.S` + decl em `capylibc.h`); mapeador
  **puro e host-testado** `capybrowse_session_lang_string` (codigo ->
  `"pt-BR"`/`"en"`/`"es"`) em `capybrowse_view`, cujo resultado o `net_pick`
  de `capy_net_stage_message`/`capy_net_stage_hint` consome direto.
- **App -- `userland/bin/capybrowse/main.c`:** `cb_diag_lang()` resolve o
  idioma em runtime via o syscall, em vez do `#define CAPYOS_CAPYBROWSE_LANG
  "en"` fixo. O override de compilacao `-DCAPYOS_CAPYBROWSE_LANG="xx"` foi
  preservado para forcar um idioma em testes deterministicos. O caminho
  feliz (pagina renderizada) nao usa idioma, entao continua inalterado.

## Validacao

- `build/x86_64/kernel/syscall.o` compila limpo sob os CFLAGS reais do
  kernel (objeto isolado).
- `make test` -- verde. ABI travada: `SYS_GET_SESSION_LANG == 44` e
  `SYSCALL_COUNT == 45` (`tests/userland/test_capylibc_abi.c`).
  `tests/userland/test_capybrowse_view.c` 20/20 (5 casos novos cobrindo o
  mapeamento codigo->string, incluindo fallback de codigo desconhecido).
- `make capybrowse-elf` -- linka o ELF ring-3 (stub `capy_get_session_lang`
  + mapeador resolvidos).
- `make layout-audit` -- sem warnings.
- `make version-audit` -- verde.

## Escopo pendente

- Gates externos VMware da Etapa 6 (`smoke-x64-vmware-capybrowse-text` +
  `smoke-x64-vmware-apps-basic-roundtrip`) -- operador (QEMU = feedback de
  dev; VMware = aceite oficial).
- Fetch **remoto** de modulos/browsing: assets de release publicados e
  alcancaveis + verificador Ed25519 da CapyAgent (P0). Caminho offline ja
  funciona (alpha.271).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.272+20260617` | `alpha.273+20260617` | Syscall `SYS_GET_SESSION_LANG` (i18n do CapyBrowse Text); ABI aditiva |

Sem mudanca de contrato cross-repo; os 6 repos irmaos permanecem nas versoes
de `alpha.266` (sem bump). Adicionar um syscall e uma evolucao **aditiva** do
`capyos-base` (travada por `tests/userland/test_capylibc_abi.c`), nao um novo
ABI canonico nem mudanca de formato de manifesto/quota/assinatura.

_Build: `0.8.0-alpha.273+20260617`_
