# CapyOS 0.8.0-alpha.169+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
timeline plan seguro para a tela de credenciais do loginwindow. O timeline plan
consome o fence plan seguro e publica somente um ticket declarativo de
timeline/semaphore para futura integracao GUI/compositor/display, sem mapear
memoria real, sem escrever pixels, sem executar flush/cache/barreira, sem
estabelecer visibilidade real, sem armar fence real, sem aguardar/sinalizar fence,
sem exportar fd, sem submeter timeline real, sem aguardar/sinalizar timeline, sem
alocar ou publicar valor de timeline, sem submeter semaphore, sem sincronizar
CPU/GPU, sem submeter DMA/output/display, sem commitar modo, sem fazer flip, sem
acordar compositor, sem autenticar pela GUI e sem carregar segredo, mascara,
comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_TIMELINE_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_timeline_plan`.
- Adicionada `login_window_credential_screen_timeline_plan_build()` para
  selecionar tickets `credential-screen-timeline-ticket`,
  `text-recovery-timeline-ticket`, `text-login-resume-timeline-ticket` ou
  `text-login-fallback-timeline-ticket`.
- Testes planejados cobrem timeline declarativo de credencial, recuperacao
  textual, resume textual, submit bloqueado, acao desconhecida, fence plan
  ausente, fence plan inseguro e tentativa de propagar estado unsafe ja
  submetido.

## Segurança

- O timeline plan exige fence plan seguro, fence autorizado mas nao submetido,
  wait/signal/fd export/CPU-GPU sync de fence desabilitados, timeline ainda nao
  submetida, wait/signal/semaphore de timeline desabilitados, valor de timeline
  exigido mas nao alocado nem publicado, flush/cache/barreira real nao executados,
  framebuffer nao mapeado, escrita e flush de framebuffer nao executados, nenhum
  pixel escrito, nenhum buffer de blit mapeado, nenhum pixel copiado, DMA
  desabilitado, output/display/scanout/vsync/schedule/present/damage/compositor
  nao submetidos, timer/fence/wake/flip desabilitados, rota selecionada,
  credenciais seguras, storage limpo, redacao de segredo e comprimento, ausencia
  de exposicao bruta/mascarada, submit bloqueado/desabilitado, callbacks de
  submit/auth zerados e login textual autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-timeline-ticket` com `gui-submit-disabled`.
- `timeline_submitted`, `timeline_wait_allowed`, `timeline_wait_submitted`,
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
  e declarativo e nao toca display real.
- `timeline_allowed`, `timeline_ticket_selected` e `timeline_target_selected`
  preparam escalabilidade futura sem submeter timeline real, aguardar/sinalizar
  timeline, alocar/publicar valor, submeter semaphore, sincronizar CPU/GPU,
  submeter output, compositor ou flip real neste patch.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
