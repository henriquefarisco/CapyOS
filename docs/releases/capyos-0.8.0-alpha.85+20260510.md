# CapyOS 0.8.0-alpha.85+20260510

## Resumo executivo

Este patch avanca F4 em `libcapy-tls` adicionando um resumo agregado metadata-only dos tamanhos do material dos trust anchors padrao. A camada continua sem copiar bytes de certificados para userland, sem parse X.509, sem BearSSL userland e sem handshake real.

## Principais entregas

- Adiciona `struct capy_tls_trust_material_summary` interna.
- Registra `subject_dn_total_bytes=14385` e `key_material_total_bytes=47329` derivados estaticamente do bundle kernel-side.
- Registra maximos agregados `subject_dn_max_bytes=213` e `key_material_max_bytes=515`.
- Adiciona flags `metadata-only`, `kernel-bundle-derived`, `cert-bytes-absent` e `aggregate-only`.
- Adiciona fingerprint `0x8DFC9FAF` para o resumo agregado.
- Eleva o manifesto para schema `2` e fingerprint `0xC042BF82`, incorporando o resumo agregado.
- Propaga os campos agregados para `capy_tls_backend_state` apenas quando a configuracao usa o bundle padrao.
- Mantem CA custom opaca: os novos campos agregados permanecem zerados quando `ca_cert` e fornecido.

## Seguranca e compatibilidade

- Nao ha copia, exposicao ou parse de bytes de certificados no modulo userland.
- O resumo e agregado e metadata-only, reduzindo superficie de dados enquanto prepara invariantes para o futuro backend BearSSL.
- A ABI publica continua inalterada; `capy_tls_context` permanece opaco.
- Entradas estruturalmente validas continuam retornando `CAPY_TLS_EUNSUPPORTED` ate existir backend TLS real.
- Rejeicoes e ciclos de vida de contexto continuam limpando todo o estado backend.

## Validacao estatica

- Revisao estatica confirmou que a nova camada e metadata-only.
- Nenhum objeto BearSSL userland, parser X.509 ou handshake real foi introduzido.
- Testes declarativos foram atualizados para cobrir consistencia, propagacao e scrub dos novos metadados.
- Nao foram executados `make`, `git`, build ou suite de testes.
