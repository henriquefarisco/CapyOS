# CapyOS — log de auditoria de segurança

Registro datado das rodadas de auditoria. Mais recente primeiro. Inventário +
status corrente: [`attack-surface.md`](attack-surface.md). Critérios:
[`audit-playbook.md`](audit-playbook.md).

## 2026-06-17 — rodada P0 + parsers de rede + ELF loader

**Escopo:** assinatura de pacote capypkg (verifier Ed25519 + gate de install +
parser de manifesto), crypto de volume (`volume_header` / `volume_provider`),
parsers de rede (DNS, DHCP) e o ELF loader.

### Achado + fix — ELF loader sem validação de intervalo de usuário do `p_vaddr` (alpha.282)

`elf_load` (`src/kernel/elf_loader.c`) validava limites de offset-no-arquivo e o
no-wrap de `p_vaddr + p_memsz`, mas **não** que o span virtual de um `PT_LOAD`
cai na metade de usuário. Como o arredondamento `vaddr_end = (p_vaddr + p_memsz +
VMM_PAGE_SIZE-1) & ~(...)` soma `+VMM_PAGE_SIZE-1` **depois** do no-wrap, um ELF
forjado podia: **(a)** com `p_vaddr+p_memsz` a <4 KiB de `UINT64_MAX`, dar wrap em
`vaddr_end` → `num_pages` gigantesco → loop de mapeamento exaurindo a PMM (DoS);
**(b)** com `p_vaddr` na metade de kernel, instalar PTEs `USER` sobre as tabelas
de página de kernel (`vmm_map_page` não valida o vaddr).

**Fix:** novo helper subtraction-only `elf_vaddr_in_user_range(vaddr, memsz,
user_top)` em `include/kernel/elf_bounds.h`, chamado em `elf_load` antes do
arredondamento com `user_top = VMM_USER_TOP`. Host test novo em
`tests/kernel/test_elf_bounds.c`. Entregue em `alpha.282+20260617`.

### Áreas auditadas sem achados (limpas)

- **Assinatura capypkg** — fail-closed em todo caminho (sem chave / sem verifier
  / assinatura ausente / hex malformado / falha de build do descritor / falha de
  verify → `CAPYPKG_ERR_SIGNATURE`); decodificador hex sem over-read; binding do
  descritor à prova de colisão (`payload_sha256`=64hex fixo, **mesmo campo** no
  descritor e na verificação do install; `name` com alfabeto restrito); `name`
  com proteção a path-traversal; `payload_url` https-only; `signature_hex`
  pré-validado 128 hex; trust anchor default não-pinado (fail-closed).
- **Crypto de volume** — comparação de tag constant-time (`vh_const_eq`); wipe
  volatile de chaves/buffers em todos os caminhos; proteção a downgrade (header
  autoritativo após `looks_valid`, sem fallback legado em falha de
  parse/derive/tag/IO); sem oráculo de tampering (wrong-pw e tampered → ambos
  `ERR_CHECK_TAG`); pisos de custo de KDF (PBKDF2 ≥1000, Argon2id RFC 9106).
- **Parser DNS** — `skip_name` trata pointer de compressão como terminal (não
  segue → sem loop de descompressão); todo `read_be*` bounds-checked
  (`+4`/`+10`/`+rdlen` vs `len`); SOA rdata confinado a `rdata_start + rdlen`;
  offsets pequenos (sem overflow); fail-closed.
- **Parser DHCP** — guarda TLV `opt_len > (len - idx)` (com `idx ≤ len`, sem
  underflow); reads de 4 bytes exigem `opt_len >= 4` in-bounds; `idx` cresce
  estritamente (termina); fail-closed em opção malformada.

### Pendente para próximas rodadas
CAPYFS (parse/fsck), ICMP/ARP, HTTP parse. TLS/X.509: o core é BearSSL
vendido/auditado upstream; auditar apenas o seam `capy_tls`.
