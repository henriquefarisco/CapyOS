# Plano de Testes do CapyCLI

Este roteiro valida o fluxo real atualmente suportado:

`UEFI/GPT -> BOOTX64.EFI -> kernel x64 -> volume DATA cifrado -> login -> CLI`

O objetivo nao e mais testar o legado em `ramdisk`, e sim garantir que o
sistema instalado suba corretamente pelo disco provisionado.

## 1. Precondicoes

- Build atualizado com `make all64`
- Toolchain checado com `make check-toolchain`
- Artefatos de boot gerados com `make iso-uefi`
- Disco provisionado por `tools/scripts/provision_gpt.py`
- Auditoria basica aprovada por `make inspect-disk IMG=<imagem>`

Observacao para ISO UEFI:
- `BOOTX64.EFI` precisa preservar a secao `.rodata` durante a conversao
  PE/COFF. O loader usa literais dessa secao para localizar `CAPYOS.INI`,
  imprimir o wizard e abrir os payloads de boot.
- O ISO oficial deve conter `CAPYOS.INI` na raiz do ISO e tambem no
  `efiboot.img`. A validacao automatica em `smoke_x64_common.py` verifica o
  marcador da arvore ISO antes de iniciar o QEMU.
- Em boot El Torito, o loader tambem aceita midia CD-ROM detectada pelo device
  path UEFI, alem do marcador e do atributo readonly.

### 1.1 Teste manual seguro do instalador

Antes desta fatia da Etapa 8, o loader escolhia silenciosamente o maior disco
fixo gravavel; o layout so era validado depois de `wipe_blocks()`, portanto um
disco pequeno podia ser apagado antes da falha de GPT. O fluxo atual deve:

1. listar discos fisicos fixos por `PathId`, `MediaId`, capacidade e DATA
   projetada;
2. numerar apenas discos de setores de 512 bytes com pelo menos
   `1880113664` bytes (1794 MiB arredondados para cima), incluindo 1 GiB minimo
   para DATA;
3. exigir o numero mesmo quando houver um unico alvo; `0` cancela sem escrita;
4. repetir a identidade selecionada junto da recovery key;
5. aceitar somente o token exato `ERASE`; Enter, `Y`, `erase` ou espacos
   cancelam;
6. revalidar path, media, geometria e o mesmo plano GPT antes do wipe.

Teste negativo recomendado: conectar um disco menor que 1794 MiB e um guard
elegivel com sentinela; o pequeno aparece como `[--]`, nunca pode ser escolhido,
e o guard nao selecionado deve permanecer byte a byte intacto. Sem alvo elegivel,
o loader informa `eligible-targets=0` e retorna sem prompt destrutivo.

Teste positivo recomendado: usar dois discos descartaveis distinguiveis pelo
`PathId`, selecionar explicitamente um deles, digitar `ERASE` e verificar que
somente ele recebe ESP/BOOT/DATA. Depois, concluir first boot, login, criar um
arquivo, reiniciar e confirmar persistencia.

O preflight automatizado de desenvolvimento executa esse contrato em QEMU:

```bash
make smoke-x64-qemu-installer-wizard TOOLCHAIN64=host
```

Ele conecta um alvo vazio de 2 GiB e um guard deterministico de 3 GiB, seleciona
o alvo pela capacidade unica e confirma o mesmo `PathId`, exige dois alvos
elegiveis, prova que o alvo mudou e compara o SHA-256 integral do guard antes e
depois. As imagens sao criadas exclusivamente sob `build/ci` e, em falha, sao
preservadas para analise sem truncar arquivos preexistentes. A persistencia nunca
e pulada quando o desktop inicia: o harness retorna ao CLI para gravar/sincronizar
o marker e, se o setup reiniciar antes do login, insere um boot adicional antes
da releitura. O retorno desktop→TTY usa o sentinel real de `CTRL+ALT+F1`, nao um
comando de terminal. A recovery key e redigida do log persistido. O gate QEMU e
o full install networked da `alpha.316` passaram no host em 2026-07-17. Na
`alpha.320`, o índice imutável declara exatamente nove módulos; o rerun
networked e a validação remota de URL, tamanho e SHA-256 permanecem pendentes
até a tag e os assets serem publicados. O preflight QEMU nao substitui nem
fecha o gate oficial VMware multi-disco.

### 1.1.1 Ciclo A/B assinado (Etapa 8, gate introduzido na `alpha.319`, candidato `alpha.320`)

```bash
make update-ab-selftest              # contrato host, tambem dentro de release-check
make smoke-x64-qemu-update-ab        # preflight de desenvolvimento
make smoke-x64-vmware-update-ab      # aceite oficial VMware + UEFI + E1000
```

Os dois gates instalam a ISO oficial num disco vazio e conduzem dois ciclos
completos em quatro power cycles: manifesto Ed25519 buscado por HTTP, payload
verificado por tamanho declarado + SHA-256, escrita no slot inativo com
autorizacao geracional e flush duravel, consumo da unica tentativa pelo loader,
confirmacao de saude persistente e, no segundo ciclo deixado sem confirmar,
rollback aplicado pelo loader e reportado por `update-rollback-check`.

Cada execucao gera um par Ed25519 descartavel e recompila o kernel com
`CAPYOS_UPDATE_LAB_TRUST_KEY_HEX` + `CAPYOS_UPDATE_LAB_MANIFEST_URL`, porque a
chave de release e offline-only. Esse kernel nao e publicavel: imprime
`[lab] update trust anchor overridden`, e recusado por `iso-uefi` fora do target
de smoke e reprovado por `release-check`. Detalhes, restricoes de ordem
(`update-confirm-health` precisa ser a primeira mutacao de boot slot do seu boot)
e o passo de operador com a chave de producao ficam em
[`../operations/etapa-8-signed-update-playbook.md`](../operations/etapa-8-signed-update-playbook.md).

