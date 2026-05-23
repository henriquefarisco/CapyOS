# Cross-repo compatibility audit — 2026-05-22

**Status:** snapshot técnico da correção da Fase A da Etapa 4 após o rollback de `alpha.255`.
**Snapshot anterior:** [`compatibility-audit-2026-05-21.md`](compatibility-audit-2026-05-21.md).
**Matriz autoritativa:** [`compatibility-matrix.md`](compatibility-matrix.md).

**Escopo:** sincronizar o estado real do sister `../CapyUI` com o core CapyOS e registrar o adapter CapyOS-side inicial para display-list. Validação estática realizada nesta máquina; nenhum comando, build, teste, script ou fetch foi executado.

**Resposta executiva:** a Fase A correta da Etapa 4 não cria nova ABI. O CapyOS passa a detectar `../CapyUI/src/widget/capy_display_list.h`, consumir o layout nativo `capy_display_list` / `capy_dl_cmd` quando o sibling está presente e expor um adapter pequeno em `include/gui/capyui_display_adapter.h` + `src/gui/widgets/capyui_display_adapter.c`. A pinagem de `CapyUI` foi atualizada para `2.13.0`, com `capy-ui-widget` v2.13 e display-list schema v7.

---

## 1. Estado real do CapyUI mapeado

Fonte inspecionada no sibling local `../CapyUI`:

| Fonte | Valor encontrado |
|---|---|
| `VERSION` | `2.13.0` |
| `docs/compatibility.md` | `capy-ui-widget` v2.13 + `capy-ui-desktop-session` v1 |
| `src/widget/capy_display_list.h` | `CAPY_DISPLAY_LIST_SCHEMA_VERSION` = `7u` |

O header autoritativo define:

- `struct capy_display_list` com `cmds`, `count`, `capacity`, `text_pool`, `text_used`, `text_capacity`, `version`, `theme`, `dpi_scale_x256` e `reserved_dpi`;
- `struct capy_dl_cmd` com `op`, `rect`, `color`, `text_offset`, `text_len`, `border_width`, `font_size`, `font_id`, `image_id` e `transform`;
- opcodes `CAPY_DL_RECT`, `CAPY_DL_BORDER`, `CAPY_DL_TEXT`, `CAPY_DL_CLIP_PUSH`, `CAPY_DL_CLIP_POP`, `CAPY_DL_IMAGE_REF`, `CAPY_DL_FOCUS_RING`, `CAPY_DL_DIRTY_HINT`, `CAPY_DL_DPI_SCOPE`, `CAPY_DL_TRANSFORM_PUSH`, `CAPY_DL_TRANSFORM_POP` e `CAPY_DL_PLUGIN_OP`.

---

## 2. Mudança CapyOS-side nesta auditoria

Arquivos novos:

- `include/gui/capyui_display_adapter.h` — header público CapyOS com ponteiro opaco para `struct capy_display_list`, status codes, stats e entry points de render/diff.
- `src/gui/widgets/capyui_display_adapter.c` — adapter runtime que renderiza comandos 2D básicos para `struct gui_surface`, aplica clip stack e damage clip e falha fechado quando o header do sister não está presente.
- `tests/gui/test_capyui_display_adapter.c` — cobertura host-side do adapter, incluindo fallback sem sibling, schema v7, rect/border, clip stack, damage clip, validação de text span e mapeamento de dirty rects.

Wiring atualizado:

- `Makefile` detecta `$(CAPYUI_DIR)/src/widget/capy_display_list.h`, define `CAPYOS_HAVE_CAPYUI_WIDGET` e adiciona `-I$(CAPYUI_WIDGET_DIR)` somente quando o sibling existe.
- `Makefile` inclui `src/gui/widgets/capyui_display_adapter.c` no kernel x86_64 e no host test bundle.
- `tests/test_runner.c` chama `test_capyui_display_adapter_run()`.

---

## 3. Modelo de compatibilidade do adapter

Com `CAPYOS_HAVE_CAPYUI_WIDGET` ativo:

- `CAPY_DL_RECT`, `CAPY_DL_BORDER`, `CAPY_DL_TEXT`, `CAPY_DL_CLIP_PUSH`, `CAPY_DL_CLIP_POP` e `CAPY_DL_FOCUS_RING` têm comportamento renderizável inicial.
- `CAPY_DL_DIRTY_HINT` é tratado como hint e não gera pixels.
- `CAPY_DL_IMAGE_REF`, `CAPY_DL_DPI_SCOPE`, `CAPY_DL_TRANSFORM_PUSH`, `CAPY_DL_TRANSFORM_POP` e `CAPY_DL_PLUGIN_OP` são ignorados de forma segura até existirem providers CapyOS dedicados para imagem, DPI/transform e plugins.
- `dl->version > CAPY_DISPLAY_LIST_SCHEMA_VERSION` retorna erro de schema não suportado.
- Text spans são checados contra `text_capacity` antes de rasterizar.
- Clip stack desbalanceado ou em overflow retorna erro controlado.

Sem `CAPYOS_HAVE_CAPYUI_WIDGET`, o adapter retorna `CAPYUI_DISPLAY_ADAPTER_ERR_UNAVAILABLE` e zera stats.

---

## 4. Matriz atualizada

`docs/reference/integration/compatibility-matrix.md` agora pina:

| Repo | Versão | ABI |
|---|---|---|
| `CapyUI` | `2.13.0` | `capy-ui-widget` v2.13 (display-list schema v7) + `capy-ui-desktop-session` v1 |

A Etapa 4 deixa de falar em promoção para um contrato intermediário como próximo passo e passa a declarar consumo do contrato real v2.13/schema v7.

---

## 5. Pendências e gates externos

Pendências técnicas restantes da Etapa 4:

- ligar o adapter a produtores reais de display-list no fluxo desktop/window quando a fase de integração visual abrir;
- implementar providers para imagem, DPI/transform e plugins antes de declarar suporte completo a todos os opcodes v7;
- implementar os gates runtime planejados: scheduler fairness, compositor damage tracking e thread-crash survival.

Validação externa/manual recomendada para esta fatia:

1. `make layout-audit`
2. `make test`
3. `make all64`
4. `make smoke-x64-vmware-compositor-damage-track` quando o smoke existir no Makefile

Esta máquina permanece review/edit only; os gates acima devem ser executados por CI ou operador externo.
