# CapyOS 0.8.0-alpha.282+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.282+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Hardening de seguranca (auditoria) -- validacao de intervalo de usuario do p_vaddr no ELF loader

## Resumo executivo

Encontrado numa **auditoria de seguranca** das areas de maior risco do tree, este
release fecha uma lacuna de validacao no carregador de ELF: `elf_load` validava
os limites de offset-no-arquivo e o no-wrap de `p_vaddr + p_memsz`, mas nao que o
**span virtual** de um segmento `PT_LOAD` cai na metade de usuario. Os campos de
ELF sao input nao-confiavel (o proprio `elf_bounds.h` documenta isso).

## A lacuna

`src/kernel/elf_loader.c::elf_load`, para cada `PT_LOAD`, computa:

```
vaddr_end = (p_vaddr + p_memsz + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);
num_pages = (vaddr_end - vaddr_start) / VMM_PAGE_SIZE;
```

Havia o guarda `elf_sum_no_wrap(p_vaddr, p_memsz)`, mas o arredondamento soma
`+ VMM_PAGE_SIZE - 1` **depois** dele. Consequencias com um ELF forjado:

1. **DoS de memoria:** `p_vaddr + p_memsz` a < 4 KiB de `UINT64_MAX` faz
   `vaddr_end` dar wrap -> `vaddr_end - vaddr_start` estoura -> `num_pages`
   gigantesco -> o loop de mapeamento aloca paginas ate exaurir a PMM (depois
   falha com -1, mas ja consumiu toda a RAM livre).
2. **Mapeamento na metade de kernel:** um `p_vaddr` canonical alto (metade de
   kernel) levaria `vmm_map_page` a instalar PTEs com flag **USER** indexando as
   tabelas de pagina da metade de kernel -- `vmm_map_page` **nao** valida o
   endereco virtual (so faz `(virt >> 39) & 0x1FF`).

Exposicao: os ELFs de boot embutidos sao confiaveis, mas `elf_load_from_file`
carrega ELFs do FS; o loader e por design um limite de seguranca para input
nao-confiavel.

## O fix

Novo predicado subtraction-only (sem wrap) em `include/kernel/elf_bounds.h`:

```c
static inline int elf_vaddr_in_user_range(uint64_t vaddr, uint64_t memsz,
                                          uint64_t user_top) {
  if (vaddr > user_top) return 0;
  if (memsz > user_top - vaddr) return 0;
  return 1;
}
```

Chamado em `elf_load` (antes do arredondamento) com `user_top = VMM_USER_TOP`
(`0x00007FFFFFFFF000`, bem abaixo de `UINT64_MAX`, entao o arredondamento nao
pode mais dar wrap). Rejeita base na metade de kernel, fim alem do topo, wrap de
`vaddr` e span que estoura o topo. Binarios de usuario legitimos (segmentos em
`[VMM_USER_BASE, VMM_USER_TOP)`) passam inalterados.

## Resto da auditoria (sem achados)

As demais areas P0 revisadas **nao** apresentaram vulnerabilidades:

- **Assinatura de pacote capypkg** (verifier Ed25519 + gate de install +
  validacao de manifesto): fail-closed em todo caminho; hex sem over-read;
  binding do descritor a prova de colisao (sha256=64hex fixo, mesmo campo no
  descritor e na verificacao do install; `name` com alfabeto restrito);
  trust anchor default nao-pinado.
- **Crypto de volume** (`volume_header.c` / `volume_provider.c`): comparacao de
  tag constant-time; wipe volatile-safe de chaves/buffers em todos os caminhos;
  protecao a downgrade (header autoritativo, sem fallback legado em falha);
  sem oraculo de header adulterado; pisos de custo de KDF.
- **Parsers de rede** (`dns.c` / `dhcp_options.c`): `skip_name` nao segue
  pointers de compressao (sem loop de descompressao); todo read e
  bounds-checked; parser TLV de DHCP confina por `opt_len > (len - idx)`; sem
  overflow, terminam, fail-closed.

## Validacao

- `make test` -- verde, incl. os novos casos de `elf_vaddr_in_user_range` em
  `tests/kernel/test_elf_bounds.c` (aceita span valido/vazio/exact-fit; rejeita
  metade de kernel / overrun / wrap).
- `make all64` -- compila (elf_loader.c com o novo guarda).
- `make layout-audit` / `make version-audit` -- sem warnings.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.281+20260617` | `alpha.282+20260617` | hardening do ELF loader (p_vaddr user-range) |

Sem mudanca de ABI. Os 6 repos irmaos permanecem inalterados (CapyUI segue 2.22.6).

_Build: `0.8.0-alpha.282+20260617`_
