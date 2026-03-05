# CapyOS - Guia Atualizado de Comandos

Este documento acompanha a distribuicao atual do CapyCLI. Cada comando
implementado possui ajuda integrada (`<comando> -help`).

Contexto operacional atual:
- trilha suportada: `UEFI/GPT/x86_64`
- smoke principal: `make smoke-x64-cli`
- auditoria de disco instalado: `make inspect-disk IMG=<imagem>`

## Como obter ajuda rapidamente

- Pressione `F1` para abrir imediatamente a documentacao interativa
  (`help-docs`) a partir do prompt.
- Qualquer comando aceita o sufixo `-help`, ex.: `list -help` ou
  `mk-file -help`.
- Em caso de erro de uso, o shell sugere o comando `-help` correspondente.

## Comandos implementados

| Comando | Uso basico | Observacoes |
|---------|------------|-------------|
| `list` | `list [caminho]` | Lista arquivos/diretorios; mostra `(vazio)` quando nao ha itens. |
| `go` | `go <caminho>` | Troca o diretorio atual (aceita caminhos relativos/absolutos). |
| `mypath` | `mypath` | Exibe o caminho absoluto atual. |
| `mk-file` | `mk-file <arquivo>` | Cria arquivo vazio (ou atualiza timestamp). |
| `mk-dir` | `mk-dir <caminho>` | Cria diretorios (toda a arvore informada). |
| `kill-file` | `kill-file <arquivo>` | Remove arquivo. |
| `kill-dir` | `kill-dir <diretorio>` | Remove diretorio vazio. |
| `clone` | `clone <origem> <destino>` | Copia arquivo; expande destino quando for diretorio. |
| `move` | `move <origem> <destino>` | Move ou renomeia. |
| `print-file` | `print-file <arquivo>` | Exibe conteudo completo. |
| `print-file-begin` | `print-file-begin <arquivo> [-n <linhas>]` | Mostra inicio do arquivo. |
| `print-file-end` | `print-file-end <arquivo> [-n <linhas>]` | Mostra final do arquivo. |
| `open` | `open <arquivo>` | Abre para edicao linha-a-linha; finalize com `.wq` para salvar. |
| `print-echo` | `print-echo [texto...]` | Replica texto na tela. |
| `print-version` | `print-version` | Informa versao e canal do CapyOS conforme manifesto (`VERSION.yaml`). |
| `print-time` | `print-time` | Horario simulado desde o boot (`hh:mm:ss`). |
| `print-host` | `print-host` | Exibe hostname carregado. |
| `print-me` | `print-me` | Exibe usuario autenticado. |
| `print-id` | `print-id` | Exibe UID/GID do usuario. |
| `add-user` | `add-user <usuario> <senha> [role]` | Cria usuario local (apenas admin). Roles: `user`, `admin`. |
| `set-pass` | `set-pass <usuario> <nova_senha>` | Altera senha. Admin altera qualquer conta; usuario comum altera apenas a propria. |
| `list-users` | `list-users` | Lista usuarios cadastrados em `/etc/users.db`. |
| `print-envs` | `print-envs` | Mostra variaveis basicas (`USER`, `HOME`, `HOST`) e exibe `CHANNEL` e `VERSION` atuais. |
| `net-status` | `net-status` | Exibe estado da rede no runtime x64 (driver, IPv4/mask, ARP, contadores). |
| `net-ip` | `net-ip` | Exibe o IPv4 local e mascara de rede configurados. |
| `net-gw` | `net-gw` | Exibe o gateway padrao configurado. |
| `net-dns` | `net-dns` | Exibe o DNS configurado. |
| `net-set` | `net-set <ip> <mask> <gateway> <dns>` | Aplica configuracao IPv4 estatica na stack atual de rede. |
| `hey` | `hey <ip|gateway|dns|self>` | Envia ICMP echo e responde no formato `hello from (...) Xms`. |
| `do-sync` | `do-sync` | Sincroniza buffers de disco. |
| `print-insomnia` | `print-insomnia` | Uptime desde o boot (`hh:mm:ss`). |
| `shutdown-reboot` | `shutdown-reboot` | Reinicia o sistema com sincronizacao de buffers. |
| `shutdown-off` | `shutdown-off` | Desliga (`halt`) apos sincronizar buffers. |
| `type` | `type <caminho>` | Informa tipo basico (arquivo ou diretorio). |
| `stats-file` | `stats-file <caminho>` | Mostra tamanho, UID/GID e permissoes. |
| `hunt-file` | `hunt-file <padrao> [onde]` | Busca apenas arquivos. |
| `hunt-dir` | `hunt-dir <padrao> [onde]` | Busca apenas diretorios. |
| `hunt-any` | `hunt-any <padrao> [onde]` | Busca arquivos e diretorios. |
| `find` | `find "texto" [caminho]` | Procura texto em arquivos. |
| `config-keyboard` | `config-keyboard [list|show|<layout>]` | Alterna layout de teclado (`us`, `br-abnt2`) e grava em `/system/config.ini`. |
| `help-any` | `help-any` | Lista comandos disponiveis. |
| `help-docs` | `help-docs` | Abre este documento no terminal. |
| `mess` | `mess` | Limpa a tela. |
| `bye` | `bye` | Encerra sessao atual e retorna para a tela de login. |

### Layouts de teclado

- `config-keyboard list` apresenta os layouts disponiveis.
- `config-keyboard show` exibe o layout ativo.
- `config-keyboard br-abnt2` ativa o layout brasileiro e salva o padrao.
- `config-keyboard us` volta ao padrao americano e salva o padrao.
- No layout `br-abnt2`, a tecla extra proxima ao Shift direito gera `/` ou `?`
  e o teclado numerico usa `,` como separador decimal.
- Durante a instalacao, escolha o layout antes de definir senhas para garantir
  que acentos e simbolos estejam corretos; o instalador grava o layout
  selecionado em `/system/config.ini`.

### Regras de sintaxe uteis

- Caminhos com espacos devem ser escritos entre aspas, ex.:
  `mk-file "notas pessoais.txt"`.
- `mk-dir` aceita caminhos com `/` para criar toda a arvore.
- `go`, `kill-file`, `move` e demais comandos aceitam aspas e barras finais.
- Use `config-keyboard list` para conferir os layouts disponiveis.

## Comandos planejados (nao implementados)

Estes comandos estao documentados apenas como referencia futura e retornarao
erro caso executados na versao atual:

| Comando planejado | Status |
|------------------|--------|
| `dp-file` | nao implementado |
| `count-lines` | nao implementado |
| `order` | nao implementado |
| `kill-duplicates` | nao implementado |
| `print-ps` | nao implementado |
| `watch` | nao implementado |
| `kill-ps` | nao implementado |
| `run-bg` | nao implementado |
| `run-fg` | nao implementado |
| `print-jobs` | nao implementado |

## Notas adicionais

- `bye` efetua logout e bloqueia o terminal ate nova autenticacao.
- Logs de processo ficam em `/var/log/setup.log` e `/var/log/cli-selftest.log`.
- Diretorios `/home/<usuario>`, `/tmp` e `/var/log` receberam permissoes
  ajustadas para permitir criacao de arquivos pelo usuario administrador.
