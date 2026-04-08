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
| `service-status` | `service-status [nome]` | Exibe o estado dos servicos internos atuais (`logger`, `networkd`, `update-agent`), incluindo alvo ativo, alvo salvo, startup, criticidade, ultimo resultado, transicoes, polls cooperativos, cadencia em ticks, falhas, reinicios, backoff, limite de retry, dependencias e resumo. |
| `recovery-status` | `recovery-status` | Exibe o estado do boot degradado, alvo de bootstrap/requested/boot/ativo e diagnosticos basicos de storage/rede para a sessao de recuperacao. |
| `recovery-storage` | `recovery-storage` | Exibe o estado do runtime de storage, VFS raiz, volume persistente e caminhos criticos usados para recuperar o sistema. |
| `recovery-storage-repair` | `recovery-storage-repair [reset-admin <senha>]` | Reconstroi a base persistente minima quando o volume ja esta montado e, opcionalmente, recria/redefine a conta `admin` durante a recuperacao. |
| `recovery-network` | `recovery-network` | Exibe o estado detalhado da NIC/runtime de rede e a remediacao minima para promover targets com rede. |
| `net-status` | `net-status` | Exibe estado da rede no runtime x64 (`driver`, `detected`, `runtime`, `ready`, IPv4, ARP, contadores). No `Hyper-V`, tambem imprime `build=... feature=... diag=...`, `vmbus=` e `stage=` para validar a imagem em campo. |
| `net-refresh` | `net-refresh` | Atualiza o runtime de rede quando houver backend seguro para isso. No `Hyper-V`, avanca o controlador `NetVSC` em passos pequenos e controlados somente quando a offer sintetica ja estiver em cache. |
| `net-dump-runtime` | `net-dump-runtime` | Exibe um dump detalhado do runtime de rede, incluindo `vmbus=`, `stage=`, gate, fase, ultimo resultado e contadores de tentativas do `Hyper-V/StorVSC`. |
| `net-ip` | `net-ip` | Exibe o IPv4 local e mascara de rede configurados. |
| `net-gw` | `net-gw` | Exibe o gateway padrao configurado. |
| `net-dns` | `net-dns` | Exibe o DNS configurado. |
| `net-resolve` | `net-resolve <hostname>` | Resolve um hostname via DNS usando o servidor configurado na stack atual. |
| `net-set` | `net-set <ip> <mask> <gateway> <dns>` | Aplica IPv4 estatico na stack atual e salva em `/system/config.ini`. |
| `net-mode` | `net-mode [list|show|static|dhcp]` | Alterna entre modo estatico e DHCP, persistindo `network_mode=` em `/system/config.ini`. |
| `hey` | `hey <ip|hostname|gateway|dns|self>` | Envia ICMP echo e responde no formato `hello from (...) Xms`. |
| `do-sync` | `do-sync` | Sincroniza buffers de disco. |
| `service-control` | `service-control <start|stop|restart> <nome>` | Controla o ciclo de vida basico dos servicos internos suportados. |
| `service-target` | `service-target [show|list|apply <nome>]` | Mostra ou aplica o alvo ativo do supervisor de servicos (`core`, `network`, `maintenance`, `full`) e persiste a escolha em `/system/config.ini`. O boot pode degradar temporariamente o alvo ativo para `core` ou `maintenance` quando detectar falha estrutural. |
| `recovery-resume` | `recovery-resume <saved|core|network|full|maintenance>` | Em modo de recuperacao, tenta promover o runtime atual para outro alvo de servicos com guardrails minimos de storage/rede. |
| `recovery-verify` | `recovery-verify [saved|core|network|full|maintenance]` | Verifica se os prerequisitos minimos para promover um alvo de recuperacao ja estao presentes, sem aplicar a mudanca. |
| `runtime-native` | `runtime-native [show|prepare-input|prepare-storage|exit-boot-services|step]` | Exibe o gate do runtime nativo e executa passos manuais controlados do coordenador Hyper-V. O modo `show` tambem imprime `build=... feature=...`. |
| `print-insomnia` | `print-insomnia` | Uptime desde o boot (`hh:mm:ss`). |
| `config-theme` | `config-theme [list|show|<tema>]` | Alterna tema visual (`capyos`, `ocean`, `forest`) e grava em `/system/config.ini`. |
| `config-splash` | `config-splash [show|on|off]` | Alterna a animacao de splash do boot e grava em `/system/config.ini`. |
| `config-language` | `config-language [list|show|pt-BR|en|es]` | Alterna o idioma do usuario atual e grava no perfil do `home`. |
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

