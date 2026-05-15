# CapyOS — Hardening HTTP request-target em libcapy-net

## Objetivo

Este documento registra o contrato de segurança do request-target HTTP usado por `libcapy-net` a partir de `0.8.0-alpha.63+20260510`.

O objetivo é manter o parser de URL e o builder público de requisições GET alinhados: qualquer byte aceito por `capy_url_parse` para path/query também precisa ser seguro para `capy_http_build_get_request`, e callers diretos do builder não podem contornar essa política.

## Política atual

O request-target HTTP rejeita:

- controles brutos e espaços (`<= 0x20`) e `DEL`;
- `#`, porque fragmentos são estado client-side e não devem ir ao servidor;
- `\`, porque proxies, servidores e frameworks podem normalizar backslash como slash;
- `%00`, porque componentes downstream podem decodificar percent-encoding e truncar strings C em NUL.

O request-target permite percent-encoding comum, inclusive outros octetos codificados, para preservar compatibilidade com URLs existentes.

## Superfícies protegidas

- `capy_url_parse` aplica a política em path e query antes de preencher `struct capy_url_parts.path`.
- `capy_http_build_get_request` aplica a mesma política para callers que constroem requisições sem passar pelo parser de URL.
- `capy_http_get` herda a proteção por usar `capy_url_parse` antes de construir a requisição.

## Compatibilidade

A ABI pública de `libcapy-net` não muda. As funções continuam retornando `-1` com `CAPY_NET_EPARSE` quando o request-target é inseguro.

HTTPS continua retornando `CAPY_NET_EUNSUPPORTED` até `libcapy-tls` ser entregue.

## Regressões declarativas

`tests/test_capylibc_net.c` registra casos para:

- backslash em path de URL;
- `%00` em path de URL;
- `%00` em query de URL;
- backslash em path passado diretamente ao builder;
- `%00` em path passado diretamente ao builder.

Nesta entrega, os testes foram adicionados e revisados estaticamente, sem execução de `make` ou suíte de testes.
