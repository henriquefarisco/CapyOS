# CapyOS 0.9.2+20260826

**Data:** 2026-08-26
**Canal:** stable (producao)
**Versao:** `0.9.2+20260826`
**Plataforma oficial:** VMware + UEFI + E1000
**Tipo:** correcao do aceite A/B de producao e hardening da evidencia pos-promocao

## Resumo executivo

`0.9.2+20260826` corrige uma lacuna do processo de release: o target VMware
existente exercitava somente uma chave descartavel de laboratorio e a sequencia
documentada tentava reaplicar o mesmo release depois da confirmacao de saude.
Em producao isso e corretamente recusado pelo anti-downgrade, porque o payload
confirmado passa a reportar a mesma versao publicada em `latest.ini`.

O novo modo de producao instala a ISO `0.9.1+20260825` publicada, fixa seus bytes
ao SHA-256 oficial, verifica o `latest.ini` e o `capyos64.bin` publicos sem
receber chave privada e exige a rota HTTPS `releases/latest/download`. O ciclo
executa rollback primeiro; ja no predecessor restaurado, reaplica a `0.9.2` e
confirma saude. Essa ordem prova rollback e confirmacao com um unico release
novo sem enfraquecer a politica de versao.

A `0.9.1` permanece registrada como bootstrap da ancora Ed25519 corrente: a
`0.9.0` continha o pin anterior e nao poderia consumir o catalogo novo. Assim,
o primeiro predecessor oficial apto ao gate e exatamente a `0.9.1`.

## Mudancas

- `smoke_x64_vmware_update_ab.py` ganha modo `--production`, recusa chave
  privada, host HTTP local, data lab e override do trust pin, e valida assinatura,
  source, canal, branch, versao, URL, tamanho e SHA-256 dos assets publicos.
- O boot de producao falha se o banner `CAPYOS_UPDATE_LAB_TRUST_KEY_HEX` aparecer
  ou se a versao observada nao casar com predecessor/candidato em cada fase.
- Novo target
  `smoke-x64-vmware-update-ab-production-existing-iso`, sem rebuild da ISO e sem
  material sensivel, confinado a discos/VMs descartaveis sob `build/ci`.
- Evidencia de producao separada declara ancora, chave publica, hash da ISO,
  rotas publicas, ordem `rollback-then-confirm` e invariantes do loader/updater.
- Contrato host cobre comparacao de versoes igual ao runtime, canais do
  `VERSION.yaml`, schema da evidencia e casos negativos de trust, URL, hash,
  ordem, provider e vazamento de recovery key.
- O driver E1000 preserva a identidade MAC fornecida pelo firmware/hipervisor
  durante o reset, reprograma RAR0, espera `CTRL.RST` com limite e falha fechado
  quando nao existe endereco unicast valido; o gate vincula o MAC visto pelo
  guest ao MAC exato declarado no VMX.
- O cliente DHCP passa a transmitir de `0.0.0.0`, emite mensagens BOOTP/DHCP de
  no minimo 300 bytes com `END` antes dos octetos `PAD` e mede os timeouts pelo
  relogio monotono do PIT, em vez de tratar um loop de `pause` como milissegundo.
- A selecao inicial de porta TCP efemera usa entropia e evita portas ja abertas;
  o download idempotente do payload admite uma unica repeticao limitada apos
  falha transitoria. O harness deixa de esperar o timeout inteiro quando o shell
  ja devolveu o prompt com erro e tolera apenas quebras visuais de linha.
- O disco VMware usa o descritor VMDK, nao o arquivo `-flat.vmdk`, preservando a
  geometria e o lifecycle esperado pelo `vmrun`.
- Playbooks distinguem gate de mecanismo (lab) de aceite pos-promocao e
  documentam explicitamente a invariante de bootstrap da ancora.
- A politica de publicacao aceita um perfil permanente para repositorio pessoal
  solo: PR e squash obrigatorios, zero bypass, delete/force-push bloqueados,
  branch atualizada, threads resolvidas e seis checks autenticados no lugar de
  uma aprovacao humana impossivel.

## Validacao

- `make update-ab-selftest` no WSL -- aprovado, incluindo KAT Ed25519 e casos
  negativos do contrato de producao.
- Preflight Windows sobre os bytes publicos da `0.9.1` -- assinatura, versao,
  rota imutavel, tamanho e SHA-256 aceitos com o pin de producao.
- `make release-check` no WSL -- aprovado: suite completa, layout/version audit,
  selftests de assinatura e update A/B, build UEFI oficial e checksums dos cinco
  artefatos.
- Gate VMware lab A/B -- aprovado em quatro boots reais, com E1000/DHCP nativos,
  apply, confirmacao, novo apply sem confirmacao, rollback e persistencia. A
  evidencia nao inclui chave de recovery e `vmrun list` terminou com zero VMs.
- Gate QEMU/OVMF lab A/B -- aprovado em quatro boots, cobrindo apply,
  confirmacao, reaplicacao sem confirmacao e rollback. O harness usa apenas a
  serial interativa para reconhecer a conclusao de comandos, sem aceitar prompt
  antigo reapresentado pelo debugcon.
- Varredura dos binarios oficiais -- nenhum banner, pin ou material de
  laboratorio encontrado; SHA-256 da ISO candidata:
  `2290955a931b52b7dd3f59df988a5b4b52cdb77f986ded1dff2f27dea5121d53`.
- Gate VMware de producao `0.9.1` -> `0.9.2` -- obrigatoriamente
  pos-promocao e, portanto, nao e antecipado por esta nota.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.9.1+20260825` | `0.9.2+20260826` | gate A/B de producao executavel e fail-closed |
| **CapyUI** | `2.24.2` | `2.24.2` | pin e ABI mantidos |
| **CapyBrowser** | `0.6.7` | `0.6.7` | pin e ABI mantidos |
| **CapyAI** | `0.2.1` | `0.2.1` | pin e ABI mantidos |
| **CapyCodecs** | `0.0.12` | `0.0.12` | pin e ABI mantidos |
| **CapyAgent** | `0.0.10` | `0.0.10` | pin e ABI mantidos |
| **CapyLang** | `0.1.12` | `0.1.12` | pin e ABI mantidos |
| **CapyBenchmark** | `0.0.11` | `0.0.11` | pin e ABI mantidos |

Nao ha mudanca de ABI nem de contrato cross-repo.

_Build: `0.9.2+20260826`_
