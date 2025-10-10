# Plano de Testes do NoirCLI

Este roteiro valida o fluxo completo desde o boot até a entrega do prompt do NoirCLI. Os blocos seguem a ordem de execução real e suas dependências. Cada etapa registra resultados em `/var/log/setup.log` (configuração) e `/var/log/cli-selftest.log` (diagnósticos do shell).

## 1. Kernel & subsistemas básicos
- **Memória**: `kinit()` inicializa heap; `kmem.o` rastreia uso.
- **GDT/IDT/ISR**: `gdt_install()`, `idt_install()`, `isr_install()` configuram vetores; interrupções mascaradas reabertas em `pic_remap()` + `sti`.
- **PIT**: `pit_init()` ativa relógio para `print-time`.
- **Buffer cache/VFS**: `buffer_cache_init()` e `vfs_init()` preparam acesso ao NoirFS.
- **Dispositivos**: `ramdisk_init()` cria disco virtual; `crypt_init()` o encapsula com AES-XTS.

## 2. NoirFS
- `mount_noirfs_root()` tenta montar; se falha, `noirfs_format()` formata, registra progresso no VGA e sincroniza cache.
- `system_run_first_boot_setup()` só inicia se `/system/first-run.done` não existir.

## 3. Assistente de Configuração
1. **Diretórios** (`ensure_directory`) → validação imediata.
2. **Banco de usuários** (`userdb_ensure`) → checa UID/GID e tamanho do arquivo.
3. **Instalação de docs** → loga sucesso/aviso.
4. **Coleta de hostname/theme/splash** usando `wizard_prompt`.
5. **Usuário admin**:
   - Criação de `/home/admin`.
   - Registro via `user_record_init` + `userdb_add`.
   - Autenticação cruzada com `userdb_authenticate`.
   - Dump do registro no log com salt + hash.
6. **Configuração**:
   - Grava `config.ini`, valida leitura (`system_load_settings`).
   - Marca primeiro boot concluído (`system_mark_first_boot_complete`).

Falhas em qualquer passo abortam com mensagem direta no console.

## 4. Pós-configuração (Kernel)
- Recarrega `system_settings` para aplicar theme definitivo.
- `system_show_splash()` opcional.
- `system_login()` apresenta prompts e registra tentativas.

## 5. NoirCLI Diagnostics
- `shell_self_test()` verifica:
  - Sessão ativa e usuário autenticado.
  - Acesso a `/` e `$HOME`.
- `shell_run_diagnostics()` (novo):
  - Executa comandos `list /`, `print-host`, `print-me`, `print-id`, `print-time`, `help-any`.
  - Registra sucesso/falha em `/var/log/cli-selftest.log`.
- Em caso de falha, mensagem de alerta é exibida antes de entregar o prompt.

## 6. Execução Interativa
- Prompt dinâmico atualizado via `shell_update_prompt()` (contém usuário, host e cwd).
- Comandos listados em `g_commands` podem ser estendidos; `help-any` sempre reflete a lista.

## Testes simultâneos/sincronização
- **Interrupções**: teclado (IRQ1) e PIT (IRQ0) permanecem ativos durante CLI; não há desativação seletiva após o assistente.
- **Buffer cache**: `do-sync` (automaticamente no shell) sincroniza sem pausar outros comandos; apenas `buffer_cache_sync` entra com exclusão simples.
- **Logs**: setup e CLI diagnostics usam VFS síncrono; operações são curtas e não bloqueiam teclado/CLI.

## Sugestões de melhoria futura
- Introduzir teste automatizado para `clone/move` e manipulação de arquivos.
- Expandir diagnósticos para rodar com usuário comum quando existentes outros perfis.
- Implementar rotação de `/var/log/*.log` para evitar crescimento indefinido.
