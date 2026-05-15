# CapyOS 0.8.0-alpha.25+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de defesa em profundidade para
`libcapy-net`: o helper público `capy_http_build_get_request` agora valida
`host` e `path` antes de montar a request line e o header `Host:`.

A versão alinhada é `0.8.0-alpha.25+20260509`.

## Principais entregas

### Builder HTTP defensivo

- `host` precisa existir e não pode ser vazio.
- `host` rejeita controles/espacos brutos (`<= 0x20`) e `DEL` (`0x7f`).
- `path` vazio continua virando `/` para compatibilidade.
- `path` não-vazio precisa começar com `/`.
- `path` rejeita controles/espacos brutos e `DEL`.
- Falhas de formato retornam `CAPY_NET_EPARSE`.

### Defesa em profundidade

`capy_url_parse` já bloqueia CR/LF/tabs vindos de URL completa. Este patch fecha
o bypass para callers que usam `capy_http_build_get_request` diretamente em
testes, ferramentas ou futura state machine do browser.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- CRLF bruto no `host` (`x.example\r\nX:evil`) recusado com `CAPY_NET_EPARSE`;
- tab bruto no `path` (`/ok\tbad`) recusado com `CAPY_NET_EPARSE`;
- path relativo não-vazio (`relative`) recusado com `CAPY_NET_EPARSE`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Requests válidas existentes continuam preservadas.
- `path == ""` continua sendo emitido como `/`.
- Percent-encoding segue preservado, porque apenas controles brutos são recusados.
- HTTPS segue bloqueado em `capy_http_get` até `libcapy-tls` userland.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- helpers internos `http_is_raw_ctl_or_space`, `http_host_safe` e
  `http_path_safe`;
- chamada defensiva antes de escrever no buffer de request;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter o smoke `smoke-x64-tls-handshake` como validação de F4 seção d.
