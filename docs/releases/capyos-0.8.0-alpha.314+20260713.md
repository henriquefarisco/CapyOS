# CapyOS 0.8.0-alpha.314+20260713

**Data:** 2026-07-13
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.314+20260713`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** fix de compatibilidade e estabilidade do navegador

## Resumo executivo

O navegador grafico deixa de encerrar ao receber paginas pesadas. A release
restaura o alinhamento ABI da stack de processos, usa o caminho TLS escalar que
nao exige preservar registradores SIMD e aumenta o limite de cabecalho HTTP.
Documentos maiores que o buffer sao renderizados como conteudo parcial marcado,
sem contaminar o cache. O primeiro boot passa a apontar para o novo indice
agregado e imutavel desta release.

## Mudancas

- `elf_loader`: alinhamento inicial correto da stack e mapeamento de stack
  userland suficiente para o caminho TLS.
- `capylibc-net` e navegador: cabecalho HTTP de 16 KiB, resposta truncada
  explicitamente marcada e modo de compatibilidade para paginas grandes.
- `capygfx`: diagnostico claro, liveness do compositor e smoke HTTPS direto
  para YouTube, Wikipedia e Tumblr.
- `capypkg`: mantem retry transacional e o indice agregado de nove modulos
  oficiais nesta tag.

## Validacao

- Testes focados: HTTP 167/167, navegacao 60/60, toolbar 49/49 e pipeline
  HTML 25/25.
- Smoke QEMU HTTPS direto: YouTube, Wikipedia e Tumblr renderizados, com dois
  heartbeats apos o carregamento e janela ainda viva.
- `make version-audit`, build/ISO e checksums: executados antes da tag; gates
  remotos sao acompanhados nesta publicacao.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.313+20260712` | `0.8.0-alpha.314+20260713` | Fix de estabilidade e pin do indice atualizado |

Sem mudanca de ABI de pacotes. Os sete repos irmaos permanecem nos pins
imutaveis anteriores e seus artefatos sao agregados no novo `modules-index.txt`.

_Build: `0.8.0-alpha.314+20260713`_
