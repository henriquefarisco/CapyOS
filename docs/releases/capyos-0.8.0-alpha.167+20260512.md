# CapyOS 0.8.0-alpha.167+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
barrier plan seguro para a tela de credenciais do loginwindow. O barrier plan
consome o flush plan seguro e publica somente um ticket declarativo de
barreira/visibilidade para futura integracao GUI/compositor/display, sem mapear
memoria real, sem escrever pixels, sem executar flush/cache, sem submeter barreira
de memoria real, sem estabelecer visibilidade real, sem sincronizar CPU/GPU, sem
submeter DMA/output/display, sem commitar modo, sem fazer flip, sem acordar
compositor, sem autenticar pela GUI e sem carregar segredo, mascara, comprimento
ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_BARRIER_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_barrier_plan`.
- Adicionada `login_window_credential_screen_barrier_plan_build()` para selecionar
  tickets `credential-screen-barrier-ticket`, `text-recovery-barrier-ticket`,
  `text-login-resume-barrier-ticket` ou `text-login-fallback-barrier-ticket`.
- Testes planejados cobrem barrier declarativo de credencial, recuperacao textual,
  resume textual, submit bloqueado, acao desconhecida, flush plan ausente,
  flush plan inseguro e tentativa de propagar estado unsafe ja submetido.

## Segurança

- O barrier plan exige flush plan seguro, flush autorizado mas nao submetido,
  cache clean e memory barrier desabilitados/nao executados, framebuffer nao
  mapeado, escrita e flush de framebuffer nao executados, nenhum pixel escrito,
  nenhum buffer de blit mapeado, nenhum pixel copiado, DMA desabilitado,
  output/display/scanout/vsync/schedule/present/damage/compositor nao submetidos,
  timer/fence/wake/flip desabilitados, rota selecionada, credenciais seguras,
  storage limpo, redacao de segredo e comprimento, ausencia de exposicao
  bruta/mascarada, submit bloqueado/desabilitado, callbacks de submit/auth zerados
  e login textual autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-barrier-ticket` com `gui-submit-disabled`.
- `barrier_submitted`, `barrier_memory_visibility_established`,
  `barrier_cache_visibility_established`, `barrier_cpu_gpu_sync_allowed`,
  `barrier_cpu_gpu_sync_submitted`, `flush_submitted`,
  `flush_cache_clean_allowed`, `flush_cache_cleaned`,
  `flush_memory_barrier_allowed`, `flush_memory_barrier_submitted`,
  `framebuffer_submitted`, `framebuffer_mapped`, `framebuffer_write_allowed`,
  `framebuffer_written`, `framebuffer_flushed`, `framebuffer_cache_cleaned`,
  `blit_submitted`, `blit_source_buffer_mapped`,
  `blit_destination_buffer_mapped`, `blit_pixels_copied`, `blit_dma_allowed`,
  `blit_dma_submitted`, `output_submitted`, `output_buffer_attached`,
  `output_buffer_submitted`, `output_flip_submitted`, `display_submitted`,
  `display_buffer_attached`, `display_mode_committed`, `display_flip_submitted`,
  `scanout_submitted`, `vsync_submitted`, `schedule_submitted`,
  `present_submitted`, `damage_submitted`, `compositor_damage_submitted`,
  `frame_timer_armed`, `compositor_wake_submitted` e `page_flip_submitted`
  permanecem `0`; o contrato e declarativo e nao toca display real.
- `barrier_allowed`, `barrier_ticket_selected` e `barrier_target_selected`
  preparam escalabilidade futura sem executar barreira real, flush/cache, CPU/GPU
  sync, output submit, compositor ou flip real neste patch.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
