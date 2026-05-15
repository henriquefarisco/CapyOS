# CapyOS 0.8.0-alpha.15+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch incremental de **Update/F5**: o CapyCLI ganhou
`update-apply <payload_sha256>`, um caminho operacional para aplicar staged
updates somente quando o SHA-256 real do payload combina com o `payload_sha256`
do manifesto assinado já validado.

A versão alinhada é `0.8.0-alpha.15+20260509`.

## Principais entregas

### Comando `update-apply`

- Novo comando shell `update-apply <payload_sha256>`.
- O comando chama `update_agent_apply_boot_slot_verified()`.
- Digest ausente, vazio, malformado ou divergente é recusado antes de armar boot
  slot.
- Digest compatível arma o próximo boot slot para o staged update.
- O evento `apply` entra em `/var/log/update-history.log`.

### Status estável para automação operacional

`update_agent_apply_boot_slot_verified()` agora publica status estável em sucesso:

- `last_result=0`;
- `summary="verified staged update applied to boot slot"`.

Isso permite que shell, histórico e serviço `update-agent` exponham a conclusão
do apply verificado de forma previsível.

### Testes planejados

`tests/test_update_transact.c` reforça por revisão estática que o apply verificado
com digest correto expõe `last_result=0` e a summary de sucesso esperada, além dos
casos já existentes de digest ausente, malformado, divergente e comparação
case-insensitive.

## Compatibilidade

- `update-fetch` continua responsável apenas pelo manifesto remoto.
- `update-stage` e `update-arm on` continuam preparando o staged update.
- `update-apply <payload_sha256>` exige que o operador ou ferramenta externa já
  tenha calculado o SHA-256 real do payload.
- Download automático de payload remoto e confirmação operacional de saúde após
  reboot seguem para incrementos posteriores.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- protótipo `cmd_update_apply` no header interno de system-control;
- implementação do comando em `jobs_updates.c`;
- registro no array `g_system_control_commands[23]`;
- uso de `update_agent_apply_boot_slot_verified()` como gate único;
- status de sucesso no update-agent transacional;
- documentação CLI/operacional/plano/release alinhada.

## Próximos passos

- Baixar payload remoto declarado no manifesto.
- Adicionar fluxo operacional de health confirm pós-reboot.
- Adicionar smoke `smoke-x64-update-fetch`/`update-apply` com servidor HTTP local.
- Integrar TLS userland completo quando F4 entregar `libcapy-tls`.
