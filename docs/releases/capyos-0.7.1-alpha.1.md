# CapyOS 0.7.1-alpha.1

Status: historical release note
Data: 2025-10-10
Canal: alpha
Commit base: f2e06f4

## Destaques historicos

- reforco na autenticacao do volume cifrado
- prevencao de reformatacao indevida em midia nao vazia
- fluxo de primeiro uso para criacao e confirmacao da senha do volume
- verificacao de acesso a `/etc/users.db` antes do prompt de login

## Observacao atual

Essa nota pertence ao periodo de transicao em que o sistema ainda tinha
cenarios em `ramdisk`. O comportamento atual validado e o boot por HDD
provisionado na trilha `UEFI/GPT/x86_64`.
