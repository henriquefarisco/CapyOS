# CapyOS — Contrato userland de libcapy-tls

> **Status (alpha.264, Etapa 5 concluída):** este documento descreve o
> contrato **fail-closed** da `libcapy-tls` (caminho de opt-out `=0`), que era
> o comportamento de produção. A Etapa 5 já adicionou **in-tree** o
> handshake BearSSL userland real atrás de `CAPYOS_TLS_USERLAND_HANDSHAKE`
> (**promovida a default em alpha.264**): com a flag ligada (default), `capy_tls_is_supported()` retorna 1 e
> `capy_tls_connect_tcp()` executa o handshake real reusando o bundle de
> trust anchors do kernel (ver §"Contrato atual" para o caminho OFF). O que
> faltava para fechar a etapa (validação externa: link ring-3 + smoke VMware + release-check) foi concluído em alpha.264.
> Estado vivo por slice em
> [`etapa-5-tls-userland-readiness.md`](etapa-5-tls-userland-readiness.md).

## Objetivo

`libcapy-tls` é a fronteira userland planejada para tirar HTTPS do contexto kernel e permitir que browser, update-agent e ferramentas de diagnóstico usem TLS sem chamar o TLS kernel-side diretamente.

A partir de `0.8.0-alpha.64+20260510`, a biblioteca expõe uma API pública mínima e fail-closed. Em `0.8.0-alpha.65+20260510`, `libcapy-net` passa a usar essa fronteira por um adaptador HTTPS interno que rejeita antes de DNS/socket. Em `0.8.0-alpha.66+20260510`, `capy_tls_connect_tcp()` passa a rejeitar hostnames malformados antes do backend BearSSL userland. Em `0.8.0-alpha.69+20260510`, configurações explícitas que desativam `verify_peer` passam a ser rejeitadas. Em `0.8.0-alpha.70+20260510`, timeouts explícitos passam a respeitar a janela pública de `CAPY_TLS_TIMEOUT_MIN_MS` até `CAPY_TLS_TIMEOUT_MAX_MS`. Em `0.8.0-alpha.71+20260510`, a configuração pública passa por `capy_tls_config_resolve()` para gerar snapshot interno normalizado. Em `0.8.0-alpha.72+20260510`, `capy_tls_context_prepare()` materializa socket, hostname e configuração efetiva em contexto interno stack-local. Em `0.8.0-alpha.73+20260510`, `capy_tls_context_reset()`/`capy_tls_context_clear()` passam a higienizar contexto preparado e rejeitado. Em `0.8.0-alpha.74+20260510`, `capy_tls_context_acquire()`/`capy_tls_context_release()` introduzem um slot interno gerenciado, e `capy_tls_free()` passa a delegar para release seguro. Em `0.8.0-alpha.75+20260510`, `capy_tls_connect_tcp()` passa a adquirir, preparar e liberar esse slot antes de retornar unsupported. Em `0.8.0-alpha.76+20260510`, `capy_tls_backend_connect()` passa a consumir o contexto preparado como stub de backend e falhar fechado antes do handshake. Em `0.8.0-alpha.77+20260510`, o contexto passa a guardar estado interno de backend com SNI, timeout efetivo e flags de lifecycle. Em `0.8.0-alpha.78+20260510`, o backend stub passa a preparar metadados internos de trust anchors sem carregar certificados. Em `0.8.0-alpha.79+20260510`, `libcapy-tls` ganha uma fonte userland metadata-only para a contagem do bundle padrão. Em `0.8.0-alpha.80+20260510`, essa fonte passa a expor catálogo interno com distribuição RSA/EC. Em `0.8.0-alpha.81+20260510`, o catálogo ganha invariantes e fingerprint metadata-only. Em `0.8.0-alpha.82+20260510`, a fonte ganha tabela metadata-only de slots por anchor. Em `0.8.0-alpha.83+20260510`, a fonte ganha descritores metadata-only bounds-checked por anchor. Em `0.8.0-alpha.84+20260510`, a fonte ganha manifesto/resumo agregado metadata-only do trust store com proveniência e fingerprint. O handshake real userland (faltante por toda a janela alpha.64-93) passou a existir **in-tree na Etapa 5**, atrás de `CAPYOS_TLS_USERLAND_HANDSHAKE` (default OFF) — ver o banner de status no topo.

## Contrato atual

- `capy_tls_is_supported()` retorna `0`.
- `capy_tls_connect_tcp()` valida argumentos, hostname, configuração efetiva, adquire/prepara o slot gerenciado, chama o backend stub para preparar SNI, timeout e trust metadata internos usando a fonte padrão metadata-only e seu catálogo RSA/EC/fingerprint/slot-layout/descritores/resumo agregado/manifesto quando necessário, libera o slot e retorna `NULL` com `CAPY_TLS_EUNSUPPORTED` quando os inputs são estruturalmente válidos.
- `capy_tls_send()`, `capy_tls_recv()` e `capy_tls_close()` retornam erro enquanto não há backend real.
- `capy_tls_get_last_security_info()` retorna metadados zerados antes de qualquer handshake.
- `capy_tls_error_name()` e `capy_tls_state_name()` dão strings estáveis para logs e UI.

## Segurança

A regra central é não fazer downgrade silencioso. Enquanto `libcapy-tls` não tiver backend BearSSL userland, qualquer tentativa válida de TLS falha explicitamente como unsupported. Callers que recebem `https://` devem propagar erro, não tentar HTTP.

## Próximo incremento

O próximo passo técnico é recompilar BearSSL como objeto userland, conectar `capy_tls_connect_tcp()` ao socket TCP já aberto por `libcapy-net` e preencher `capy_tls_security_info` com versão, cifra, ALPN, trust anchors e validação de hostname.
