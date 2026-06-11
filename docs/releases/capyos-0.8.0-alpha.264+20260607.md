# CapyOS 0.8.0-alpha.264+20260607

**Data:** 2026-06-07
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.264+20260607`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Fecho da Etapa 5 (TLS userland real, flag promovida a default) + abertura da Etapa 6 (apps basicos + CapyBrowse Text) + hardening de seguranca

## Resumo executivo

A Etapa 5 (TLS userland real) **fechou** apos o gate externo passar (build
flag-on + `make smoke-x64-vmware-tls-handshake`, marker `[smoke] tls-handshake
ready` no COM1, + `release-check`): a flag `CAPYOS_TLS_USERLAND_HANDSHAKE` foi
**promovida a default** no build ring-3 (`USERLAND_CFLAGS`; opt-out com `=0`),
de modo que a `libcapy-tls` userland passa a fazer handshake BearSSL real por
padrao (`capy_tls_is_supported()==1`). A **Etapa 6** (apps basicos do desktop +
CapyBrowse Text) abriu, com a fundacao CapyOS-side de diagnostico de rede e
localizacao entregue. Esta janela tambem consolidou hardening de seguranca em
parsers de input nao-confiavel.

## Mudancas entregues

### Etapa 5 — fechamento
- Flag `CAPYOS_TLS_USERLAND_HANDSHAKE` **promovida a default** (Makefile
  `USERLAND_CFLAGS`; opt-out `=0`; `HOST_CFLAGS` / host tests inalterados).
- Criterios de aceite (master plan §8) marcados; readiness doc fechado.

### Etapa 6 — fundacao CapyOS-side (slices nao-bloqueados)
- **6.2** `capy_net_strerror(capy_net_err_t)` — strings EN estaveis para os 11
  codigos de erro de rede (base do diagnostico amigavel do CapyBrowse Text).
- **6.3** `capy_net_diagnose_stage(net_err, tls_state)` + `capy_net_stage_name`
  — classifica a falha em estagio DNS/TCP/TLS/HTTP combinando o erro de rede
  com o estado TLS (corrige a conflacao de falha TLS com "feature not
  supported").
- **6.5** `localization_select` — EN como fallback base universal para string
  ausente (a selecao default segue PT-BR, invariante travado por teste).

### Hardening de seguranca (parsers de input nao-confiavel)
- ELF loader **userland** (`elf_loader.c`): 3 overflows de integer corrigidos
  via `include/kernel/elf_bounds.h` + teste host `test_elf_bounds.c`.
- ELF loader **de boot** (`kernel_loader.c`): bounds de phdr-table + overflow
  de `p_offset` em `load_kernel_from_buffer` + guarda de wrap `p_paddr+p_memsz`
  nos dois loaders.
- **Volume header**: tetos de custo KDF (Argon2 t<=4096 / m<=1 GiB, PBKDF2
  <=100M iters) contra DoS de mount pre-auth + testes.
- Cobertura adversarial host nova para os parsers de RX DNS, DHCP options,
  ICMP e ARP.
- CAPYFS `names_equal` limitado ao campo fixo de 32 bytes.

### Verificacao (sem mudanca de codigo)
- Stack de input de rede do browser auditada e **segura**: `capy_net_http.c`
  (resposta HTTP — bounded, anti request-smuggling, exaustivamente testada) e
  `capy_net_url.c` (URL — bounded, anti-phishing/strict-by-design).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.263+20260606` | `alpha.264+20260607` | Fecho Etapa 5 + abertura Etapa 6 + hardening de seguranca |

Bump **no-op para os contratos sister**: nenhum ABI / manifesto / quota /
politica de assinatura mudou. Pins inalterados — CapyUI `2.22.0`, CapyCodecs
`0.0.7`, CapyLang `0.1.8`, CapyAgent `0.0.7`, CapyBrowser `0.5.0`, CapyBenchmark
`0.0.7`. Addendum cross-repo registrado em `compatibility-audit-2026-06-06.md`.

## Evidencias / validacao

Gate externo da Etapa 5 (executado e aprovado, base do fecho):
- `make all64 PROFILE=full CAPYOS_TLS_USERLAND_HANDSHAKE=1 CAPYOS_TLS_HANDSHAKE_SMOKE=1`
- `make smoke-x64-vmware-tls-handshake` (marker `[smoke] tls-handshake ready` no COM1)
- `make release-check`

Validacao local esperada antes da tag (cobre as mudancas in-tree desta janela):
- `make test` — host tests novos (`strerror`, `diagnose_stage`, localization
  fallback EN, ELF bounds, tetos KDF do volume header, DNS/DHCP/ICMP/ARP) +
  suites `capy_net` / `capylibc`.
- `make version-audit` — `VERSION.yaml` mudou (current/extended + history).
- `make layout-audit` — codigo + docs novos.
- `make all64 iso-uefi` — confirma que o **novo default** (flag-on) compila e
  boota (= o build flag-on ja validado no gate da Etapa 5).

## Proximos passos

1. **Etapa 6 / Slice 6.4** (consumir o core HTML-to-text): **bloqueado** ate o
   `CapyBrowser` publicar a ABI `capy-browser-core` (hoje so o codec BMP foi
   migrado; HTML-to-text/links/word-wrap sao do core externo).
2. **Etapa 6 / 6.3-localizado + 6.6** (apresentacao localizada do diagnostico +
   maturidade dos apps): dependem do app CapyBrowse Text / framework de apps
   CapyUI; dificeis de validar host-side (review/edit-only).
