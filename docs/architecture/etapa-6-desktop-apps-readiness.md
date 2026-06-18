# Etapa 6 — Apps básicos do desktop maduros + CapyBrowse Text: readiness audit + tracker por slice

> **ESTADO: ATIVA desde `alpha.264`** (Etapa 5 fechada e validada
> externamente). Este documento abre a Etapa 6 como a Etapa 5 abriu: com
> uma auditoria de estado read-only do que **já existe** no lado CapyOS
> versus o que falta, e um plano por slice. A regra sequencial
> (`docs/plans/active/capyos-master-plan.md` §1) foi respeitada — a Etapa 6
> só abriu após o gate externo da Etapa 5.
>
> **Definição autoritativa:** `capyos-master-plan.md` §9. Esta etapa é
> grande e majoritariamente de **repos desacoplados**: o lado CapyOS
> entrega **adaptadores** e o **seam de integração**, não os cores.

**Atualizado:** 2026-06-11 · **Versão base:** `0.8.0-alpha.265+20260611`

---

## 1. Fronteira de propriedade (o que é CapyOS vs sister)

Conforme `.windsurf/rules/40-decoupled-development.md` e
`docs/architecture/decoupled-development-contracts.md`:

- **CapyOS owns:** compositor, janelas, input, fontes, surfaces, tema,
  framebuffer, damage tracking, lifecycle de processo/app, sandbox/FS
  permitido, o seam de rede/HTTPS (TLS da Etapa 5), o adaptador
  `services/capypkg` e o `module_gate`. Os **adaptadores** que ligam os
  cores sister ao runtime são CapyOS.
- **Sister owns (não entra no kernel):** o widget/desktop-session
  (`CapyUI`, ABIs `capy-ui-widget` / `capy-ui-desktop-session`) e o core
  HTML-to-text / HTML+CSS / display-list do browser (`CapyBrowser`, ABI
  `capy-browser-core`; subset textual publicado em `CapyBrowser v0.6.0`).

Contratos autoritativos relevantes:
- `docs/reference/integration/browser-core-integration-contract.md`
- `docs/reference/integration/capyui-widget-integration-contract.md`
- `docs/reference/integration/compatibility-matrix.md` (pins)

## 2. Estado atual CapyOS-side (auditoria)

**Já existe e está em produção** (não reinventar):

- **i18n:** `src/lang/localization.c` + `include/lang/localization.h`
  (`localization_select(lang, pt_br, en, es)`, `localization_text_for`,
  normalização/suporte de idioma) e `src/lang/app_language.c` +
  `include/lang/app_language.h` (`app_current_language()`, macro
  `APP_T(pt,en,es)`), usados pelos apps GUI desde a F4 i18n (2026-05-03).
- **Apps in-tree fallback + CapyUI:** `src/apps/`, `src/gui/desktop/`,
  `src/gui/window/` (fallback de paridade) e os apps/sessão de `../CapyUI`
  quando o sibling existe (detecção no Makefile). Inclui settings,
  file_manager, text_editor, taskbar, desktop_icons.
- **Tema:** entregue na Etapa 1 (sistema de tema consumido pelos apps).
- **Rede/HTTPS:** **novo na Etapa 5** — `capy_net`/`capy_http_get` agora
  faz HTTPS real (handshake BearSSL userland), DNS/TCP/TLS fail-closed.
  Disponível para o `CapyBrowse Text` usar.
- **Pacotes:** adaptador `services/capypkg` (manifesto, install, quota,
  path-traversal-safe) — base para distribuir apps como pacotes.

**Gaps reais da Etapa 6** (candidatos a slice de código):

1. **Seam `CapyBrowse Text`:** integrar o core HTML-to-text de
   `CapyBrowser` (`capy-browser-core`) via o contrato, sobre o HTTPS da
   Etapa 5, com extração de **lógica pura** primeiro (sem ampliar
   parser/render acoplado in-tree). Diagnóstico amigável de DNS/TCP/TLS/HTTP.
   **Publicação desbloqueada em 2026-06-11:** `CapyBrowser v0.6.0` publica
   `org.capyos.browser.text` via `make package STAGE=text`, com `depends=`
   vazio por design.
