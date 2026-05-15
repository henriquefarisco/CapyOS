# CapyOS — Fonte userland de trust anchors em libcapy-tls

## Objetivo

`libcapy-tls` precisa de uma fonte interna para representar o bundle padrão de trust anchors antes de conectar BearSSL userland real. `0.8.0-alpha.79+20260510` cria essa fonte como metadata-only. `0.8.0-alpha.80+20260510` transforma a fonte em catálogo metadata-only. `0.8.0-alpha.81+20260510` fixa invariantes e fingerprint metadata-only do catálogo. `0.8.0-alpha.82+20260510` materializa uma tabela userland metadata-only de slots por anchor. `0.8.0-alpha.83+20260510` adiciona descritores metadata-only por anchor. `0.8.0-alpha.84+20260510` adiciona manifesto metadata-only do trust store. `0.8.0-alpha.85+20260510` adiciona resumo agregado metadata-only dos tamanhos do material dos anchors. `0.8.0-alpha.90+20260510` materializa o bundle userland metadata-only com uma entrada por anchor.

## Contrato em `0.8.0-alpha.90+20260510`

- `CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT` declara a contagem padrão atual: `146`.
- `CAPY_TLS_DEFAULT_TRUST_ANCHOR_RSA_COUNT` declara `106` anchors RSA.
- `CAPY_TLS_DEFAULT_TRUST_ANCHOR_EC_COUNT` declara `40` anchors EC.
- `CAPY_TLS_CUSTOM_TRUST_ANCHOR_SLOT_COUNT` declara um slot custom opaco.
- `CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK` declara presença RSA/EC como `0x3`.
- `CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT` declara a identidade metadata-only derivada `0xDB22D94A`.
- `CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT` declara o layout metadata-only derivado `0x07A622AB`.
- `CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT` declara os descritores metadata-only derivados `0xE1A18A70`.
- `CAPY_TLS_DEFAULT_TRUST_ANCHOR_DESCRIPTOR_FLAGS` declara metadata-only, slot-backed, key-type-known e cert-bytes-absent.
- `CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FINGERPRINT` declara a identidade do bundle metadata-only `0x0ED4E969`.
- `CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_ENTRY_FLAGS` declara metadata-only, subject-DN-size-known, key-material-size-known e cert-bytes-absent.
- `CAPY_TLS_DEFAULT_TRUST_ANCHOR_BUNDLE_FLAGS` declara metadata-only, kernel-bundle-derived, cert-bytes-absent e fail-closed-only.
- `CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT` declara o manifesto metadata-only v3 derivado `0xBD2653A4`.
- `CAPY_TLS_DEFAULT_TRUST_MANIFEST_FLAGS` declara metadata-only, kernel-bundle-derived, cert-bytes-absent e fail-closed-only.
- `CAPY_TLS_DEFAULT_TRUST_MATERIAL_SUMMARY_FINGERPRINT` declara o resumo agregado metadata-only `0x8DFC9FAF`.
- `CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_TOTAL_BYTES` declara `14385` bytes agregados de DNs de sujeito.
- `CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_TOTAL_BYTES` declara `47329` bytes agregados de material de chave.
- `CAPY_TLS_DEFAULT_TRUST_SUBJECT_DN_MAX_BYTES` declara maximo agregado `213`.
- `CAPY_TLS_DEFAULT_TRUST_KEY_MATERIAL_MAX_BYTES` declara maximo agregado `515`.
- `capy_tls_default_trust_anchor_catalog()` retorna o catálogo interno.
- `capy_tls_default_trust_anchor_slots()` retorna a tabela interna metadata-only de slots.
- `capy_tls_default_trust_anchor_descriptor()` retorna descritor bounds-checked e zera a saída inválida.
- `capy_tls_default_trust_material_summary()` retorna o resumo agregado metadata-only.
- `capy_tls_default_trust_anchor_bundle_entries()` retorna a tabela userland metadata-only por anchor.
- `capy_tls_default_trust_anchor_bundle()` retorna o resumo do bundle userland metadata-only.
- `capy_tls_default_trust_anchor_bundle_entry()` retorna entrada bounds-checked e zera a saida invalida.
- `capy_tls_default_trust_store_manifest()` retorna o manifesto interno metadata-only.
- `capy_tls_default_trust_catalog_consistent()` valida contagens, máscara, fingerprint derivado e metadata-only.
- `capy_tls_default_trust_anchor_slots_consistent()` valida índices, tipos RSA/EC, distribuição e fingerprint de layout.
- `capy_tls_default_trust_anchor_descriptors_consistent()` valida descritores contra slots, flags, distribuição e fingerprint.
- `capy_tls_default_trust_anchor_bundle_consistent()` valida entradas contra slots, distribuicao RSA/EC, tamanhos agregados e fingerprint.
- `capy_tls_default_trust_store_manifest_consistent()` valida manifesto contra catálogo, slots, descritores, bundle, resumo agregado, flags e fingerprint.
- `capy_tls_default_trust_anchors_available()` exige catálogo, slots, descritores, resumo agregado e manifesto consistentes.
- O backend stub usa esse catálogo para preencher `trust_anchor_count`, `trust_anchor_rsa_count`, `trust_anchor_ec_count`, `trust_anchor_key_type_mask`, `trust_catalog_fingerprint`, `trust_anchor_slot_count`, `trust_slot_layout_fingerprint`, `trust_anchor_descriptor_count`, `trust_descriptor_fingerprint`, `trust_anchor_bundle_entry_count`, `trust_anchor_bundle_fingerprint`, `trust_material_summary_fingerprint`, `trust_subject_dn_total_bytes`, `trust_key_material_total_bytes`, `trust_subject_dn_max_bytes`, `trust_key_material_max_bytes`, `trust_manifest_schema_version`, `trust_manifest_source_id`, `trust_manifest_flags` e `trust_manifest_fingerprint` quando não há CA custom.
- Nenhum certificado é copiado, parseado ou exposto.
- Nenhum objeto BearSSL é requerido pelo módulo userland novo.

## Segurança e compatibilidade

A fonte é interna e não altera a ABI pública. O catálogo reduz acoplamento com o bundle kernel-side ao criar uma fronteira userland explícita para metadados, mas ainda evita ativar validação criptográfica incompleta.

## Próximo incremento

O backend plan BearSSL fail-closed consome este contrato; o proximo passo e reservar estado BearSSL metadata-only sem executar handshake real.