### Temas visuais

- `config-theme list` apresenta os temas disponiveis.
- `config-theme show` exibe o tema ativo.
- `config-theme ocean` aplica o tema azul/ciano e salva o padrao.
- `config-theme forest` aplica o tema verde/floresta e salva o padrao.
- `config-theme capyos` retorna ao tema padrao do sistema.

### Splash de inicializacao

- `config-splash show` exibe se o splash esta habilitado ou desabilitado.
- `config-splash on` habilita a animacao de splash no proximo boot.
- `config-splash off` desabilita a animacao e prioriza logs de inicializacao.
- A configuracao e gravada em `/system/config.ini` como `splash=enabled` ou
  `splash=disabled`.

### Modo de manutencao

- Quando a politica de boot degradar o alvo para `maintenance`, o sistema
  entra diretamente em uma sessao de recuperacao em vez de passar pelo login
  normal.
- Nessa sessao, o prompt usa o usuario sintetico `maintenance` e informa o
  motivo da degradacao logo abaixo do banner.
- Os comandos mais uteis nesse modo sao `service-status`,
  `service-target show`, `recovery-status`, `recovery-storage`,
  `recovery-storage-repair`,
  `recovery-network`, `recovery-verify`, `recovery-resume`, `do-sync`,
  `shutdown-reboot` e `shutdown-off`.
- Se `recovery-storage` indicar `ram-fallback=yes`, a shell de recuperacao
  esta rodando sobre um runtime temporario em RAM. Nesse caso, os reparos nao
  serao persistidos no volume principal; a correcao precisa ser feita via ISO,
  chave correta do volume ou remediacao externa do disco.
- Quando o volume persistente ja estiver montado, `recovery-storage-repair`
  recompõe `/system`, `/etc`, `/var/log`, regrava `/system/config.ini`,
  revalida `/etc/users.db` e pode recriar ou redefinir `admin` com
  `recovery-storage-repair reset-admin <senha>`.
- `recovery-resume saved` tenta voltar ao alvo persistido em
  `/system/config.ini`.
- `recovery-verify saved` valida primeiro se storage e, quando necessario,
  rede ja atendem ao alvo persistido antes de tentar a promocao.
- Se o storage validado ainda nao estiver disponivel, o sistema recusa sair
  do modo de recuperacao para evitar promover um runtime inconsistente.

### Idioma por usuario

- `config-language list` mostra os idiomas suportados: `pt-BR`, `en`, `es`.
- `config-language show` exibe o idioma ativo da sessao autenticada.
- `config-language en` muda o idioma do usuario atual para ingles.
- `config-language es` muda o idioma do usuario atual para espanhol.
- `config-language pt-BR` retorna ao idioma padrao em portugues do Brasil.
- A configuracao e gravada no perfil do usuario em
  `/home/<usuario>/.config/capyos/user.ini`.
- O idioma passa a ser aplicado automaticamente apos o login e persiste entre
  reboots.

### Rede

- `net-status` mostra duas camadas de estado:
  `driver=<nome>` identifica a NIC detectada;
  `runtime=<none|driver-missing|init-failed|ready>` identifica se existe
  backend funcional no kernel atual.
- Em `Hyper-V`, quando a offer sintetica de rede for descoberta, `net-status`
  tambem exibe `vmbus=relid:<n> conn:<n> dedicated=<yes|no>`.
- Em `Hyper-V`, `net-status` passa a exibir `netvsc=<stage>` e `vmbus=<stage>`
  com a trilha explicita `off|hypercall|synic|contact|offers|channel|control|ready|failed`.
