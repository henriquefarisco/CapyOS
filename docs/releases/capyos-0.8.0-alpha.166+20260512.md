# CapyOS 0.8.0-alpha.166+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um flush
plan seguro para a tela de credenciais do loginwindow. O flush plan consome o
framebuffer plan seguro e publica somente um ticket declarativo de flush/cache
para futura integracao GUI/compositor/display, sem mapear memoria real, sem
anexar buffer real, sem escrever pixels, sem executar flush/cache, sem submeter
barreira de memoria, sem submeter DMA/output/display, sem commitar modo, sem
fazer flip, sem enviar damage real, sem submeter compositor real, sem autenticar
pela GUI e sem carregar segredo, mascara, comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_FLUSH_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_flush_plan`.
- Adicionada `login_window_credential_screen_flush_plan_build()` para selecionar
  tickets `credential-screen-flush-ticket`, `text-recovery-flush-ticket`,
  `text-login-resume-flush-ticket` ou `text-login-fallback-flush-ticket`.
- Testes planejados cobrem flush declarativo de credencial, recuperacao textual,
  resume textual, submit bloqueado, acao desconhecida, framebuffer plan ausente,
  framebuffer plan inseguro e tentativa de propagar estado unsafe ja submetido.

## Segurança

- O flush plan exige framebuffer plan seguro, framebuffer autorizado mas nao
  submetido, framebuffer nao mapeado, escrita desabilitada, nenhum pixel escrito,
  flush/cache ainda nao executados, nenhum buffer de blit mapeado, nenhum pixel
  copiado, DMA desabilitado, flush/cache real e barreira de memoria
  desabilitados, modo de display nao commitado, flips desabilitados,
  scanout/vsync/schedule/present/damage/compositor nao submetidos, rota
  selecionada, credenciais seguras, storage limpo, redacao de segredo e
  comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-flush-ticket` com `gui-submit-disabled`.
- `flush_submitted`, `flush_cache_clean_allowed`, `flush_cache_cleaned`,
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
- `flush_allowed`, `flush_ticket_selected` e `flush_target_selected` preparam
  escalabilidade futura sem executar flush/cache real, barreira de memoria,
  output submit, compositor ou flip real neste patch.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
