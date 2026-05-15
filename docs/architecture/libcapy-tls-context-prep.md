# CapyOS — Contexto preparado em libcapy-tls

## Objetivo

`libcapy-tls` precisa de um contexto interno que agregue socket, hostname e configuração efetiva antes de conectar BearSSL userland. `0.8.0-alpha.72+20260510` introduz essa preparação sem habilitar handshake e sem retornar contexto ao caller.

## Contrato em `0.8.0-alpha.72+20260510`

- `struct capy_tls_context` continua opaca na API pública.
- Internamente, o contexto guarda `socket_fd`, hostname copiado e `capy_tls_effective_config`.
- `capy_tls_context_prepare()` rejeita socket negativo, hostname inválido e configuração efetiva nula.
- Hostnames são copiados para buffer interno de 254 bytes, aceitando até 253 bytes úteis.
- `capy_tls_connect_tcp()` prepara o contexto em stack e retorna `CAPY_TLS_EUNSUPPORTED` enquanto BearSSL userland não existe.

## Segurança e manutenção

A preparação centralizada reduz o risco de o backend futuro consumir hostname cru ou configuração pública não normalizada. O contexto preparado será a fronteira para SNI, timeout, trust anchors e metadados de segurança.

## Próximo incremento

Em `0.8.0-alpha.73+20260510`, o contexto preparado ganhou reset/clear internos para limpar hostname e configuração efetiva antes do caminho unsupported retornar. O próximo passo é definir alocação/liberação real do contexto userland ou conectar o contexto preparado ao primeiro stub BearSSL userland sem handshake completo.
