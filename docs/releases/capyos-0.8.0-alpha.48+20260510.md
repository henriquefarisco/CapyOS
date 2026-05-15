# CapyOS 0.8.0-alpha.48+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** para a trilha oficial VMware+E1000: o projeto
passa a versionar um harness dedicado para smoke DHCP externo, exposto pelo alvo
`smoke-x64-vmware-dhcp`.

A versão alinhada é `0.8.0-alpha.48+20260510`.

## Principais entregas

### Harness VMware+E1000 DHCP

- Novo `tools/scripts/smoke_x64_vmware.py` com providers `vmrun` e `govc`.
- Validação de artefatos de build antes de ligar a VM.
- Observação de serial/debug console com timeout e markers configuráveis.
- `--dry-run` para validar wiring de argumentos sem tocar infraestrutura externa.
- Novo alvo `smoke-x64-vmware-dhcp` no `Makefile`.
- Novo guia `docs/testing/vmware-e1000-smoke.md` com setup, execução e troubleshooting.

## Compatibilidade

- Nenhuma API de runtime foi alterada.
- Smokes QEMU existentes não foram modificados.
- A execução real continua dependendo de infraestrutura VMware externa, VM template
  UEFI+E1000 e serial file configurado pelo operador/CI.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- parsing de argumentos e validações de ambiente do harness;
- providers `vmrun` e `govc` sem efeitos destrutivos fora da VM alvo;
- target `smoke-x64-vmware-dhcp` no `Makefile`;
- guia operacional em `docs/testing/vmware-e1000-smoke.md`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Provisionar VM/serial/credenciais VMware na CI.
- Publicar a chave Ed25519 oficial esperada para verificação de release.
- Ligar `verify-release-signature` e `smoke-x64-vmware-dhcp` na esteira de tag.