- `net-refresh` esta desativado para `Hyper-V` nesta fase, porque qualquer
  consulta `VMBus` fora de um backend dedicado ainda reinicia a VM real.
- `net-dump-runtime` e a forma recomendada de validar o backend `Hyper-V`
  durante esta frente: ele mostra `gate`, `next`, `last_result`, numero de
  tentativas e estado do `StorVSC`, sempre com `vmbus=` e `stage=`.
- `runtime-native show` exibe o gate de `ExitBootServices` (`ebs=`), o gate
  de input `Hyper-V` (`input-gate=`) e o estado agregado da plataforma.
- `runtime-native show` agora tambem imprime `build=` e `feature=` para
  confirmar rapidamente se a VM carregou a build correta desta trilha.
- `runtime-native show` agora tambem imprime `tables=` e
  `vmbus=off|hypercall|synic|contact|offers` para distinguir preparo de
  hypercall, preparo de `SynIC`, contato do barramento e cache de offers.
- `runtime-native prepare-input` tenta apenas preparar a base `Hyper-V/VMBus`
  do input no nivel de hypercall, sem tentar `ExitBootServices`.
- `runtime-native prepare-bridge` e `runtime-native prepare-synic` seguem
  desativados no modo hibrido; a validacao em Hyper-V real mostrou reboot
  quando esses passos foram expostos antes do `ExitBootServices`.
- `runtime-native prepare-input` tambem fica desativado no modo hibrido; a
  validacao em Hyper-V real mostrou reboot mesmo no preparo minimo do
  hypercall.
- `runtime-native prepare-storage` no modo hibrido esta limitado ao passo
  passivo seguro; ele nao conecta `VMBus` nem abre canal.
- `runtime-native prepare-storage` tenta apenas avançar o `StorVSC` em um
  passo seguro e controlado: preparar barramento, conectar `VMBus`, cachear a
  offer ou avancar o controlador, dependendo do estado atual.
- `runtime-native exit-boot-services` tenta apenas o passo manual de
  `ExitBootServices`, respeitando os gates atuais.
- Quando o `runtime-native show` indicar `next=exit-boot-services`, o sistema
  ja reuniu os pre-requisitos minimos para tentar a transicao manual ao
  runtime nativo no passo seguinte.
- `runtime-native step` executa um unico passo controlado do coordenador de
  runtime nativo. Se um passo falhar, o comando agora deixa isso explicito em
  vez de reportar falsamente que houve avancos. O fluxo e incremental: um
  passo pode apenas preparar a base do `VMBus`, e o passo seguinte tenta o
  `ExitBootServices`.
- `detected=yes/no` separa ausencia real de hardware de ausencia de driver.
- `ready=yes/no` indica se a stack esta operacional para envio/polling.
- `net-resolve <hostname>` consulta um registro `A` via UDP/53 usando o DNS
  configurado na stack atual.
- `hey <hostname>` agora tenta resolver o nome via DNS antes do ICMP echo,
  usando o mesmo DNS configurado na stack atual.
- `net-set <ip> <mask> <gateway> <dns>` altera a stack atual e persiste a
  configuracao em `/system/config.ini`.
- `net-mode show` exibe o modo persistido ativo: `static` ou `dhcp`.
- `net-mode static` reaplica os valores salvos em `ipv4/mask/gateway/dns`.
- `net-mode dhcp` solicita lease dinamico imediatamente e persiste
  `network_mode=dhcp`.
- Os campos persistidos atualmente sao `ipv4=`, `mask=`, `gateway=` e `dns=`.
- O `config.ini` tambem guarda `network_mode=`.
- No boot instalado, a stack reaplica os valores estaticos salvos e, em
  `dhcp`, tenta lease dinamico antes do login. Se o lease falhar, o fallback
  estatico salvo continua disponivel.
- Em `dhcp`, o lease atual pode sobrescrever `ipv4/gateway/dns` apenas em
  runtime; o fallback estatico continua salvo no `config.ini`.

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
- `print-envs` agora tambem expõe `LANG=<idioma>` para facilitar auditoria da
  sessao atual.
