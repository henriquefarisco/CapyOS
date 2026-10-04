# CapyOS 0.11.3+20261004

Candidata `0.11.3+20261004`, nao publicada. Latest imutavel permanece 0.11.1+20261004.

O cache do atualizador agora divide kernels maiores que 4.243.456 bytes em
partes de ate 2 MiB. Publica o descritor por ultimo, relê os bytes completos e
mantem tamanho, SHA-256 e assinatura Ed25519 obrigatorios antes de armar A/B.
CapyFS v2, boot slots, ABIs, pins e chaves de producao nao mudam.

A ponte OTA 0.11.2 cabe no arquivo do cliente antigo. Preserva o som de boot e
musicas existentes, mas adia a instalacao dos tres presets OGG ate a imagem
completa. Seu runtime tem identidade distinta, retorna da rota oficial
temporaria bridge.ini para latest.ini e nao pode gerar uma ISO instaladora.

## Evidencia local

- `make test TOOLCHAIN64=elf`: passou apos o bump e separacao dos testes
  (`build/migration-final-tests-v2.log`). Layout/version audits strict passaram.
- Cache com ASan/UBSan e falhas de escrita/corrupcao: passou.
- A/B QEMU com kernel de 7.416.616 bytes: download, readback, apply, reboot,
  confirmacao e rollback passaram em laboratorio (chave descartavel).
- Ponte de 4.067.152 bytes iniciou com loader extraido da ISO publica 0.10.0;
  a identidade 0.11.2 foi observada no banner.
- Ponte final host: 4.124.464 bytes, abaixo de 4.243.456, SHA-256
  `075e389483495ead7c6fcb0fd8584f611ea7f4264ca6fa77235f931ea20119f1`.
- Contratos de migracao, assinaturas, inventario 12/14 e workflows: passaram.
- Build completo host/ISO: passou; kernel de 7.473.584 bytes, SHA-256
  `0c31d5349bc626c1ba95f083cb6d189acb7282745c835b2c8470cda264179b0c`.
- ISO local SHA-256
  `50733579e8960eccad376f7ab43f20f261446c0e8ae93cbedf0f79aaac1cb0bd`:
  boot QEMU com presets passou; VMware Full, desktop, tres musicas e
  persistencia apos reboot passaram. Evidencia:
  `build/ci/migration-final-vmware-installer.manifest`, run `c9e5f1abbff9`;
  disco guard intacto e ISO inalterada. Esses nao sao ainda os bytes do CI.
- Dois boots QEMU HDA capturados: zero diferencas de amostras frente ao WAV
  original e capivara visivel durante o loading
  (`build/migration-final-boot-sound/`). Som de boot capturado no VMware HDA
  pela saida Realtek: erro RMS relativo 0,0002822354, mono do Windows preservado
  (`build/migration-final-vmware-boot/result-realtek.log`). A primeira captura
  pela saida HDMI/NVIDIA falhou no oracle de waveform (RMS 0,124655); nao e
  aceite de audio para essa saida do host. O harness de instalacao separadamente
  desativa som por projeto e nao comprova playback.

## Pendencias obrigatorias

PR/CI, draft com 14 assets, assinaturas offline e promocao imutavel.
Validar a ISO exata publicada no VMware. Depois,
predecessor publico 0.10.0 -> ponte -> full por HTTPS com chave de producao,
rollback para ponte, reaplicacao do cache e confirmacao final. Nao tratar os
testes de laboratorio como esse aceite. WAVs originais continuam preservados.
