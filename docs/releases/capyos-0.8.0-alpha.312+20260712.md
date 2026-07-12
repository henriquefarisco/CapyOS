# CapyOS 0.8.0-alpha.312+20260712

`0.8.0-alpha.312+20260712` conclui o browser grafico estatico e consolida as
correcoes de estabilidade acumuladas desde alpha.309.

## Entregue

- Browser ring-3 interativo com toolbar fixa, barra de endereco, Go,
  voltar/avancar, reload seletivo, historico, links, scroll e estados de erro.
- Fetch real HTTP/HTTPS com redirects limitados, chunked estrito, cookies,
  cache, HSTS, CSS externo e imagens decodificadas por CapyCodecs.
- Limites fail-closed de pagina, recursos, redirects, imagens e tempo; paginas
  estaticas de ate 256 KiB e `Location` completo de ate 2047 bytes.
- Lifecycle de janela/processo endurecido para abrir e fechar repetidamente sem
  acumular processos, janelas ou mappings; page tables proprias e NXE coerente
  no BSP e nos APs.
- Capy Wizard com instalacao transacional, retry/backoff limitado, invalidacao
  de cache por URL e lista oficial alinhada ao indice agregado.
- CapyAI grafico integrado ao desktop; pedidos saem do preemption guard e o
  caminho assincrono impede comandos longos de bloquear frames da interface.
- Siblings imutaveis: CapyBrowser `0.6.7`, CapyUI `2.24.0` e CapyAI `0.1.0`.
- `modules-index.txt` passa a ser asset desta release do CapyOS, com URLs e
  hashes verificaveis para todos os pacotes oficiais.
- Updater passa a usar chave Ed25519 real e endpoint `latest.ini` da release.
  Aplicacao em disco permanece explicitamente indisponivel ate existir escrita
  atomica de slot, readback e rollback persistente; nao ha falso sucesso.

## Limite de escopo

O browser e deliberadamente estatico: nao executa JavaScript, WebAssembly,
extensoes ou plugins. HTML, CSS, imagens, links e navegacao sao o escopo desta
release.

## Gates

- Testes host de transporte, cache, navegacao, toolbar, render e lifecycle.
- Smoke QEMU de site controlado: redirect, CSS, imagem, link, historico e
  reload, com contagem independente dos endpoints.
- Build x86_64/UEFI, auditoria de layout/versao, manifests reproduziveis e
  verificacao remota de tags/assets/indices.