2. **Default EN obrigatório — RESOLVIDO na Slice 6.5 (reverificado em
   2026-06-17):** o critério §9 separa dois conceitos que o código já trata
   corretamente: (a) a **seleção default de idioma é PT-BR** (herança do
   CapyOS, invariante "seleção padrão" travado em `test_localization.c`);
   (b) **EN é o fallback base obrigatório quando falta uma string do idioma
   selecionado** — implementado em `localization_select` (ES e PT-BR caem em
   EN; EN ausente vira ""). Logo não há gap aberto: `localization_index_for`
   retornar `LANG_PT_BR` para idioma NULL/desconhecido é a *seleção* default
   pretendida, não uma falha do *fallback de strings*. Nada a inverter.
3. **Maturidade dos apps básicos:** garantir que cada app (File Manager,
   Text Editor, Settings, Image Viewer, Calculator, Log Viewer,
   Notes/Calendar, Media Player áudio/imagem) abre, executa função
   primária e fecha sem crash, com strings localizadas e tema.
4. **Adaptadores de lifecycle/toolkit:** janela/input/FS/tema/lifecycle
   por contrato CapyUI; toolkit mínimo (button, list, textbox, dialog,
   menu) consumido por contrato, não duplicado in-tree.
5. **Ícones oficiais + launcher/taskbar** e **acessibilidade básica**
   (atalhos consistentes, contraste mínimo).

## 3. Plano por slice (proposto)

> Sequência por ROI + dependência. Cada slice fecha código + testes host
> (onde a lógica é pura) + docs; o gate externo fica para o fim da etapa.

- **Slice 6.1 — readiness audit + plano (este documento).** Mapeia
  existente vs. gaps, fixa a fronteira de propriedade e a disciplina de
  "extrair lógica pura antes de acoplar". **Concluído.**
- **Slice 6.2 — base do diagnóstico de rede (`capy_net_strerror`).**
  Strings EN estáveis (estilo POSIX `strerror`) para os 11 códigos
  `capy_net_err_t` em `userland/lib/capylibc-net/capy_net_error.c`, com
  teste host. CapyOS-owned (a UX de erro é do sistema base, ver §1),
  host-testável e **não bloqueado pelo core**. É a base EN sobre a qual o
  app monta o diagnóstico amigável. **Entregue in-tree** (host test em
  `tests/userland/test_capylibc_net.c`).
