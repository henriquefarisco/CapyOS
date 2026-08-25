# CapyOS 0.9.1+20260825

**Data:** 2026-08-25
**Canal:** stable (producao)
**Versao:** `0.9.1+20260825`
**Plataforma oficial:** VMware + UEFI + E1000
**Tipo:** patch de compatibilidade do instalador, recuperacao de confianca e hardening da promocao de release

## Resumo executivo

`0.9.1+20260825` transforma a regressao de entrada observada em VMware sem
COM1 em um gate reproduzivel sobre o artefato exato de release. O instalador
continua validando a UART 16550 por loopback e rejeitando leituras flutuantes
`0x00`/`0xFF`; a automacao agora comprova que a topologia realmente nao possui
dispositivo serial, que o prompt permanece estavel sem caracteres fantasmas e
que EFI ConIn aceita a sequencia de cancelamento sem alterar os discos de teste.

O wizard VMware multi-disco tambem passa a derivar e revalidar a capacidade do
extent VMDK real, confinar o arquivo de dados ao diretorio descartavel, rejeitar
evidencia obsoleta e provar que o mesmo SHA-256 da ISO foi preservado durante o
teste. A esteira de promocao permanece fail-closed: assinatura Ed25519 e
politicas remotas sao exigidas antes da unica transicao para publicacao e
Latest, sem armazenar chaves privadas no repositorio ou na CI.

Esta release também reprovisiona a chave dedicada do `latest.ini` por uma nova
instalação oficial. As alphas 313/314 foram assinadas pelo pin anterior, mas a
chave privada necessária para uma release-ponte não estava disponível. A
recuperação não enfraquece a verificação: instalações antigas precisam desta
ISO oficial; somente imagens 0.9.1 ou posteriores aceitam o novo catálogo.

## Mudancas

- Novo gate `smoke-x64-vmware-installer-no-uart-existing-iso`, com framebuffer
  RFB local, redimensionamento `DesktopSize`, captura de evidencias e teardown
  restrito a uma VM descartavel identificada.
- Gate VMware wizard endurecido com selecao por capacidade/`PathId`, confinamento
  do extent, disco guard maior e byte-identico, hash da ISO antes/depois e
  rejeicao de manifesto PASS antigo.
- Contratos host adicionados ao `make test` e ao `release-check` para a topologia
  sem UART, protocolo de entrada, evidencias, VMDKs e tag stable.
- Bootstrap Windows passa a exigir `vmcli.exe` junto de `vmrun.exe` e
  `vmware-vdiskmanager.exe`.
- Documentacao operacional passa a exigir QEMU sem UART, VMware sem UART e
  wizard VMware sobre a mesma ISO e o mesmo SHA-256.
- Fingerprint real da nova chave dedicada de checksums passa a ser autoridade
  versionada em `.github/release-policy/release-checksum-ed25519.sha256`.
- Âncora raw Ed25519 do update-agent é reprovisionada para
  `9a98d2011ba954a3975c9f628e2f9255df87f1f429c9665d51ac6aaf91f474e0`;
  clientes do pin anterior exigem reinstalação oficial, sem bypass de assinatura.

## Validacao

- `make release-check TOOLCHAIN64=elf CROSS64=/home/henriquefarisco/cross/bin/x86_64-elf`
  -- passa no WSL; testes, layout audit, version audit, build UEFI e checksums.
- ISO final validada byte a byte: SHA-256
  `0b2765d757d6e606c04d0af9f57623ab2af516e69b2941a70d2e819955fdf19c`.
- `smoke-x64-qemu-installer-no-uart` -- passa sem `isa-serial`; prompt limpo,
  EFI ConIn funcional e disco inalterado. Evidencias em
  `build/ci/release-v0.9.1-0b2765d7/qemu-no-uart/`.
- `smoke-x64-vmware-installer-no-uart-existing-iso` -- passa com zero UART,
  prompt visualmente estavel, entrada `0` localizada, cancelamento para o Boot
  Manager, ISO e ambos os VMDKs inalterados. Manifesto e capturas em
  `build/ci/release-v0.9.1-0b2765d7/vmware-no-uart/`.
- `smoke-x64-vmware-installer-wizard-existing-iso` -- passa com selecao explicita
  do alvo por capacidade/`PathId`, instalacao, primeiro boot, login, marcador de
  persistencia lido apos reboot e disco guard byte-identico. Manifesto v3 e logs
  publicos em `build/ci/release-v0.9.1-0b2765d7/vmware-wizard/`.
- `vmrun list` -- zero VMs apos cada gate VMware; a ISO manteve o mesmo SHA-256
  antes e depois dos tres cenarios.

Os hashes e evidências acima pertencem ao candidato local depois da recuperação
de confiança. O workflow da tag recompila o artefato; por isso, os três gates de
VM serão repetidos sobre os bytes exatos baixados do draft antes da promoção.
Esta nota não antecipa publicação, assinatura, imutabilidade ou Latest.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.9.0+20260821` | `0.9.1+20260825` | patch de instalador e release gates |
| **CapyUI** | `2.24.2` | `2.24.2` | pin e ABI mantidos |
| **CapyBrowser** | `0.6.7` | `0.6.7` | pin e ABI mantidos |
| **CapyAI** | `0.2.1` | `0.2.1` | pin e ABI mantidos |
| **CapyCodecs** | `0.0.12` | `0.0.12` | pin e ABI mantidos |
| **CapyAgent** | `0.0.10` | `0.0.10` | pin e ABI mantidos |
| **CapyLang** | `0.1.12` | `0.1.12` | pin e ABI mantidos |
| **CapyBenchmark** | `0.0.11` | `0.0.11` | pin e ABI mantidos |

Nao ha mudanca de ABI nem de contrato cross-repo.

_Build: `0.9.1+20260825`_
