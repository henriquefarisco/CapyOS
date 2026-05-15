# CapyOS — Estado interno de backend em libcapy-tls

## Objetivo

`libcapy-tls` precisa preparar estado interno consumível pelo futuro backend BearSSL userland sem iniciar handshake prematuramente. `0.8.0-alpha.77+20260510` adiciona `struct capy_tls_backend_state` ao contexto privado. `0.8.0-alpha.78+20260510` estende esse estado com metadados de trust anchors. `0.8.0-alpha.79+20260510` passa a preencher a contagem padrão a partir de uma fonte userland interna. `0.8.0-alpha.80+20260510` propaga a distribuição RSA/EC do catálogo metadata-only. `0.8.0-alpha.81+20260510` propaga máscara e fingerprint do catálogo. `0.8.0-alpha.82+20260510` propaga contagem e fingerprint da tabela de slots. `0.8.0-alpha.83+20260510` propaga contagem e fingerprint de descritores metadata-only. `0.8.0-alpha.84+20260510` propaga schema, source, flags e fingerprint do manifesto metadata-only. `0.8.0-alpha.85+20260510` propaga o resumo agregado metadata-only de tamanhos do material. `0.8.0-alpha.90+20260510` propaga contagem e fingerprint do bundle userland metadata-only por anchor. `0.8.0-alpha.91+20260510` propaga o plano interno fail-closed do backend BearSSL reservado. `0.8.0-alpha.92+20260510` propaga o estado BearSSL reservado metadata-only. `0.8.0-alpha.93+20260510` propaga o adaptador BearSSL metadata-only.

## Contrato em `0.8.0-alpha.77+20260510`

- `capy_tls_context` guarda `capy_tls_backend_state` internamente.
- O estado registra se contexto, SNI e timeout estão prontos.

## Extensão em `0.8.0-alpha.93+20260510`

- O estado registra se metadados de trust anchors estão prontos.
- CA custom válida marca um anchor interno esperado e guarda apenas o tamanho.
- Configuração padrão marca trust metadata pronta sem declarar certificados carregados.
- Configuração padrão registra `trust_anchor_count=146` via fonte userland metadata-only.
- Configuração padrão registra `trust_anchor_rsa_count=106` e `trust_anchor_ec_count=40`.
- Configuração padrão registra `trust_anchor_key_type_mask=0x3` e `trust_catalog_fingerprint=0xDB22D94A`.
- Configuração padrão registra `trust_anchor_slot_count=146` e `trust_slot_layout_fingerprint=0x07A622AB`.
- Configuração padrão registra `trust_anchor_descriptor_count=146` e `trust_descriptor_fingerprint=0xE1A18A70`.
- Configuração padrão registra `trust_anchor_bundle_entry_count=146` e `trust_anchor_bundle_fingerprint=0x0ED4E969`.
- Configuração padrão registra `trust_material_summary_fingerprint=0x8DFC9FAF`, `trust_subject_dn_total_bytes=14385`, `trust_key_material_total_bytes=47329`, `trust_subject_dn_max_bytes=213` e `trust_key_material_max_bytes=515`.
- Configuração padrão registra `trust_manifest_schema_version=3`, `trust_manifest_source_id=0x1`, flags `0xF` e `trust_manifest_fingerprint=0xBD2653A4`.
- Configuração padrão registra `backend_plan_ready=1`, `handshake_allowed=0`, schema `1`, engine BearSSL userland `0x1`, flags `0xF` e fingerprint `0x4F809D54`.
- Configuração padrão registra `bearssl_state_ready=1`, `bearssl_engine_initialized=0`, schema `1`, engine `0x1`, flags `0x1F`, fingerprint `0x7D1732D0` e bytes reservados de contexto/I/O em `0`.
- Configuração padrão registra `bearssl_adapter_ready=1`, `bearssl_adapter_initialized=0`, schema `1`, engine `0x1`, flags `0x1F` e fingerprint `0xE73E3E65`.
- CA custom mantém distribuição RSA/EC, máscara, fingerprint, layout de slots, descritores, resumo agregado e manifesto zerados porque não há parse do certificado.
- `handshake_started` permanece `0` enquanto BearSSL userland não existir.
- SNI é derivado do hostname já validado e copiado para buffer próprio.
- `timeout_ms` guarda o timeout efetivo normalizado pela configuração.
- Reset, clear e release limpam todo o estado backend.
- Rejeições do backend stub limpam o estado antes de retornar `CAPY_TLS_EINVAL`.

## Segurança e compatibilidade

O estado backend não altera a ABI pública porque `struct capy_tls_context` continua opaco. O SNI usa a política compartilhada de hostname e o mesmo limite interno, reduzindo risco de divergência entre validação e consumo pelo backend real.

## Próximo incremento

Avancar para preflight de handshake userland ainda fail-closed antes de validacao criptografica completa.
