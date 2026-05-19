# Media codecs — contrato de integração com CapyOS

**Status:** referência para desenvolvimento apartado.
**Integração planejada:** decoders de imagem nas Etapas 6-7; áudio/vídeo na Etapa 10.
**Repositório externo atual:** `CapyCodecs`.

## Migração inicial

BMP, PNG e JPEG foram extraídos para `CapyCodecs/src/image/` como decoders
portáteis com allocator injetado. PNG usa inflater injetado pelo host para
evitar dependência direta de `tinf` ou headers internos do CapyOS.

## Fronteira

O projeto apartado pode desenvolver codecs puros:

- PNG/JPEG/WebP/GIF/AVIF quando aplicável;
- WAV/OGG/Vorbis/MP3/Opus quando aplicável;
- vídeo software simples apenas quando o plano autorizar.

O CapyOS base fornece:

- file/network I/O;
- buffers alocados conforme política do sistema;
- render backend;
- audio device/mixer;
- sandbox e limites globais;
- UI do Image Viewer/Media Player/browser.

## API recomendada

Entrada:

- bytes imutáveis;
- tamanho;
- opções de limite;
- allocator/callback opcional fornecido pelo host.

Saída:

- metadata;
- buffer RGBA ou PCM;
- warnings;
- erro determinístico.

## Limites

- dimensão máxima;
- samples máximos;
- frames máximos;
- memória máxima;
- tempo máximo por decode em modo interativo.

## Segurança

- Parser fail-closed em overflow, truncamento e dimensões absurdas.
- Sem alocação não limitada.
- Sem acesso a arquivos/rede a partir do codec puro.
- Fuzz tests recomendados antes de integração com conteúdo remoto.

## Testes apartados obrigatórios

- golden decode fixtures;
- metadata tests;
- truncated/corrupt input tests;
- overflow/large dimension tests;
- round-trip quando codec suportar encode.

## Gate de integração CapyOS

Quando as etapas correspondentes estiverem ativas, recomendar execução externa de:

- `make smoke-x64-vmware-browser-https-static` para imagens no browser;
- `make smoke-x64-vmware-audio-playback-roundtrip` para áudio;
- gates de vídeo apenas quando o plano ativar vídeo como escopo.
