# libcapyhtml — HTML parser portável (userland-buildable)

**Status:** ✅ Slices 1..6 entregues (F3.3c 100%) — branch `feature/m5-w4` consolidado em `feature/dev-bugfixes`.
**Plano-mãe (arquivado):** [`docs/plans/historical/f3-3c-html-viewer-userland-slicing.md`](../../../docs/plans/historical/f3-3c-html-viewer-userland-slicing.md).
**Entrega consolidada:** [`docs/plans/historical/f3-browser-delivered.md`](../../../docs/plans/historical/f3-browser-delivered.md).

Esta biblioteca é o destino userland do parser HTML/CSS hoje
acoplado em `src/apps/html_viewer/`. Quando completa, será linkada
estaticamente no binário `userland/bin/capybrowser/` (ring 3) e
permitirá que o engine renderize páginas reais isoladamente do
kernel.

## Slice 1 (entregue neste branch)

- Esqueleto de diretórios (`include/capyhtml/`, `src/`).
- `parser.h` com forward declaration de `capyhtml_parse`,
  `struct capyhtml_document`, `struct capyhtml_node`,
  `capyhtml_yield_fn`.
- `parser.c` com stub mínimo (retorna -1) que serve de placa de
  amarração para a regra de build.
- Target `make capyhtml-userland-syntax` no Makefile raiz que
  invoca `clang -fsyntax-only -target x86_64-unknown-linux-gnu`
  sobre `parser.c`. CI passa a executar o target a partir do
  próximo PR (Slice 2 abre o ELF de fato).

## Inventário de dependências bloqueantes para Slice 2

`html_parser.c` (734 linhas) chama 30 símbolos externos. Cada um
precisa ser portado, stubado ou injetado por callback antes de o
parser real morar nesta lib.

### Helpers puros (string/buffer) — port direto via `<string.h>`

| Símbolo (kernel) | Origem hoje | Plano Slice 2 |
|---|---|---|
| `kmemzero` | `memory/kmem.h` | `static inline` via `memset` |
| `kstrcpy` | `util/kstring.h` | `static inline` via `strncpy` + NUL |
| `kstrlen` | `util/kstring.h` | `strlen` |
| `kstreq` | `util/kstring.h` | `strcmp == 0` |
| `kbuf_append`, `kbuf_append_u32` | `util/kbuf.h` | inlines locais |

### Helpers privados do html_viewer — copiar/mover para esta lib

Todos vivem em `src/apps/html_viewer/text_url_helpers.c` ou
`html_tree_helpers.c` e são puros (não tocam kernel). Slice 2
move o corpo destes para `userland/lib/capyhtml/src/text_helpers.c`
e `tree_helpers.c`:

- `hv_is_space`, `hv_streq_ci`, `hv_contains_ci`
- `hv_skip_special_tag`, `hv_skip_block`
- `hv_read_tag_name`, `hv_scan_tag_end`, `hv_tag_is_void`
- `hv_extract_attr_value`, `hv_has_boolean_attr`
- `hv_apply_node_attrs`
- `hv_collect_text_until_tag`, `hv_parse_inline_content`
- `hv_extract_srcset_first_url`
- `hv_decode_entity_value`, `hv_text_append_char`, `hv_trim_text`
- `hv_token_list_contains_ci`
- `hv_form_input_type`, `hv_parse_form_method`
- `hv_parse_meta_refresh_content`
- `hv_doc_queue_pending_css`
- `hv_image_type_supported_by_decoder` *(precisa do enum de tipos
  suportados; portável)*
- `html_push_node`

### Acoplamento com o kernel — substituir por callback / app-state

| Símbolo | Origem | Estratégia Slice 2 |
|---|---|---|
| `task_yield` | `kernel/task.h` | callback `capyhtml_yield_fn` injetada |
| `hv_parse_app_get` | global mutável em `common.c` | parâmetro explícito `capyhtml_parse_ctx *` |
| `hv_nav_budget_blocked` | `navigation_budget.c` | callback `capyhtml_should_cancel_fn` |

### Submódulo CSS

`css_parse` + `css_apply_to_doc` vivem em `src/apps/html_viewer/css_parser.c`
e dependem só de `kstring`/`kmem`. Slice 2 copia integralmente para
`userland/lib/capyhtml/src/css.c`.

## Critérios de saída do Slice 1

- [x] `parser.h` declara surface mínima com `extern "C"` guard.
- [x] `parser.c` compila standalone (não chama nada além do stub).
- [x] `make capyhtml-userland-syntax` executa `clang -fsyntax-only`
      cross-target sobre `parser.c` e devolve exit 0.
- [x] `make layout-audit` continua sem warnings novos.
- [x] Inventário de dependências desta seção é completo o suficiente
      para Slice 2 começar sem investigação adicional.