Asserts por boot: `Boot provider: ready=yes reason=ready` em `print-boot-slot` e
`[boot] A/B attempt slot=<n> state=<confirmed|pending|rollback>` no boot log. O
gate emite manifesto de evidencia em `build/ci/` e, quando promovido, uma copia
sanitizada em [`../releases/evidence/`](../releases/evidence/).

### 1.2 First boot e desktop

Em disco vazio, o primeiro boot deve sempre mostrar `CAPYOS SETUP`, coletar
teclado, hostname, administrador, senha e perfil. Login direto e
`Provisionamento automatico` sao falha do smoke. A credencial automatizada usa
senha exclusiva de teste, nunca `admin`; portanto uma imagem antiga com
`admin/admin` nao pode produzir falso positivo.

A ISO oficial falha no build se `CAPYCFG.BIN` contiver setup/senha preseedados
ou se o kernel normal carregar o marker `capyai-gui-async`. Depois de qualquer
smoke com `EXTRA_CFLAGS64`, um `make iso-uefi` normal deve imprimir que a
variante mudou, reconstruir kernel/userland e produzir um ELF sem o marker de
smoke.

Apos login full, o desktop deve permanecer ativo ate logout, power ou
`CTRL+ALT+F1`. Um frame seguido de shell indica variante de smoke residual;
procure `[smoke] capyai-gui-async` entre `[desktop] session started/stopped`.
Logout durante autostart deve voltar ao login, nunca expor o shell classico.

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
9. `net-status`
10. `net-refresh`
11. `net-set 10.0.2.42 255.255.255.0 10.0.2.2 1.1.1.1`
12. `net-mode dhcp`
13. `net-resolve example.com`
13. `print-file /system/config.ini`
14. `shutdown-reboot`
16. autenticar novamente, validar `net-mode show`, `net-refresh`, `net-ip`,
    `net-gw`, `net-dns`, `net-resolve example.com` e executar `shutdown-off`

Criticos:
- nao pode haver reset espontaneo durante login ou CLI
- o teclado deve responder durante login e shell
- o `do-sync` nao pode quebrar o prompt nem perder a sessao
- `net-status` precisa refletir o estado real (`runtime=ready` quando o backend
  estiver operacional)
- `net-refresh` nao pode derrubar a sessao nem reiniciar a VM; em `Hyper-V`, ele
  deve avancar o controlador `NetVSC` em passos pequenos e controlados, sempre
  confirmado via `net-status`, sem derrubar a sessao ou reiniciar a VM
- em `Hyper-V Gen2`, a validacao deve registrar `runtime-native show`,
  `net-status` e `net-dump-runtime` junto do log serial completo; `Gen2`
  usa apenas `Network Adapter` sintetico
- `net-set` precisa persistir `ipv4/mask/gateway/dns` em `/system/config.ini`
- `net-mode dhcp` precisa obter lease no runtime atual e persistir
  `network_mode=dhcp`
- `net-resolve <hostname>` precisa retornar pelo menos um `ipv4=` valido quando
  o DNS do runtime estiver configurado e alcancavel
- `shutdown-reboot` precisa derrubar a instancia atual e permitir boot limpo
- `shutdown-off` precisa encerrar a instancia sem kill externo do hypervisor

## 4. Persistencia entre boots

1. No boot 1, criar um arquivo de teste e sincronizar.
2. Reiniciar com `shutdown-reboot`.
3. No boot 2, autenticar novamente.
4. Validar que o arquivo criado continua presente.
5. Validar que o usuario provisionado continua autenticando.
6. Validar que `net-mode show` preserva `dhcp` ou `static`.
7. Em `dhcp`, validar que `net-ip/net-gw/net-dns` mostram o lease atual.
8. Validar que `net-resolve example.com` continua funcional apos reboot.
9. Em `static`, validar que `net-ip/net-gw/net-dns` mostram os valores salvos.
10. Encerrar a instancia com `shutdown-off`.

Resultado esperado:
- nenhum reformat da particao `DATA`
- nenhum retorno ao fluxo efemero por `ramdisk`, exceto em contingencia
  explicitamente registrada

## 5. Automacao recomendada

Smoke principal:

```bash
make smoke-x64-cli
```

Smoke do instalador ISO:

```bash
python3 tools/scripts/smoke_x64_iso_install.py --build
```

Smoke de cancelamento do instalador ISO:

```bash
python3 tools/scripts/smoke_x64_iso_cancel.py --build
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

- a regressão de boot/instalação está corrigida na `alpha.320`: dois ciclos KVM
  focados passaram (quatro boots com persistência) e o smoke oficial ISO KVM
  passou em três boots, incluindo `marker:persist-ok`;
- o smoke CLI TCG passou em 86,2 s, com dois boots e persistência;
- o smoke oficial ISO TCG ainda não foi executado;
- o preflight do ciclo update A/B e o aceite VMware oficial (apply, reboot,
  confirmação e rollback) ainda não foram executados e mantêm a Etapa 8 aberta;
- o índice imutável possui nove entradas, mas a verificação remota
  pós-publicação depende da tag e dos assets;
- falhas apos o reboot pelo HDD instalado devem ser tratadas como problemas do
  runtime instalado, nao como ausencia do instalador;
- USB HID/XHCI da Etapa 3 permanece como gate regressivo; a validacao externa
  oficial foi aprovada em `alpha.245`;
- o caminho `EFI ConIn` ainda mantem parte do boot em modo hibrido
