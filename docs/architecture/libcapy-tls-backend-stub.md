# CapyOS — Backend stub de libcapy-tls

## Objetivo

`libcapy-tls` precisa de uma fronteira interna de backend antes de integrar BearSSL userland real. `0.8.0-alpha.76+20260510` introduz `capy_tls_backend_connect()` para consumir contexto preparado e falhar fechado antes do handshake.

## Contrato em `0.8.0-alpha.76+20260510`

- `capy_tls_backend_connect()` recebe `struct capy_tls_context` interno.
- Contexto nulo ou socket negativo retorna `CAPY_TLS_EINVAL`.
- Hostname precisa continuar válido pela política compartilhada TLS.
- Configuração efetiva precisa manter `verify_peer=1`, CA consistente e timeout dentro da janela pública.
- Contexto estruturalmente pronto retorna `CAPY_TLS_EUNSUPPORTED`.
- Nenhum handshake BearSSL userland é iniciado.

## Segurança e compatibilidade

O stub cria uma barreira explícita entre preparação de contexto e backend real. Isso reduz risco de o futuro BearSSL consumir estado incompleto e preserva o contrato público atual: callers continuam recebendo unsupported para conexões TLS válidas.

## Próximo incremento

Em `0.8.0-alpha.77+20260510`, o contexto ganhou estado interno de backend com SNI, timeout efetivo e flags de lifecycle. Em `0.8.0-alpha.78+20260510`, o backend stub também prepara metadados mínimos de trust anchors sem carregar certificados reais. Em `0.8.0-alpha.79+20260510`, o backend stub usa uma fonte userland metadata-only para a contagem do bundle padrão. Em `0.8.0-alpha.80+20260510`, o backend stub propaga a distribuição RSA/EC desse catálogo. Em `0.8.0-alpha.81+20260510`, o stub também propaga máscara e fingerprint metadata-only. Em `0.8.0-alpha.82+20260510`, o stub propaga contagem e fingerprint da tabela metadata-only de slots. Em `0.8.0-alpha.83+20260510`, o stub propaga contagem e fingerprint de descritores metadata-only. Em `0.8.0-alpha.84+20260510`, o stub propaga schema, source, flags e fingerprint do manifesto/resumo agregado metadata-only. Em `0.8.0-alpha.90+20260510`, o backend passa a consumir um bundle userland metadata-only por anchor. Em `0.8.0-alpha.91+20260510`, o backend passa a consumir um plano interno do BearSSL userland reservado, ainda com handshake desabilitado. Em `0.8.0-alpha.92+20260510`, o backend tambem consome um estado BearSSL reservado metadata-only com engine/contexto/buffers ausentes. Em `0.8.0-alpha.93+20260510`, o backend consome o adaptador BearSSL metadata-only, ainda sem inicializar engine e sem handshake.
