# Plano de Testes do CapyCLI

Este roteiro valida o fluxo real atualmente suportado:

`UEFI/GPT -> BOOTX64.EFI -> kernel x64 -> volume DATA cifrado -> login -> CLI`

O objetivo nao e mais testar o legado em `ramdisk`, e sim garantir que o
sistema instalado suba corretamente pelo disco provisionado.

## 1. Precondicoes

- Build atualizado com `make all64`
- Artefatos de boot gerados com `make iso-uefi`
- Disco provisionado por `tools/scripts/provision_gpt.py`
- Auditoria basica aprovada por `make inspect-disk IMG=<imagem>`

## 2. Validacao do boot

### 2.1 Firmware e loader
- Confirmar boot UEFI em `Geracao 2` / OVMF.
- Confirmar que `BOOTX64.EFI` abre o manifesto na particao `BOOT` ou por
  fallback da ESP.
- Confirmar handoff de framebuffer, mapa de memoria e metadados do disco.

### 2.2 Runtime de storage
- Confirmar deteccao do disco/particao `DATA`.
- Confirmar que o kernel valida o handle logico da particao e usa fallback RAW
  apenas em caso de erro real de probe.
- Confirmar que o volume cifrado monta sem loop de formatacao.

### 2.3 Login e shell
- Confirmar prompt de login.
- Autenticar com o usuario provisionado.
- Confirmar entrega do prompt do CapyCLI.

## 3. Smoke funcional minimo

Executar no sistema bootado:

1. `help-any`
2. `list /`
3. `mk-dir /tmp/smoke`
4. `mk-file /tmp/smoke/prova.txt`
5. `open /tmp/smoke/prova.txt`
6. `print-file /tmp/smoke/prova.txt`
7. `find "texto" /tmp/smoke`
8. `do-sync`
9. `bye`

Criticos:
- nao pode haver reset espontaneo durante login ou CLI
- o teclado deve responder durante login e shell
- o `do-sync` nao pode quebrar o prompt nem perder a sessao

## 4. Persistencia entre boots

1. No boot 1, criar um arquivo de teste e sincronizar.
2. Reiniciar com `shutdown-reboot`.
3. No boot 2, autenticar novamente.
4. Validar que o arquivo criado continua presente.
5. Validar que o usuario provisionado continua autenticando.

Resultado esperado:
- nenhum reformat da particao `DATA`
- nenhum retorno ao fluxo efemero por `ramdisk`, exceto em contingencia
  explicitamente registrada

## 5. Automacao recomendada

Smoke principal:

```bash
make smoke-x64-cli
```

Auditoria de disco:

```bash
make inspect-disk IMG=build/disk-gpt.img
```

Testes de host:

```bash
make test
```

## 6. Sinais de regressao

- loader encontra kernel mas o login nao sobe
- runtime cai para fallback RAW sem falha de probe
- volume `DATA` entra em loop de chave incorreta ou reformatacao
- boot 2 perde arquivo criado no boot 1
- input depende novamente de COM para completar o login

## 7. Lacunas atuais

- o instalador por ISO ainda nao tem smoke dedicado de ponta a ponta
- USB HID/XHCI continua incompleto
- o caminho `EFI ConIn` ainda mantem parte do boot em modo hibrido
