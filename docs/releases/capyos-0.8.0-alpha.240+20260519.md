# CapyOS 0.8.0-alpha.240+20260519

Release alpha `0.8.0-alpha.240+20260519` para o circuito modular de
instalacao e deploy de componentes.

## Destaques

- Adiciona perfil de instalacao em `/system/install/profile.ini` com modos `BASIC`, `FULL` e `CUSTOM`.
- Adiciona bootstrap de pacotes via `capypkg_bootstrap_run()` e comando `capysh pkg-bootstrap [--force]`.
- Reduz o poll do servico `SYSTEM_SERVICE_CAPYPKG` para facilitar retry durante aquecimento da rede.
- Adiciona `make modules-index` para agregar manifests dos repos externos em `build/capypkg/modules-index.txt`.
- Integra o formato de pacote dos repos externos com manifests line-oriented e payloads deterministas.

## Componentes Externos

- CapyAgent `0.0.2`
- CapyBrowser `0.0.2`
- CapyCodecs `0.0.2`
- CapyUI `0.6.0`
- CapyLang `0.1.1`
- CapyBenchmark `0.0.2`

## Validacao Esperada

- `make test`
- `make test-capypkg`
- `make layout-audit`
- `make version-audit`
- `make modules-index`
- `make all64 TOOLCHAIN64=host`
- `make iso-uefi TOOLCHAIN64=host`
- `make verify-release-checksums TOOLCHAIN64=host`
