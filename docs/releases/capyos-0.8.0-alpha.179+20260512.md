# CapyOS 0.8.0-alpha.179+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
receipt plan seguro para a tela de credenciais do loginwindow. O receipt plan
consome o record plan seguro e publica somente um ticket declarativo de recibo
para futura integracao GUI/compositor/display/auditoria persistente, sem mapear
memoria real, sem escrever pixels, sem persistir recibo real, sem persistir
registro real, sem anexar log real, sem escrever estado real, sem executar
flush/cache/barreira, sem estabelecer visibilidade real, sem armar fence real,
sem submeter timeline real, sem aguardar/sinalizar sync, sem armar deadline real,
sem armar timer de deadline, sem expirar deadline, sem reportar completion real,
sem acknowledge real, sem submeter ack, sem submeter retire, sem submeter
cleanup, sem submeter seal, sem submeter auditoria, sem submeter registro, sem
submeter recibo, sem limpar/liberar recursos reais, sem sincronizar CPU/GPU, sem
submeter DMA/output/display, sem commitar modo, sem fazer flip, sem acordar
compositor, sem autenticar pela GUI e sem carregar segredo, mascara, comprimento
ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_RECEIPT_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_receipt_plan`.
- Adicionada `login_window_credential_screen_receipt_plan_build()` para
  selecionar tickets `credential-screen-receipt-ticket`,
  `text-recovery-receipt-ticket`, `text-login-resume-receipt-ticket` ou
  `text-login-fallback-receipt-ticket`.
- Testes planejados cobrem recibo declarativo de credencial, recuperacao textual,
  resume textual, submit bloqueado, acao desconhecida, record plan ausente,
  record plan inseguro e tentativa de propagar estado unsafe ja submetido.

## Segurança

- O receipt plan exige record plan seguro, record permitido mas nao submetido,
  record ticket/target selecionados, persistencia de record desabilitada,
  persistencia de receipt desabilitada, CPU/GPU sync de receipt desabilitado,
  audit permitido mas nao submetido, append de log de auditoria desabilitado,
  seal permitido mas nao submetido, cleanup permitido mas nao submetido, retire
  permitido mas nao submetido, ack permitido mas nao submetido, completion
  permitido mas nao reportado, acknowledge exigido mas nao executado, deadline
  permitido mas nao armado, timer de deadline nao armado, deadline nao expirado,
  completion de deadline nao reportado, sync/timeline/fence/barrier/flush/
  framebuffer/blit/output/display/scanout/vsync/schedule/present/damage/
  compositor nao submetidos, rota selecionada, credenciais seguras, storage
  limpo, redacao de segredo e comprimento, ausencia de exposicao bruta/mascarada,
  submit bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-receipt-ticket` com `gui-submit-disabled`.
- `receipt_submitted`, `receipt_persist_allowed`, `receipt_persisted`,
  `receipt_cpu_gpu_sync_allowed`, `receipt_cpu_gpu_sync_submitted`,
  `record_submitted`, `record_persist_allowed`, `record_persisted`,
  `record_cpu_gpu_sync_allowed`, `record_cpu_gpu_sync_submitted`,
  `audit_submitted`, `audit_log_append_allowed`, `audit_log_appended`,
  `audit_cpu_gpu_sync_allowed`, `audit_cpu_gpu_sync_submitted`,
  `seal_submitted`, `seal_state_write_allowed`, `seal_state_written`,
  `seal_cpu_gpu_sync_allowed`, `seal_cpu_gpu_sync_submitted`,
  `cleanup_submitted`, `cleanup_resource_release_allowed`,
  `cleanup_resource_released`, `cleanup_cpu_gpu_sync_allowed`,
  `cleanup_cpu_gpu_sync_submitted`, `retire_submitted`,
  `retire_resource_release_allowed`, `retire_resource_released`,
  `retire_cpu_gpu_sync_allowed`, `retire_cpu_gpu_sync_submitted`,
  `ack_submitted`, `ack_cpu_gpu_sync_allowed`, `ack_cpu_gpu_sync_submitted`,
  `completion_reported`, `completion_acknowledged`,
  `completion_cpu_gpu_sync_allowed`, `completion_cpu_gpu_sync_submitted`,
  `deadline_armed`, `deadline_timer_armed`, `deadline_expired`,
  `deadline_completion_reported`, `deadline_cpu_gpu_sync_allowed`,
  `deadline_cpu_gpu_sync_submitted`, `sync_submitted`, `sync_wait_allowed`,
  `sync_wait_submitted`, `sync_signal_allowed`, `sync_signal_submitted`,
  `sync_deadline_armed`, `sync_completion_reported`,
  `sync_cpu_gpu_sync_allowed`, `sync_cpu_gpu_sync_submitted`,
  `timeline_submitted`, `timeline_wait_allowed`, `timeline_wait_submitted`,
  `timeline_signal_allowed`, `timeline_signal_submitted`,
  `timeline_semaphore_allowed`, `timeline_semaphore_submitted`,
  `timeline_value_allocated`, `timeline_value_published`,
  `timeline_cpu_gpu_sync_allowed`, `timeline_cpu_gpu_sync_submitted`,
  `fence_submitted`, `fence_wait_allowed`, `fence_wait_submitted`,
  `fence_signal_allowed`, `fence_signal_submitted`, `fence_fd_export_allowed`,
  `fence_fd_exported`, `fence_cpu_gpu_sync_allowed`,
  `fence_cpu_gpu_sync_submitted`, `barrier_submitted`,
  `barrier_memory_visibility_established`, `barrier_cache_visibility_established`,
  `barrier_cpu_gpu_sync_allowed`, `barrier_cpu_gpu_sync_submitted`,
  `flush_submitted`, `flush_cache_clean_allowed`, `flush_cache_cleaned`,
  `flush_memory_barrier_allowed`, `flush_memory_barrier_submitted`,
  `framebuffer_submitted`, `framebuffer_mapped`, `framebuffer_write_allowed`,
  `framebuffer_written`, `framebuffer_flushed`, `framebuffer_cache_cleaned`,
  `blit_submitted`, `blit_pixels_copied`, `blit_dma_allowed`,
  `blit_dma_submitted`, `output_submitted`, `display_submitted`,
  `display_mode_committed`, `scanout_submitted`, `vsync_submitted`,
  `schedule_submitted`, `present_submitted`, `damage_submitted`,
  `compositor_damage_submitted`, `frame_timer_armed`,
  `compositor_wake_submitted` e `page_flip_submitted` permanecem `0`; o contrato
  e declarativo e nao toca recibos, registros, logs, estado, recursos ou display
  reais.
- `receipt_allowed`, `receipt_ticket_selected` e `receipt_target_selected`
  preparam escalabilidade futura sem persistir recibo real, persistir registro
  real, anexar log real, escrever estado real, limpar/liberar recursos reais,
  submeter receipt, record, audit, seal, cleanup, retire, ack, acknowledge,
  CPU/GPU sync, output, compositor ou flip real neste patch.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
