# CapyOS — Metadados de trust anchors em libcapy-tls

## Objetivo

`libcapy-tls` precisa distinguir, antes do BearSSL userland real, se uma conexão usará o bundle padrão de trust anchors ou uma CA custom fornecida via configuração pública. `0.8.0-alpha.78+20260510` adiciona metadados internos para essa decisão sem carregar certificados. `0.8.0-alpha.79+20260510` conecta esses metadados a uma fonte userland metadata-only do bundle padrão. `0.8.0-alpha.80+20260510` adiciona catálogo com distribuição RSA/EC. `0.8.0-alpha.81+20260510` fixa máscara e fingerprint metadata-only do catálogo. `0.8.0-alpha.82+20260510` adiciona tabela metadata-only de slots por anchor. `0.8.0-alpha.83+20260510` adiciona descritores metadata-only por anchor. `0.8.0-alpha.84+20260510` adiciona manifesto metadata-only do trust store. `0.8.0-alpha.85+20260510` adiciona resumo agregado metadata-only dos tamanhos do material. `0.8.0-alpha.90+20260510` adiciona tabela userland metadata-only por anchor derivada do bundle kernel-side.

## Contrato em `0.8.0-alpha.78+20260510`

- `capy_tls_backend_state` registra `trust_anchors_ready`.
- Em `0.8.0-alpha.78+20260510`, configuração padrão mantém `custom_anchor_ready=0`, `trust_anchor_count=0` e `custom_anchor_len=0`.
- Em `0.8.0-alpha.79+20260510`, configuração padrão usa a fonte userland interna e registra `trust_anchor_count=146`.
- Em `0.8.0-alpha.80+20260510`, configuração padrão também registra `trust_anchor_rsa_count=106` e `trust_anchor_ec_count=40`.
- Em `0.8.0-alpha.81+20260510`, configuração padrão registra máscara `0x3` e fingerprint `0xDB22D94A`.
- Em `0.8.0-alpha.82+20260510`, configuração padrão registra `trust_anchor_slot_count=146` e fingerprint de layout `0x07A622AB`.
- Em `0.8.0-alpha.83+20260510`, configuração padrão registra `trust_anchor_descriptor_count=146` e fingerprint de descritores `0xE1A18A70`.
- Em `0.8.0-alpha.84+20260510`, configuração padrão registra schema/source/flags do manifesto e fingerprint `0x958359C5`.
- Em `0.8.0-alpha.85+20260510`, configuração padrão registra resumo agregado de material (`14385` bytes de DN, `47329` bytes de chave, maximos `213`/`515`) e fingerprint `0x8DFC9FAF`.
- Em `0.8.0-alpha.90+20260510`, configuração padrão registra `trust_anchor_bundle_entry_count=146`, fingerprint do bundle `0x0ED4E969`; o manifesto passa para schema `3` e fingerprint `0xBD2653A4`.
- Configuração com `ca_cert` válido marca `custom_anchor_ready=1`, `trust_anchor_count=1` e `custom_anchor_len=ca_cert_len`.
- Nenhum byte de certificado é copiado ou parseado no backend userland.
- `handshake_started` permanece `0`.
- Rejeições limpam todos os metadados de trust anchors.

## Segurança e compatibilidade

A etapa reduz risco de o futuro backend BearSSL consumir CA inconsistente, mas ainda evita parse prematuro e mantém o comportamento fail-closed. O contrato público continua inalterado: callers recebem unsupported para entradas estruturalmente válidas.

## Próximo incremento

Avancar para o adaptador BearSSL userland usando este contrato sem habilitar handshake ate completar validacao criptografica.
