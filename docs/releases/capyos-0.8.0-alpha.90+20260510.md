# CapyOS 0.8.0-alpha.90+20260510

## Resumo executivo

Este patch avanca F4 em `libcapy-tls` materializando uma representacao userland metadata-only do bundle padrao de trust anchors. A nova tabela registra uma entrada por anchor com indice, tipo RSA/EC, tamanho do DN de sujeito, tamanho do material de chave e flags/fingerprint derivados do bundle kernel-side, mas ainda nao copia certificados, nao faz parse X.509 e nao conecta BearSSL userland ao handshake.

## Principais entregas

- Adiciona `userland/lib/capylibc-tls/capy_tls_trust_bundle.c`.
- Adiciona `struct capy_tls_trust_anchor_bundle_entry` e `struct capy_tls_trust_anchor_bundle` ao contrato interno.
- Registra fingerprint do bundle metadata-only `0x0ED4E969` e manifesto schema `3` com fingerprint `0xBD2653A4`.
- Propaga `trust_anchor_bundle_entry_count` e `trust_anchor_bundle_fingerprint` no backend stub quando a configuracao usa o bundle padrao.
- Integra o novo objeto em `CAPYLIBC_TLS_OBJS` e na suite host declarativa.

## Seguranca e compatibilidade

- O contrato publico de `libcapy-tls` permanece inalterado e fail-closed.
- Nenhum certificado e copiado, exposto ou parseado em userland.
- Nenhum objeto BearSSL userland e construido e nenhum handshake real e iniciado.
- A tabela permite validar limites por anchor antes de habilitar material criptografico real.

## Validacao estatica

- Revisao estatica confirmou que o bundle userland contem apenas metadados de tamanho e tipo, nao bytes de certificados ou chaves.
- Revisao estatica confirmou que o backend continua retornando `CAPY_TLS_EUNSUPPORTED` para entradas estruturalmente validas.
- Revisao estatica confirmou que scripts temporarios `tmp_alpha90*.py` foram removidos.
- Nao foram executados `make`, `git`, build, suite de testes ou smoke VMware real.
