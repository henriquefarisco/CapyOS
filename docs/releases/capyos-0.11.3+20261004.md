# CapyOS 0.11.3+20261004

Release `0.11.3+20261004` publicada como Latest imutavel com 14 assets.
[Download oficial](https://github.com/henriquefarisco/CapyOS/releases/tag/v0.11.3+20261004).
Commit do artefato: `584f6290cb0ce221e4765c04f4db016b1fafeee7` (PR #78).

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

## Publicacao e bytes finais

- CI final `37234552890` e CodeQL `37234552899`: passaram; merge protegido
  sem bypass. ReleaseArtifacts `37235177730` e promocao `37235892015`: sucesso.
- Inventario estrito dos 14 downloads publicos, checksums e assinaturas Ed25519
  de ambos os manifestos verificados com as ancoras de producao.
- ISO publicada: 24.766.464 bytes, SHA-256
  `f7411f491fce9da11d6acd830ce13dcd91f8c2cf5234a815bf15c83302d767e2`.
- Kernel completo: 7.404.368 bytes, SHA-256
  `09e5eed7a1b0dbf299603637f70c8b12542bb95660538d913a942b6ed738bbb6`.
- Ponte: 4.054.888 bytes, SHA-256
  `386f0218be04ed08b62a9ef9ef1baee9b52f954dea3e268c01ec89d2713c16f9`.
- ISO exata do CI/publicada passou instalacao Full, login, desktop, tres presets
  e persistencia apos reboot no VMware (run `e6f2c525aeea`,
  `build/ci/release-0.11.3-vmware-installer.manifest`). Guard e ISO intactos;
  disco descartavel limpo. Este harness desativa audio, nao mede playback.
- Releases/tags imutaveis anteriores preservados; nenhuma chave, ABI ou pin
  alterado. WAVs originais preservados.

## Aceite de migracao ainda aberto

predecessor publico 0.10.0 -> ponte -> full por HTTPS com chave de producao,
rollback para ponte, reaplicacao do cache e confirmacao final. Nao tratar os
testes de laboratorio como esse aceite. Run `adf7f1978975` parou ANTES do fetch:
o editor sob admin UID 1000 nao pode recriar repository.ini pertencente ao root.
Run `89aa6eef9493` tentou manutencao pelo comando existente; apos reboot voltou
ao login normal. Diagnostico estreito no disco preservado confirmou que
`service-target apply maintenance` mostra sucesso mas persiste `network`.
No kernel extraido da ISO publica, `system_service_target_or_default` trata
`service_manager_target_find(...) == 0` como sucesso, embora essa API retorne o
ID (maintenance=2). `kernel_service_target_from_settings` repete esse contrato
incorreto. Alterar somente o kernel novo nao repara o cliente que ainda nao o
consegue baixar. A copia manual do manifesto assinado pelo editor tambem nao
e uma rota valida: `TTY_BUFFER_MAX=128` trunca a linha Ed25519 de 145 caracteres.

Essas tentativas nao aplicaram update e nao contam como aceite. Logs sanitizados
preservados em `build/ci/smoke_x64_vmware_update_ab_{run}.<fase>.log` e
`build/migration-maintenance-debug.log`; ajustes experimentais retirados.
O helper de editor agora recusa imediatamente o retorno prematuro ao shell,
com teste de regressao, em vez de esperar um prompt que nao sera emitido.
Uma rota de recuperacao/migracao externa ao editor antigo deve ser definida e
validada antes de fechar o aceite OTA. Nao alterar permissoes, assinaturas,
ancoras, release ou tags imutaveis para esconder essas falhas.