- **Slice 6.3 — diagnóstico de rede do CapyBrowse Text (entregue in-tree).**
  `capy_net_diagnose_stage(net_err, tls_state)` + `capy_net_stage_name`
  classificam o estágio DNS/TCP/TLS/HTTP (combinando o erro de rede com
  `capy_tls_last_state()`, corrigindo a **conflação de falha TLS com "feature
  not supported"**); `capy_net_stage_message(stage, lang)` dá a **mensagem
  amigável localizada** PT-BR/EN/ES (fallback EN obrigatório, ASCII como o
  catálogo do kernel). Tudo em `capy_net_error.c`, host-testado, **sem tocar o
  caminho HTTPS da Etapa 5** e sem JS. O vocabulário vive no userland porque o
  ring-3 não linka o `localization_select` do kernel; o app CapyBrowse Text só
  consome `diagnose_stage` + `stage_message` na sua tela de erro.
  **Extensão (2026-06-17): `capy_net_stage_hint(stage, lang)`** — uma segunda
  linha **acionável** (o que o usuário pode FAZER: verificar rede/endereço,
  certificado não confiável, etc.) localizada PT-BR/EN/ES com fallback EN,
  ASCII puro, impressa sob a mensagem pelo `cb_fail`/caminho de erro do
  `userland/bin/capybrowse/main.c`. `CAPY_NET_STAGE_OK` não tem hint (""). Pura,
  host-testada em `tests/userland/test_capylibc_net.c` (`test_stage_hint`,
  suite capylibc-net 143/143). Aditiva — assinaturas existentes intactas.
- **Slice 6.4 — consumo do core HTML-to-text (`capy-browser-core`).**
  **Adapter entregue in-tree** — consome a ABI publicada (`capy_html_to_text` /
  `struct capy_text_doc` do `CapyBrowser v0.6.0`) **sem reinventar** (lição
  alpha.254):
  - **Pré-requisito:** lib de string freestanding do `capylibc` que o core
    exige (`<string.h>`: `capy_str_ops.h` cores + `userland/include/string.h` +
    `capylibc/string.c`, reusando o `capy_word_memcpy/memset` auditado; host
    test; guarda `#include_next`/`UNIT_TEST` p/ não sombrear o `<string.h>` do
    host nos testes).
  - **6.4a:** sibling-detection do `CapyBrowser` no Makefile (conjunto
    `STAGE=text` autoritativo = url parse/normalize/origin + html
    entities/tokenizer/text_emit; sem DOM/CSS/codecs → `depends=` vazio),
    espelhando o padrão CapyUI. **Colisão de símbolo resolvida** (pega no
    `make capybrowse-elf`): o core e o `capylibc-net` exportam ambos
    `capy_url_parse` (parsers de URL distintos). O adapter renomeia o do **core**
    via `-Dcapy_url_parse=capybrowse_core_url_parse` (escopo só nas TUs do core +
    `#define` casado no `main.c` depois do `capy_net.h`; **fora do `HOST_CFLAGS`**
    para não renomear o `capy_url_parse` do net nos testes host).
  - **6.4b:** binário ring-3 `userland/bin/capybrowse` (fetch HTTPS da Etapa 5
    → `capy_html_to_text` → formatter → stdout; falha → diagnóstico amigável via
    `capy_net_diagnose_stage` + `capy_net_stage_message`) **+ view formatter
    puro host-testado** (`capybrowse_view`: título + corpo + links `[n]`
    resolvidos + contagem de warnings + truncamento).
  - **6.4c:** render por stdout **reusando `terminal.c`** (word-wrap/scroll) —
    sem duplicar.
  - **Smoke (entregue in-tree):** latch puro `capybrowse_text_smoke` (host-
    testado, espelha o `tls_handshake_smoke`) + embedding de boot **gated
    `CAPYOS_CAPYBROWSE_SMOKE`** (registro do blob em `embedded_progs.c`, hook
    `kernel_boot_run_capybrowse` em `user_init.c`, branch em `kernel_main.c`,
    latch no `process_exit`, blob + alvo `make smoke-x64-vmware-capybrowse-text`
    no Makefile) — **default OFF → build de produção byte-idêntico**.
  **Fixes de integração cross-repo no link `capybrowse-elf` (resolvidos):**
  (1) colisão de símbolo `capy_url_parse` (o core e o `capylibc-net` exportam
  ambos) → namespacing do símbolo do **core** (`-Dcapy_url_parse=...` só nas TUs
  do core + `#define` casado no `main.c`); (2) `time()` que o BearSSL X.509
  referencia no link freestanding ring-3 → stub em `capy_tls_backend.c` (ao lado
  do `br_prng_seeder_system`, guardado por `CAPYOS_TLS_USERLAND_HANDSHAKE` +
  `!UNIT_TEST`; compartilhado, também alinha o `tls_smoke`, que ganhou
  `CAPYLIBC_STRING_OBJS` no link). **Build validado externamente:** `make test`
  verde; `make capybrowse-elf` linka; e `make all64 PROFILE=full
  CAPYOS_TLS_USERLAND_HANDSHAKE=1 CAPYOS_CAPYBROWSE_SMOKE=1` + `iso-uefi`
  produzem a **ISO bootável** — kernel + os 4 hooks de boot
  (`process.c`/`embedded_progs.c`/`user_init.c`/`kernel_main.c`) + blob do
  `capybrowse` + core CapyBrowser, tudo linkado num `capyos64.bin`. **Pendente
  (externo):** só o **boot VMware** — `make smoke-x64-vmware-capybrowse-text`
  exige `SMOKE_X64_VMWARE_ARGS="--vmx … --serial-log … --timeout …"` (a VM +
  servidor HTTP/HTTPS controlado; ver o playbook §5.C). **Não executa JavaScript.**
- **Slice 6.5 — EN como fallback obrigatório (`localization_select`).**
  **Entregue in-tree.** Evidência decisiva no teste host
  (`test_localization.c`): a **seleção default é PT-BR por desígnio**
  (invariante "seleção padrão" travado por teste) — logo "EN default
  obrigatório" do §9 significa EN como **fallback** mandatório, não inversão
  da seleção de idioma. Mudei o fallback de string ausente de
  `localization_select` para **EN** (base universal: ES/PT-BR ausentes → EN,
  nunca outro idioma localizado; EN ausente → vazio), preservando PT-BR como
  seleção default e PT-BR/ES como idiomas completos selecionáveis. Regressão
  host estendida (pt-BR/es com string ausente → EN).
- **Slice 6.6 — maturidade dos apps básicos** (roundtrip abre/usa/fecha
  sem crash; falha de um app não derruba o desktop) por contrato CapyUI,
  + ícones/launcher/taskbar e acessibilidade básica.
  - **Foundation in-tree (host-testada):** latch puro `apps_roundtrip_smoke`
    (`include/kernel` + `src/kernel` + `_io` + stub + `tests/kernel/test_apps_roundtrip_smoke_gate.c`,
    ligado em `TEST_SRCS`/`CAPYOS64_OBJS`/`test_runner`), espelhando o padrão
    `capybrowse_text_smoke`: conta **N saídas limpas** de app (`code 0`) →
    marcador único `[smoke] apps-basic-roundtrip ready`; saída não-limpa (app
    crashado) não conta e não dispara.
  - **Já pronto kernel-side:** isolamento de crash (`process_exit` não derruba
    kernel/desktop; validado pelo gate `thread-crash-survives`). Localização EN
    fallback (6.5) e tema (Etapa 1) entregues; cobertura de strings/tema dos
    apps é responsabilidade do CapyUI.
  - **Pendente (acoplado ao CapyUI + orquestração a definir, fora desta
    máquina):** lançar os apps no boot (hook em `process_exit` gated
    `CAPYOS_APPS_ROUNDTRIP_SMOKE` + embedding/orquestração — apps GUI não
    "saem 0" como os binários de smoke single-shot) + o alvo
    `make smoke-x64-vmware-apps-basic-roundtrip`. **Proposta de design da
    orquestração:** [`etapa-6-apps-roundtrip-orchestration.md`](etapa-6-apps-roundtrip-orchestration.md).

> **Reordenação vs. o rascunho inicial:** o consumo do core (antes 6.2) foi
> movido para 6.4 porque o `capy-browser-core` ainda não tinha sido publicado — os
> slices de código que **não** dependem do core (diagnóstico de rede 6.2/6.3,
> sobre o HTTPS recém-entregue na Etapa 5) vêm primeiro. O primeiro slice de
> código entregue é o **6.2**; 6.1 é a abertura/auditoria. Em 2026-06-11, o
> handoff `CapyBrowser v0.6.0` removeu o bloqueio de publicação da 6.4; a
> integração em runtime ainda depende do adapter CapyOS e do smoke externo.

## 4. Critérios de aceite (espelham §9 do master plan)

- [ ] Cada app abre, executa função primária e fecha sem crash.
- [ ] Falha de um app não derruba o desktop.
- [ ] `CapyBrowse Text` abre páginas de texto/HTML simples, mostra erros
      claros de DNS/TLS/HTTP e **não** executa JavaScript.
- [ ] HTML-to-text e widget layout entram por **contratos versionados**,
      com adaptador CapyOS pequeno e testável.
- [ ] Apps usam o tema da Etapa 1.
- [ ] Strings de UI localizadas em PT-BR e ES com **fallback EN**.

## 5. Gates externos (recomendados; rodados fora desta máquina)

> Runbook operator-facing (pré-requisitos, comandos, markers, modos de falha,
> formato de report): [`../operations/etapa-6-external-validation-playbook.md`](../operations/etapa-6-external-validation-playbook.md).

- `make smoke-x64-vmware-apps-basic-roundtrip` (novo): abre cada app,
  executa função primária, fecha sem leak/crash.
- `make smoke-x64-vmware-capybrowse-text` (novo): abre página HTTP/HTTPS
  controlada, valida texto, links numerados, scroll e erro TLS fail-closed.
- `make test` / `make layout-audit` / `make all64 iso-uefi` por slice.

> Coordenação cross-repo: promover a Etapa 6 e integrar `CapyBrowser`/
> `CapyUI` por contrato é um gatilho de versionamento
> (`.windsurf/rules/20-cross-repo-versioning.md`). O bump `alpha.265`
> registra o primeiro handoff sister da Etapa 6 (`CapyBrowser v0.6.0`);
> integração de runtime ainda exige adapter e gate externo.
