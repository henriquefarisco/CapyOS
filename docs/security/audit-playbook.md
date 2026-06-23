# CapyOS — playbook de auditoria de segurança

Como auditar uma área de input não-confiável / manipulação de segredo com
rigor repetível. Inventário das áreas + status:
[`attack-surface.md`](attack-surface.md). Registro dos achados:
[`audit-log.md`](audit-log.md).

## Quando auditar

- **Sempre** ao tocar uma área da superfície de ataque (rede, disco, crypto,
  loader, parsers de input externo).
- **Rodadas periódicas** das áreas marcadas "Pendente" no inventário.
- Antes de promover uma Etapa que exponha nova superfície (ex.: install
  assinado user-facing, browser gráfico).

## Como rodar a regressão

```sh
make security-selftest   # parsers de input + crypto + assinatura de pacote
```

Reusa o binário de teste full-link; roda só o subconjunto de segurança
(o argumento `security` em `tests/test_runner.c`). Cobre: `csprng`,
`crypt_vector`, `volume_header`, `volume_provider` (+ rekey), `user_password_hash`,
`capypkg` (incl. assinatura), `net_dns`, `net_dhcp_options`, `net_icmp`,
`net_arp`, `elf_bounds`.

## Checklist por área (o que verificar)

Para **todo parser de input não-confiável**:

- [ ] **Bounds em todo read** — cada acesso indexado é guardado por um limite
      derivado do tamanho real recebido (não de um campo do próprio input).
- [ ] **Sem over-read** — nunca lê além do buffer; checa o terminador/NUL antes
      de avançar; índices pós-loop comprovadamente in-bounds.
- [ ] **Sem overflow de inteiro** — comparações de limite usam subtração
      (`a > size - b`), nunca `a + b <= size` que pode dar wrap. Spans de 64-bit
      controlados pelo atacante (offset+len, vaddr+memsz) checados sem wrap **e**
      antes de qualquer arredondamento posterior.
- [ ] **Terminação** — loops sobre o input avançam estritamente; ponteiros de
      compressão/redireção não são seguidos em laço (ex.: DNS `skip_name`).
- [ ] **Fail-closed** — qualquer entrada malformada → erro explícito + saída
      segura (zerada); nunca "best-effort" silencioso.

Para **crypto / segredos**:

- [ ] **Constant-time** na comparação de tag/hash/assinatura derivada de segredo.
- [ ] **Wipe volatile** de chaves/buffers em **todos** os caminhos (sucesso e
      erro); outputs zerados primeiro, e de novo em falha.
- [ ] **Sem oráculo** — senha errada e dado adulterado retornam o mesmo erro.
- [ ] **Proteção a downgrade** — caminho forte é autoritativo; sem fallback ao
      caminho fraco em falha de auth.
- [ ] **Pisos de custo de KDF**; trust anchor default fail-closed.

Para o **ELF loader / mapeamento**:

- [ ] Limites de offset-no-arquivo (`elf_range_in_bounds`/`elf_phdr_entry_fits`).
- [ ] `p_vaddr`+`p_memsz` sem wrap **e** dentro da metade de usuário
      (`elf_vaddr_in_user_range`, `VMM_USER_TOP`) — antes do arredondamento de
      página (ver o fix de alpha.282).

## Ao terminar

1. Atualize a linha da área em [`attack-surface.md`](attack-surface.md) (data +
   status).
2. Registre em [`audit-log.md`](audit-log.md): áreas vistas, achados, fixes (com
   o alpha), e o que ficou limpo.
3. Se houver fix, adicione um host test de regressão (padrão `elf_bounds`).
