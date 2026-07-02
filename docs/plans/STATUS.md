# CapyOS — Status executivo

**Data:** 2026-07-01 ? **Vers?o:** `0.8.0-alpha.308+20260702` ? **Plataforma oficial:** VMware + UEFI + E1000 ? **P?blico alvo:** usu?rio desktop comum

> **Fonte de verdade:** [`active/capyos-master-plan.md`](active/capyos-master-plan.md).
> **Implementação finalizada (alpha.93):**
> [`historical/implementation-delivered-through-alpha93.md`](historical/implementation-delivered-through-alpha93.md).
> **Snapshot da sequência antiga (pré-reordenação ROI):**
> [`historical/capyos-master-plan-pre-roi-reorder.md`](historical/capyos-master-plan-pre-roi-reorder.md).
> Este documento mostra apenas o plano ativo sequencial. Itens concluídos
> foram condensados aqui e ficam preservados em detalhe nos documentos
> históricos e em [`../../VERSION.yaml`](../../VERSION.yaml) (history por alpha).
>
> **Reorganização 2026-05-15:** Etapas 3-15 foram reordenadas por ROI ao
> usuário desktop comum e expandidas para 14 etapas (3-16) sem violar a
> regra sequencial estrita. Etapas 1-2 não foram afetadas.

---

## Progresso global

- **Base histórica:** 100% consolidada até `alpha.93`; Etapa 1 fechada em `alpha.100`.
- **Plano sequencial novo (pós-reordenação ROI):** Etapas 1-6 oficialmente fechadas; 6/16 etapas concluídas.
- **Etapa 6 CONCLUIDA (alpha.287; gates externos VMware validados pelo operador). Etapa 7 EM ANDAMENTO desde alpha.288 (browser web estatica).** Historico da Etapa 6 -- Apps básicos do desktop maduros + `CapyBrowse Text` (desbloqueada pelo fecho da Etapa 5 em alpha.264; Slice 6.4 com adapter CapyOS-side implementado + build-validado in-tree — `make capybrowse-elf` (link cross-repo) + `make test` verdes — pendente do gate externo `make smoke-x64-vmware-capybrowse-text` para fecho). **Atualizacao alpha.267 (2026-06-17):** corrigido o bloqueador central de exec userland ring-3 -- process_enter_user_mode passou a carregar o CR3 do address space do processo antes da transicao (o caminho boot-direct nao passava pelo context switch do scheduler, entao o CR3 ficava no kernel AS sem a imagem ELF do usuario -> #PF de instruction-fetch em _start em TODO programa). make smoke-x64-hello-user passa sob QEMU+OVMF (hello chega a ring 3 e imprime hello, capyland; zero panics) e make test verde (commit d759900). Os gates externos VMware capybrowse-text e apps-basic-roundtrip seguem pendentes (QEMU = feedback de dev; VMware = aceite oficial). **Atualizacao alpha.269 (2026-06-17):** corrigida a ordem de bytes do endereco de destino no socket_connect do kernel -- o sin_addr nao era convertido de network para host order (so a porta era), entao um connect literal para 10.0.2.2 saia no fio como 2.2.0.10 (trocado duas vezes, comprovado por pcap QEMU). Com o fix simetrico, o gate de dev smoke-x64-qemu-capybrowse-text passa ponta-a-ponta (SYN -> 10.0.2.2, handshake TCP + roundtrip HTTP, marker [smoke] capybrowse-text ready) e make test segue verde. VMware capybrowse-text e apps-basic-roundtrip seguem como gates oficiais pendentes. **Atualizacao alpha.273 (2026-06-17):** o CapyBrowse Text passou a localizar seus diagnosticos no idioma da sessao logada (PT-BR/ES) em vez de fixo em EN, via o novo syscall SYS_GET_SESSION_LANG (=44, SYSCALL_COUNT 44->45): o handler le session_active()/session_language() e devolve um codigo estavel (0=pt-BR / 1=en / 2=es; sem sessao -> pt-BR pela invariante de selecao padrao; nao reconhecido -> en como base de fallback). O ring-3 ganha o stub capy_get_session_lang + o mapeador puro host-testado capybrowse_session_lang_string; cb_diag_lang do app resolve o idioma em runtime (override -DCAPYOS_CAPYBROWSE_LANG mantido para testes deterministicos). Fecha o criterio i18n da Etapa 6 para o caminho de erro do navegador. make test verde (ABI SYS_GET_SESSION_LANG==44 / SYSCALL_COUNT==45; test_capybrowse_view 20/20), capybrowse-elf linka, layout-audit sem warnings. ABI aditiva de capyos-base; 6 irmaos sem bump. **Atualizacao alpha.274 (2026-06-17):** o CapyBrowse Text passou a exibir um aviso localizado de uma linha para status HTTP de erro (>= 400) -- ex. "HTTP 404: Pagina nao encontrada" em PT-BR/EN/ES (fallback EN), via a nova funcao pura host-testada capybrowse_format_status_notice; status < 400 (sucesso/redirect) nao gera aviso e o caminho feliz fica inalterado. Fecha a parte HTTP do criterio i18n da Etapa 6 ("mostra erros claros de DNS/TLS/HTTP"). make test verde (test_capybrowse_view 31/31, +11 casos), capybrowse-elf linka, layout-audit limpo. Smoke-safe (example.com -> 200 -> sem aviso). Sem mudanca de ABI nem de contrato cross-repo. **Atualizacao alpha.275 (2026-06-17):** o CapyBrowse Text deixou de mangle conteudo binario -- passa a checar o Content-Type da resposta (capybrowse_content_is_text, pura host-testada) e, para conteudo nao-textual (imagem/PDF/octet-stream/...), emite um aviso localizado PT-BR/EN/ES ("Conteudo nao-textual (<tipo>): nao exibivel em modo texto") e encerra limpo, em vez de passar bytes binarios ao core HTML-to-text. text/*, html, xml, json e Content-Type ausente seguem renderizando. make test verde (test_capybrowse_view 45/45, +14 casos), capybrowse-elf linka, layout-audit limpo. Smoke-safe (example.com -> text/html -> exibivel). Sem mudanca de ABI nem de contrato cross-repo. **Atualizacao alpha.278 (2026-06-17, pareado com CapyUI 2.22.3):** apps-basic-roundtrip smoke (Slice 6.6), 1o milestone. DESCOBERTA na implementacao: os apps basicos do CapyUI sao funcoes IN-KERNEL (compiladas no kernel ELF, chamam o compositor direto), NAO processos ring-3 -- o modelo de exit-de-processo do design doc original era inviavel. Caminho correto: o CapyUI expoe calculator_smoke_roundtrip() (calc_eval headless) + o agregador src/apps/apps_smoke.c implementando o contrato CapyOS include/apps/apps_smoke.h (superficie aditiva de capy-ui-desktop-session); o CapyOS ganha o orquestrador in-kernel kernel_boot_run_apps_roundtrip (gated CAPYOS_APPS_ROUNDTRIP_SMOKE, branch em kernel_main) que roda cada smoke e alimenta o latch apps_roundtrip_smoke (reusado, SEM process_exit), emitindo [smoke] apps-basic-roundtrip ready; + o alvo make smoke-x64-vmware-apps-basic-roundtrip. REQUIRED_APPS=1 (calculator); expande app-a-app. Validado: make test verde (0 FAIL), all64 GATADO compila E LINKA o wiring cross-repo, layout-audit limpo. Design doc corrigido. O marker em runtime e o gate VMware externo (operador). **Atualizacao alpha.279 (2026-06-17, pareado com CapyUI 2.22.4):** apps-basic-roundtrip expandido ao 2o app (task_manager): task_manager_smoke_roundtrip exercita a enumeracao task_iter/process_iter headless (a mesma das abas TASKS/PROCESSES), retornando 0 num snapshot sao (>=1 task viva, process count nao-negativo); apps_smoke total()=2, REQUIRED_APPS=2. O orquestrador ganhou a guarda total()==REQUIRED_APPS: recusa rodar em drift (gate falha em vez de falso-positivo) e, com igualdade, o marker so dispara se TODOS os apps passam. Validado: make test verde + layout-audit limpo + all64 GATADO (REQUIRED_APPS=2) compila E LINKA o wiring cross-repo (task_manager_smoke_roundtrip + apps_smoke + calculator + orquestrador + latch). Sem mudanca de ABI. **Atualizacao alpha.280 (apps-roundtrip COMPLETO, pareado com CapyUI 2.22.5):** o conjunto apps-basic-roundtrip passa a cobrir os 5 apps basicos -- +file_manager (helpers puros de path), +text_editor (text_editor_handle_key sobre o singleton g_editor), +settings (validador de username), alem de calculator/task_manager. apps_smoke total()=5, REQUIRED_APPS=5; a guarda total()==REQUIRED_APPS garante que o marker so dispara se os 5 passarem. Validado: make test verde + layout-audit limpo + all64 GATADO (REQUIRED_APPS=5) compila E LINKA os 5 smokes. Slice 6.6 completo no nivel de codigo/build; resta o gate VMware externo (operador). **Atualizacao alpha.281 (correcao do smoke task_manager, pareado com CapyUI 2.22.6):** a revisao de correcao dos 5 smokes apps-basic-roundtrip pegou um bug latente em task_manager_smoke_roundtrip (alpha.279): exigia count_tasks()>=1, mas a run queue pre-login esta vazia (task_current()==NULL; o demo que criaria tasks e no-op sem CAPYOS_PREEMPTIVE_DEMO) -> count==0 -> o gate VMware externo falharia por timeout em runtime (em alpha.279-280 so o link era validado). Fix: o criterio passa a ser 'a enumeracao task_iter/process_iter roda e termina com contagens sas e estaveis', data-independent (passa com 0 tasks). Os outros 4 smokes ja eram data-independent. Validado: make test verde + all64 GATADO (REQUIRED_APPS=5) linka. Sem mudanca de ABI. **Atualizacao alpha.282 (hardening de seguranca, auditoria):** o ELF loader (elf_load) passou a validar que o span virtual de cada PT_LOAD cabe na metade de usuario (novo helper subtraction-only elf_vaddr_in_user_range, user_top=VMM_USER_TOP) -- antes um ELF forjado podia (a) dar wrap no arredondamento de pagina de vaddr_end -> num_pages gigante -> exaustao de PMM, ou (b) mapear PTEs USER na metade de kernel (vmm_map_page nao valida o vaddr). Binarios legitimos inalterados; host test novo. A auditoria das areas P0 (assinatura capypkg, crypto de volume, parsers DNS/DHCP) nao achou outras vulnerabilidades. **Atualizacao alpha.286 (2026-06-17, fix critico de instalacao):** completa o trabalho de ordem de bytes do alpha.269 para o caminho via DNS. O alpha.269 adicionou o byteswap incondicional de sin_addr em socket_connect para corrigir connect por IP literal (10.0.2.2 -> 2.2.0.10), mas http_transport_connect (src/net/services/http/transport.c) entrega o IP resolvido por DNS em host/canonical order SEM swap (so a porta era convertida) -> apos alpha.269 o endereco vindo do DNS passou a sair INVERTIDO no fio (github.com 140.82.121.3 -> SYN para 3.121.82.140; release-assets 44.230.85.241 -> 241.85.230.44), 0 SYN-ACK, e a instalacao completa (profile=full) falhava repetindo 'Index download failed (rc=-6) connection failed tls=init' para TODOS os 7 modulos oficiais (a base do sistema instalava normal). O capybrowse (IP literal) nao expunha o bug, e o smoke-x64-iso usa profile=basic + SLIRP restrict=on (sem egress), entao a CI nunca exercitou o download real -> o bug escapou ~17 alphas. FIX (1 linha + comentario): converter o ip canonico para network-order em transport.c (espelha o swap de sin_port; socket_connect aplica o ntohl inverso). Reproduzido e CONFIRMADO em QEMU (instalacao ISO completa sobre SLIRP com NAT de saida): antes 36 SYN/0 SYN-ACK -> 'connection failed'; depois 18 SYN/18 SYN-ACK, ~4MB baixados, 7 modulos 'Completed: 7 Failed: 0', 'Install complete'. make test verde; all64 relink limpo; sem mudanca de ABI; 6 irmaos sem bump (pin CapyUI v2.13.0 + indice agregado dos 7 modulos integros no GitHub, nao eram a causa). Lacuna de teste (download real de modulos fora da CI) + follow-up de robustez (resolucao por compatibilidade no publish) registrados no plano mestre (Etapa 8). **Atualizacao alpha.287 (2026-06-17, FECHA a Etapa 6 + hardening pos-fix):** com a instalacao de modulos validada (operador) e os 2 gates externos VMware (apps-basic-roundtrip + capybrowse-text) confirmados, a Etapa 6 esta CONCLUIDA (6/16). Para fechar a lacuna que deixou o bug do alpha.286 escapar ~17 alphas, este release adiciona: (a) #2 -- gate de CI advisory de download REAL de modulos: novo alvo make smoke-x64-iso-modules-net + opt-in --first-boot-net no smoke de instalacao (sobe rede QEMU user-net real/SLIRP NAT) + workflow .github/workflows/modules-download.yml (dispatch + semanal), exercitando o caminho DNS+TLS+redirect que a CI nunca tocou (profile=basic + restrict=on); (b) #3-guarda -- pin de modules-index single-sourced em VERSION.yaml (modules_index.url) + checagem no version-audit que FALHA se o CAPYOS_DEFAULT_MODULES_INDEX_URL em modules.c divergir (mata a classe 'pin manual esquecido'). O resolve-at-publish completo (indice assinado endereçado por token de ABI) e o signer Ed25519 (P0) ficam sequenciados para a Etapa 8, como o plano ja preve. Validado: make test verde; version-audit verde (com a nova guarda); layout-audit limpo; sem mudanca de ABI; 6 irmaos sem bump. Proxima etapa: 7 (browser usavel com web estatica moderna). **Atualizacao alpha.288 (2026-06-17, ABRE a Etapa 7 / Slice 7.1):** primeiro render backend CapyOS-side para o display-list desacoplado do capy-browser-core. O CapyBrowser (v0.6.5) ja entrega puro + host-testado o core estatico (DOM M1, CSS M2, block layout M3a, e o display-list versionado capy_dl M3b com nos TEXT/RECT/IMAGE/LINK em celulas de layout); faltava o CapyOS consumir esse display-list. Novo modulo userland/bin/capybrowse/browser_render.{c,h} (capyos_browser_render_text) rasteriza o capy_dl num grid de caracteres limitado (modo texto, fiel pois a geometria ja e em celulas: celula (x,y) -> coluna x / linha y): runs de TEXT posicionados por coluna, [img:alt] para IMAGE, lista numerada Links: para LINK, RECT/background omitidos no texto. Deterministico, allocation-free, fail-closed, freestanding (host-testavel + linkavel no ring-3). Seguranca: bytes de controle (<0x20/0x7F) do conteudo remoto nao-confiavel sanitizados para '?' (bloqueia injecao de escape de terminal); UTF-8 passa intacto; payload fora da arena fail-closed; content extent limitado (MAX_COLS=200/ROWS=2000). Satisfaz o criterio 6 da Etapa 7 (parser/layout/display-list substituiveis sem acoplar o core ao compositor). Host test tests/userland/test_browser_render.c (placement por coluna, determinismo, placeholder de imagem, sanitizacao de controle, fail-closed em NULL/versao/payload-fora-da-arena, clipping off-grid). Deteccao de sibling CAPYBROWSER_CORE_AVAILABLE (display_list.h) -> CFLAGS/includes em HOST_CFLAGS + TEST_SRCS; no-op em checkout CapyOS-only. Validado: make test verde; layout-audit limpo; version-audit verde. Sem mudanca de runtime/ABI; CapyBrowser v0.6.5 consumido so via headers (nenhum TU extra linkado ainda); 6 irmaos sem bump. Proximas fatias: 7.2 superficie grafica ring-3 + raster em pixels; 7.3 decode de imagem inline (CapyCodecs capy-codec-image v2); 7.4 pipeline real no app; 7.5 cache/cookies/forms; mantendo o modo texto como fallback. **Atualizacao alpha.289 (2026-06-17):** Slice 7.2 (nucleo) -- primeira superficie grafica ring-3 do CapyOS (6 syscalls SYS_WINDOW_CREATE..SYS_WINDOW_DESTROY, numeros 45..50; SYSCALL_COUNT 45->51; ABI ring-3 aditiva/append-only) com isolamento Opcao A (a janela e referenciada so por um handle opaco rastreado por pid; o app compoe pixels no proprio buffer e faz copy-in validado; ownership/bounds/overflow fail-closed) no modulo handler kernel/syscall_gfx.c + vtable injetavel struct syscall_gfx_ops (backend de compositor em kernel/syscall_gfx_init.c; testes de host injetam fake) + cleanup de janelas via observer de teardown em process_exit/process_destroy; mais o rasterizador CapyOS userland/bin/capybrowse/browser_render_pixel.{c,h} que pinta o MESMO capy_dl em ARGB32 (RECT cor CSS / TEXT font8x8 compartilhada / IMAGE placeholder / LINK underline + parser de cor). ABI compartilhada em include/kernel/syscall_gfx_abi.h + wrapper userland capy_gfx.h. Validado: make test verde (test_syscall_gfx 41/41, test_browser_render_pixel 22/22, test_capylibc_abi com os 6 numeros + SYSCALL_COUNT 51), make all64 verde (backend do compositor linka), layout-audit limpo. Handlers retornam -1 ate syscall_gfx_install_default_ops; a prova runtime ring-3 ponta-a-ponta em QEMU (app /bin/capygfx gated) e a Slice 7.2.2 (alpha.290); o desktop interativo coordenado com CapyUI e a 7.3. **Atualizacao alpha.290 (2026-06-17):** Slice 7.2.2 -- prova de runtime em QEMU da superficie grafica ring-3: novo app gated /bin/capygfx exercita os 6 syscalls (window create/fill/blit/present/poll) contra o compositor REAL (kernel_main inicializa o compositor sobre o framebuffer de boot + syscall_gfx_install_default_ops sob CAPYOS_GFX_SMOKE), sai 0 e o process_exit emite o marker COM1 [smoke] capygfx ready (observer de teardown destroi a janela). make smoke-x64-qemu-capygfx PASSOU (marker observado); make test verde (test_capygfx_smoke_gate 8/8); capygfx-elf linka; layout-audit limpo. Gate VMware smoke-x64-vmware-browser-graphical mapeado e pulado (sustentado por QEMU). Sem mudanca de ABI (a dos 6 syscalls e do alpha.289). **Atualizacao alpha.291 (2026-06-17):** Slice 7.3 (nucleo) -- pipeline real HTML->display-list. Novo adapter userland/bin/capybrowse/browser_pipeline.{c,h} (capyos_browser_build_display_list) dirige os 5 estagios puros do core do CapyBrowser (capy_html_parse->capy_css_parse->capy_css_cascade->capy_layout->capy_displaylist) produzindo o capy_dl a partir de HTML(+CSS); arenas ~1MiB no .bss, single-shot, fail-closed; pre-requisito do decode de imagem (nos IMAGE vem do pipeline), por isso movido a frente no master-plan. Provado em host por um binario de teste FOCADO (make test-browser-pipeline, sem capylibc-net p/ evitar a colisao do simbolo capy_url_parse): HTML->display-list (TEXT + LINK com URL resolvida absoluta) e HTML->pixels (rasteriza o display-list real via o backend da 7.2) -- 13/13. make test roda o agregado + o pipeline (condicional a CAPYBROWSER_CORE_AVAILABLE) -- ambos verdes; layout-audit limpo. Sem mudanca de runtime/ABI do kernel; o boot smoke ring-3 do pipeline (QEMU) e a alpha.292. **Atualizacao alpha.292 (2026-06-17):** Slice 7.3 (prova de runtime) -- prova HTML->pixels->janela EM RING-3 (QEMU). O app gated /bin/capygfx, quando o core grafico esta presente, renderiza uma pagina HTML embutida via o pipeline real (capyos_browser_build_display_list: HTML->DOM->CSS->cascade->layout->display-list) -> rasteriza com o backend da 7.2 -> blita pela ABI grafica -> present, contra o compositor REAL. Makefile compila os 11 TUs do core como objetos ring-3 (dir capybrowser-gfx/, SEM o rename de capy_url_parse pois o capygfx nao linka capylibc-net -> sem colisao) e os liga no capygfx (+ adapter + rasterizador + font + string.h) sob CAPYBROWSER_CORE_AVAILABLE; main.c com -DCAPYOS_HAVE_CAPYBROWSER_CORE (fallback de padrao auto-composto sem o core). Binario ring-3 com ~1.33 MiB de .bss; o smoke confirma empiricamente que o ELF loader carrega esse .bss e o pipeline executa em ring-3. Validado: capygfx-elf linka (sem colisao), make test verde (unit + pipeline 13/13), layout-audit limpo, make smoke-x64-qemu-capygfx PASSOU (marker [smoke] capygfx ready). Sem mudanca de ABI nem de TU do kernel. Gate VMware smoke-x64-vmware-browser-graphical mapeado e pulado (sustentado por QEMU). **Atualizacao alpha.293 (2026-06-17, Slice 7.4 nucleo host-provado; pareado com CapyBrowser 0.6.6):** decode de imagem inline -- a cadeia <img> -> pixels fecha no backend de render do CapyOS. CROSS-REPO: CapyBrowser 0.6.6 (liberado: develop+main, CI+Security verdes, tag v0.6.6) faz o no IMAGE do display-list carregar o src resolvido (em url_off/url_len, os mesmos campos do LINK; aditivo, CAPY_DL_VERSION inalterado). CapyOS: novo adapter userland/bin/capybrowse/browser_image.{c,h} sobre capy-codec-image v2 (injeta bump allocator de arena .bss 512 KiB + inflater zlib tinf; fail-closed; o CapyOS nunca decoda por conta propria) e o rasterizador browser_render_pixel.{c,h} ganha o callback resolve_image + blit escalado -- um no IMAGE com src resolvido e pixels decodificados e blitado escalado a caixa do no (images_decoded), com o placeholder bordado + label alt como fallback. Makefile detecta o core CapyCodecs e liga as 8 TUs de codec + 3 de tinf + browser_image.o no binario de teste focado (-DCAPYOS_HAVE_CAPYCODECS_IMAGE), sem colisao de simbolo. Validado: make test verde + test-browser-pipeline 19/19 (decode BMP->ARGB, PNG 2x2 via tinf->RGBW, bytes invalidos->fail-closed, rasteriza-com-imagem), layout-audit limpo, make all64 (clean) verde -> build/capyos64.bin; KERNEL byte-identico ao alpha.292. A prova de decode EM RING-3 (capygfx renderiza pagina com imagem embutida -> blit -> present em QEMU) segue na 7.4.2/alpha.294. Gate VMware smoke-x64-vmware-browser-graphical mapeado e pulado (sustentado por QEMU). CapyBrowser 0.6.5 -> 0.6.6; 5 repos irmaos inalterados. **Atualizacao alpha.294 (2026-06-17, Slice 7.4.2 -- prova de runtime em ring-3):** o app ring-3 capygfx, quando o core grafico do CapyBrowser E o core de codecs do CapyCodecs estao presentes, embute uma pagina com <img>, decoda um PNG 2x2 real EM RING-3 via o adapter browser_image (capy-codec-image v2 + inflater tinf) e o blita (escalado) no no IMAGE do display-list, contra o compositor real -- fechando o decode de imagem inline da Etapa 7. Makefile (bloco 7.4b) compila browser_image.c + 8 TUs de codec + 3 de tinf como objetos ring-3 (capybrowser-gfx/, CC64 freestanding, -DCAPYOS_HAVE_CAPYCODECS_IMAGE) e os liga no capygfx so quando ambos os cores estao presentes (aninhado sob CAPYBROWSER_CORE_AVAILABLE + CAPYCODECS_IMAGE_AVAILABLE); sem colisao de simbolo. O binario ring-3 tem ~1.83 MiB de .bss (pipeline ~1.09 MiB + surface ~0.30 MiB + arena de decode 512 KiB). O smoke FALHA FECHADO se images_decoded < 1 (o marker so dispara se o decode em ring-3 aconteceu). Validado: capygfx-elf linka limpo (sem colisao), make test verde (unit + pipeline 19/19), layout-audit limpo, make smoke-x64-qemu-capygfx PASSOU (marker [smoke] capygfx ready). Kernel byte-identico ao alpha.293; sem mudanca de ABI. CapyBrowser 0.6.6 + capy-codec-image v2 consumidos como-esta (TUs puros so no binario ring-3, nunca no kernel); 5 repos irmaos inalterados. Gate VMware smoke-x64-vmware-browser-graphical mapeado e pulado (sustentado por QEMU). **Atualizacao alpha.295 (2026-06-17, Slice 7.5 -- cache HTTP, nucleo host-provado):** abre a fatia 7.5 (cache/cookies/forms) com um cache de respostas HTTP limitado (subconjunto do RFC 7234) CapyOS-side -- novo modulo src/net/services/http/http_cache.{c,h} + include/net/http_cache.h (irmao do dns_cache; cache/cookies sao adapters do CapyOS, nao do core desacoplado do CapyBrowser). Puro, determinístico, fail-closed, com relogio INJETADO (now em segundos) e store FORNECIDO PELO CHAMADOR (struct http_cache, sem .bss no kernel ate alguem alocar), sobre struct http_request/http_response. Conservador (nunca serve stale incorretamente): so GET; status cacheavel 200/203/301/308/404; honra Cache-Control no-store (req+resp) e no-cache; nao cacheia respostas com Vary; frescor por max-age/Expires-Date; idade por RFC 7234 4.2.3; validadores ETag/Last-Modified -> revalidacao condicional (If-None-Match/If-Modified-Since) + refresh em 304; store LRU limitado (corpo acima do cap nao cacheado, fetch normal); + parser IMF-fixdate (host-testado). Provado por tests/net/test_http_cache.c (run_http_cache_tests, ~30 checagens em 10 grupos: data canonica 784111777, cacheabilidade, frescor, store/lookup FRESH/STALE, no-cache, idade RFC, headers condicionais, refresh 304, evicao LRU, oversize/NULL). http_cache.o adicionado aos objetos de net service do kernel (compila freestanding + linka no all64; dead code ate o fetch path ligar). Validado: make test verde (marker [http_cache] all cache tests passed; test-browser-pipeline 19/19), layout-audit limpo, make all64 verde -> build/capyos64.bin (+~4.7 KiB). Sem mudanca de ABI (modulo interno novo); 6 repos irmaos inalterados. A ligacao do cache ao fetch path (kernel http_get e/ou capylibc-net) + smoke QEMU de 2a-visita-servida-do-cache, cookies por dominio e formularios simples seguem nas proximas sub-fatias da 7.5. Gate VMware nao se aplica a este nucleo puro host-provado; o smoke de 2a visita sera mapeado quando o cache ligar ao runtime. **Atualizacao alpha.296 (2026-06-17, Slice 7.5 -- orquestracao de fetch do cache HTTP, nucleo host-provado):** estende o cache da alpha.295 com a camada de orquestracao do fluxo de fetch RFC 7234 (completa o cerebro do cache, ainda puro/desacoplado do transporte). A entrada do cache passa a guardar os headers de resposta + Location (serving fiel; Content-Type preservado). Novo http_cache_fetch(cache, req, resp, now, fetch_fn, ctx): FRESH -> serve do cache SEM fetch de transporte (2a visita rapida); STALE -> headers condicionais (If-None-Match/If-Modified-Since) + fetch, com 304 reusando o corpo cacheado e 200 substituindo; MISS -> fetch + store. O transporte e INJETADO por ponteiro de funcao (http_cache_fetch_fn), entao o fluxo e deterministicamente testavel em host SEM rede e desacoplado do cliente HTTP real (kernel http_request, transporte userland, ou fake). Provado por test_fetch_orchestration (estende run_http_cache_tests): MISS->fetched (1 chamada); 2a visita FRESH servida do cache SEM 2a chamada de transporte (prova de 'cache acelera a 2a visita') + status/corpo/Content-Type restaurados; STALE->304 reusando o corpo cacheado (nao o 304 vazio) + condicional na request; fresh de novo apos refresh do 304; STALE->200 substituindo pelo novo corpo; erro de transporte e fetch_fn NULL fail-closed (ERROR). Validado: make test verde (marker [http_cache] all cache tests passed; test-browser-pipeline 19/19), layout-audit limpo (http_cache.c ~540 LOC), make all64 verde (kernel +~128 B). Aditivo, sem mudanca de ABI; as primitivas da alpha.295 ficam inalteradas; o fluxo e dead code ate a integracao de transporte ligar (kernel http_request com cache estatico e/ou o navegador via capylibc-net), quando entra o smoke QEMU de 2a-visita-servida-do-cache. 6 repos irmaos inalterados. **Atualizacao alpha.297 (2026-06-17, Slice 7.5 -- cookies por dominio, nucleo host-provado):** continua a 7.5 com o jar de cookies por dominio (subconjunto do RFC 6265), CapyOS-side -- novo src/net/services/http/http_cookies.{c,h} + include/net/http_cookies.h (irmao do http_cache; cookies sao adapters do CapyOS). Puro, determinístico, fail-closed, com relogio INJETADO (now em segundos) e jar FORNECIDO PELO CHAMADOR (struct http_cookie_jar, sem .bss no kernel ate alguem alocar), sobre Set-Cookie em struct http_response + host/path da request. Conservador (nunca manda cookie pro site errado): parseia name=value + Domain/Path/Max-Age/Expires/Secure/HttpOnly (SameSite ignorado); Max-Age vence Expires; expiry ausente/inparseavel -> session; um Set-Cookie com Domain que NAO domain-matcheia o host e REJEITADO (anti-injecao, RFC 6265 5.3); sem Domain -> host-only (match exato); default-path derivado da request (5.1.4); expiry no passado ou Max-Age<=0 DELETA um match; no envio = domain-match + path-match + (Secure -> so https) + nao-expirado, ordenado por path mais longo primeiro (5.4); jar limitado com evicao do mais antigo. Reusa o parser IMF-fixdate compartilhado (http_cache_parse_date) para Expires. Provado por tests/net/test_http_cookies.c (run_http_cookies_tests, ~40 checagens em 10 grupos: parse/atributos/rejeicao cross-domain/match domain+path/envio+ordem/Secure/subdominio-vs-host-only/expiry+delete/update+evicao/NULL). http_cookies.o nos objetos de net service do kernel (linka no all64 resolvendo http_cache_parse_date; dead code ate o fetch path ligar). Validado: make test verde (markers [http_cookies] all cookie tests passed E [http_cache] all cache tests passed; test-browser-pipeline 19/19), layout-audit limpo (http_cookies.c ~460 LOC), make all64 verde -> build/capyos64.bin (+~8.7 KiB). Sem mudanca de ABI (modulo interno novo); 6 repos irmaos inalterados. Com o cache (295/296) e os cookies (297) host-provados, a proxima sub-fatia liga ambos ao fetch path real + smoke QEMU; depois os formularios simples fecham a 7.5. Gate VMware nao se aplica a este nucleo puro host-provado. **Atualizacao alpha.298 (2026-06-17, Slice 7.5 -- sessao de fetch: cache+cookies compostos, nucleo host-provado):** completa a camada PURA de fetch-policy da 7.5 compondo os dois adapters (http_cache + http_cookies) na unica cadeia de fetch que uma sessao de navegador usa, sem acopla-los entre si. Novo include/net/http_session.h + src/net/services/http/http_session.c: struct http_session { http_cache cache; http_cookie_jar jar } + http_session_fetch(s, req, resp, now, fetch_fn, ctx) que por fetch (1) anexa os cookies do jar que casam (host/path/scheme) como header Cookie na request, (2) roda o fluxo de cache RFC 7234 (hit FRESH servido SEM fetch de transporte, senao revalida/busca via a fn injetada), (3) colhe Set-Cookie de uma resposta de REDE (miss/refetch) de volta no jar. Puro, determinístico (relogio injetado), fail-closed; sessao FORNECIDA PELO CHAMADOR (sem .bss no kernel ate alguem alocar) + transporte INJETADO (http_cache_fetch_fn) -> cadeia testavel em host SEM rede. Provado por tests/net/test_http_session.c (run_http_session_tests: fluxo cache+cookie -- MISS /login seta SID + cacheia com max-age, 2a /login servida do cache SEM fetch, /app nao-cacheado buscado COM o cookie SID na request; gating Secure http omite/https envia; NULL session/fetch fail-closed). http_session.o nos objetos de net service do kernel (linka no all64 resolvendo http_cache_fetch + http_cookie_jar_*; dead code ate o fetch path ligar) + par de teste em TEST_SRCS + run_http_session_tests no test_runner. Validado: make test verde (markers [http_session] all session tests passed, [http_cookies] all cookie tests passed e [http_cache] all cache tests passed; test-browser-pipeline 19/19), layout-audit limpo (http_session.c ~67 LOC), make all64 verde -> build/capyos64.bin (+~176 B). Sem mudanca de ABI (modulo interno novo); 6 repos irmaos inalterados. Camada PURA de fetch-policy da 7.5 COMPLETA (cache 295/296 + cookies 297 + sessao 298 + forms ja no core do CapyBrowser capy_form_submit). Resta o binding de transporte real (estender capy_http_get do capylibc-net p/ headers de request -- Cookie + If-None-Match -- + um runtime de navegador multi-fetch/persistente) + smoke QEMU de 2a-visita-servida-do-cache; depois a 7.6 (streaming/limites/mixed-content). Gate VMware nao se aplica a este nucleo puro host-provado. **Atualizacao alpha.299 (2026-06-17, Slice 7.6 -- politica de seguranca de fetch, nucleo host-provado):** abre o endurecimento da 7.6 com a politica que um fetcher aplica ANTES de abrir conexao -- allow-list de scheme + navegacao HTTPS-first + bloqueio de mixed-content. Novo include/net/http_fetch_policy.h + src/net/services/http/http_fetch_policy.c, CapyOS-side (o core do navegador delega HTTPS-first/mixed-content ao adapter do host; irmao do http_cache/http_cookies/http_session). Puro, determinístico, fail-closed, SEM estado (cada decisao e funcao apenas do scheme classificado). Conservador (nega por padrao): scheme desconhecido -> BLOCK; sub-recurso http (inseguro) de pagina https (segura) -> BLOCK (mixed content, inclusive passivo); file:/ftp:/javascript: nunca buscaveis por conteudo web. API: http_fetch_classify_scheme (case-insensitive, sem ':' -> https/http/wss/ws/data/blob/about/file/ftp/javascript/other), http_fetch_scheme_is_secure (https/wss), http_fetch_policy_navigation (https ALLOW / http UPGRADE / data-about-blob ALLOW / file-ftp-ws-wss-javascript-other BLOCK), http_fetch_policy_subresource(page_secure, sub) (pagina segura so puxa sub-recurso seguro; data/blob/about inline ALLOW; file/ftp/javascript/other BLOCK; pagina insegura puxa http/https). Provado por tests/net/test_http_fetch_policy.c (run_http_fetch_policy_tests: classificacao incl. ci/near-miss/NULL; is_secure; navegacao; matriz de sub-recurso pagina-segura x insegura; caso critico 'pagina https bloqueia imagem http'). http_fetch_policy.o nos objetos de net service do kernel (linka no all64; dead code ate o fetch path ligar) + par de teste em TEST_SRCS + run_http_fetch_policy_tests no test_runner. Validado: make test verde (marker [http_fetch_policy] all fetch-policy tests passed; test-browser-pipeline 19/19), layout-audit limpo (http_fetch_policy.c ~114 LOC), make all64 verde -> build/capyos64.bin (+~400 B). Sem mudanca de ABI (modulo interno novo); 6 repos irmaos inalterados. E o gate que a busca de sub-recurso de rede (ex. as imagens da 7.4 quando vierem da rede) e a navegacao top-level aplicam antes de conectar; entra em uso quando o fetch ligar ao runtime. Restam na 7.6: tracker de orcamento de memoria/tempo por pagina, endurecimento da politica de certificado, streaming render, + o binding de transporte que liga as politicas de fetch (cache/cookies/sessao/seguranca) ao runtime do navegador. Gate VMware nao se aplica a este nucleo puro host-provado. **Atualizacao alpha.300 (2026-06-17, Slice 7.6 -- HSTS, nucleo host-provado):** continua o endurecimento da 7.6 com o store HSTS (subconjunto do RFC 6797), CapyOS-side (irmao do http_cache/http_cookies/http_session/http_fetch_policy). Novo include/net/http_hsts.h + src/net/services/http/http_hsts.c: puro, determinístico, fail-closed, relogio INJETADO (now) + store FORNECIDO PELO CHAMADOR (sem .bss no kernel ate alguem alocar). HSTS endurece a navegacao HTTPS-first da 299: um Strict-Transport-Security valido recebido SOBRE conexao segura torna https OBRIGATORIO para o host (e subdominios com includeSubDomains) ate expirar -- derrota SSL-stripping/downgrade. Conservador (RFC 6797): so honrado sobre conexao segura (ignorado sobre http, 8.1); max-age obrigatorio (sem ele e ignorado); max-age=0 REMOVE a politica; host IP-literal rejeitado (8.1.1); includeSubDomains estende aos subdominios. API: http_hsts_init / http_hsts_process_header(store,host,value,secure,now) / http_hsts_should_upgrade(store,host,now) / http_hsts_gc. Provado por tests/net/test_http_hsts.c (run_http_hsts_tests, 10 grupos: store exato vs subdominio; http ignorado; max-age ausente ignorado; max-age=0 remove + no-op em host desconhecido; includeSubDomains apex/sub/deep/suffix-sem-dot/prefix; IP rejeitado; expiry+gc; max-age citado+whitespace; evicao; NULL fail-closed). http_hsts.o nos objetos de net service do kernel (linka no all64; dead code ate o fetch path ligar) + par de teste em TEST_SRCS + run_http_hsts_tests no test_runner. Validado: make test verde (marker [http_hsts] all HSTS tests passed; test-browser-pipeline 19/19), layout-audit limpo (http_hsts.c ~229 LOC), make all64 verde -> build/capyos64.bin. Sem mudanca de ABI (modulo interno novo); 6 repos irmaos inalterados. Compoe com http_fetch_policy no call-site (HSTS consultado primeiro -> forca https; depois a politica de fetch decide ALLOW/BLOCK). Restam na 7.6: tracker de orcamento de memoria/tempo por pagina, endurecimento adicional da politica de certificado, streaming render, + o binding de transporte que liga toda a suite de politicas de fetch (cache/cookies/sessao/seguranca/HSTS) ao runtime de um navegador multi-fetch. Gate VMware nao se aplica a este nucleo puro host-provado. **Atualizacao alpha.301 (2026-06-26):** binding de transporte (capy_http_get_with_headers no capylibc-net) + runtime de navegador multi-fetch (browser_fetch) que torna VIVA a suite de fetch-policy (cache/cookies/session/HSTS/mixed-content, antes dead code): 2a-visita-servida-do-cache + cookies same-site + HSTS forca https + gate de mixed-content (imagem http em pagina https bloqueada), tudo host-provado; + page_budget (limites de memoria/tempo por pagina, Slice 7.6a/7.6b); + prova de link ring-3 capymultifetch-elf (pilha inteira freestanding CC64). Host de build = Windows+WSL2. make test / layout-audit / all64 / version-audit VERDES; capybrowse-elf + tls-smoke-elf + capymultifetch-elf linkam. Sem mudanca de ABI; 6 irmaos inalterados. Pendente p/ fechamento oficial da Etapa 7: smoke de runtime do multi-fetch (boot hook + servidor cacheavel controlado + smoke-x64-{qemu,vmware}-browser-multifetch), fetch de imagem pela rede em runtime, Slice B (navegador grafico com janela interativa no desktop CapyUI), e os gates de aceite VMware+UEFI+E1000 (passo do operador). **Atualizacao alpha.302 (2026-07-01):** o smoke de RUNTIME do browser-multifetch passou em QEMU real (smoke-marker-pattern: latch capymultifetch_smoke host-testado + boot hook + blob embutido sob CAPYOS_MULTIFETCH_SMOKE), transformando a prova de link ring-3 do alpha.301 numa prova de runtime completa -- o app /bin/capymultifetch busca uma URL cacheavel 2x pelo mesmo browser_fetch_ctx persistente sobre o transporte TLS/socket real e o kernel confirma via process_exit que a 2a visita foi servida do cache SEM 2a chamada de rede; validado por DOIS lados independentes (marcador do kernel + contagem de requisicoes do servidor host, exatamente 1). Descoberta arquitetural registrada para os proximos slices: capygfx nao linka capylibc-net (evita colisao de capy_url_parse com o core CapyBrowser renomeado), entao browser_fetch nao pode ser plugado direto nele -- buscar imagem pela rede no navegador grafico (Slice C runtime) ou a janela interativa (Slice B) exige resolver essa costura de simbolos primeiro. make test/layout-audit/all64(default e com flags de smoke)/iso-uefi/manifest64/version-audit VERDES. Sem mudanca de ABI; 6 irmaos inalterados. Gate VMware oficial smoke-x64-vmware-browser-multifetch definido e pronto, pendente do operador. **Atualizacao alpha.303 (2026-07-01):** resolvido o bloqueio arquitetural do alpha.302 -- CAPYBROWSER_PIPELINE_CFLAGS (capygfx ring-3, isolado de HOST_CFLAGS/browser_pipeline_tests) passou a aplicar o mesmo rename de capy_url_parse que o core de texto ja usa -- e, com isso, IMPLEMENTADO e PROVADO EM QEMU o fetch de imagem pela rede no navegador grafico atraves do gate de mixed-content (Slice C runtime): novo resolvedor cb_resolve_image_net (opt-in via CAPYGFX_NET_IMAGE_SMOKE, OFF por padrao) busca o <img src> via browser_fetch_get, passa pelo gate de mixed-content, decodifica com o mesmo adaptador CapyCodecs do smoke embutido. Smoke real QEMU com rede E1000/SLIRP passou; smoke alpha.294 (sem rede) re-executado do zero e continua passando (sem regressao). make test (19/19 browser_pipeline_tests sem regressao), layout-audit, capygfx-elf standalone, all64 default (capyos64.bin byte-identico, zero impacto em producao) e version-audit todos verdes. Sem mudanca de ABI; 6 irmaos inalterados. Gate VMware oficial smoke-x64-vmware-capygfx-net-image definido e pronto, pendente do operador. **Atualizacao alpha.304 (2026-07-01):** primeiros passos da integracao do navegador grafico com o desktop interativo (fora do boot-exclusivo): kernel_spawn_capygfx_desktop (spawn NAO-noreturn, arma main thread + scheduler_add, sem process_enter_user_mode) + syscall_gfx_install_default_ops chamavel sob demanda + capygfx embutido fora do gate de smoke (CAPYOS_DESKTOP_GRAPHICAL_BROWSER) + comando de shell open-browser-graphical. Novo smoke CAPYOS_DESKTOP_GRAPHICAL_BROWSER_SMOKE PROVOU em QEMU real que o spawn e de fato escalonado (espelha kernel_boot_run_two_busy_users: hello entra em ring 3, capygfx e enfileirado, yield cooperativo despacha capygfx quando hello sai). HONESTO sobre o gap restante (investigacao dedicada mapeou 5 subsistemas): isto prova o MECANISMO com um supervisor sintetico, NAO a sessao real desktop_runtime_start do CapyUI (task_create_kernel, ainda nao testado); e falta a PONTE DE INPUT (mouse/teclado -- a fila que SYS_WINDOW_POLL_EVENT dre na so recebe eventos de ciclo de vida de janela hoje, nao mouse/teclado do desktop real), que exige mudancas no repo irmao CapyUI fora do escopo desta sessao. make test/layout-audit/all64(smoke e default) verdes; capyos64.bin cresce ~48 bytes por design (comando incondicional, nao byte-identico desta vez); version-audit verde (current=0.8.0-alpha.304). Sem mudanca de ABI; nenhuma mudanca no CapyUI ainda. Gate VMware oficial smoke-x64-vmware-capygfx-desktop-spawn definido, pendente do operador. **Atualizacao alpha.305 (2026-07-01):** fechado o item 4 (ponte de input, o mais arriscado/sem precedente) dos 5 subsistemas do Slice B mapeados no alpha.304 -- struct gui_window ganha campo aditivo gfx_owner_pid (0=in-kernel, pid=ring-3); syscall_gfx_init.c marca a janela na criacao; novo gfx_owned_redirect() no topo de gui_window_dispatch_event (CapyUI/src/window/window_dispatcher.c) re-empurra mouse/teclado na fila para janelas ring-3 em vez do despacho on_mouse/on_key direto -- um unico choke point cobre os 5 call sites reais de input do desktop. Host-testado (7 asserções, incl. regressao de janela nao-owned) + confirmado sem regressao em runtime (os dois smokes QEMU do capygfx re-executados do zero, ambos passam). HONESTO: a integracao com o loop REAL desktop_runtime_start segue nao-testada em CI; e a governanca cross-repo (bump de versao do CapyUI 2.22.6 + docs/compatibility.md + matriz) ficou PENDENTE deliberadamente, para nao arriscar um bump incorreto no fim de sessao -- recomendado ao operador antes do fechamento oficial. make test/validate(CapyUI)/layout-audit/all64(default) verdes; version-audit verde (current=0.8.0-alpha.305). **Atualizacao alpha.306 (2026-07-01, correcao critica pos-teste VMware real):** kernel_spawn_capygfx_desktop nao protegia process_create/elf_load_into_process/scheduler_add contra o scheduler preemptivo -- todo outro chamador dessa sequencia e boot-exclusivo (roda antes do timer preemptivo ser armado), e sys_fork chega la via syscall trap gate (IF mascarado); esta foi a PRIMEIRA chamada rodando de uma sessao de desktop ja viva com interrupcoes habilitadas -- travou o sistema real em VMware (sem diagnostico, sem log serial disponivel). Corrigido com secao critica cli()/sti() (salva/restaura RFLAGS). Achado 100% por analise estatica (sem log de crash); make test verde, smoke de regressao do spawn sintetico passou, novo artefato gerado -- PENDENTE de reteste do operador em VMware para confirmacao definitiva. Um segundo travamento relatado (comando 'help' em boot limpo, sem relacao com o navegador grafico) NAO foi investigado nesta release -- registrado como problema separado, pre-existencia nao confirmada.
- **Slice 3D fechado em 2026-05-21 (alpha.245):** gate externo `make smoke-x64-vmware-usb-hid-keyboard` validado em VMware + UEFI + E1000 com teclado USB HID real, marker `[smoke] usb-hid-keyboard ready` observado no COM1, follow-ups §14.1-§14.3 entregues, audit fixes §15.1-§15.5 corrigidos e bug W (slot reuse collision) resolvido. 25 novos host tests cobrem smoke gate, event pump, release slot, port ack CSC, Ctrl combinations, LED dispatch e caps lock.
- **Slice 3E.1 entregue em 2026-05-21 (alpha.246):** extração host-testável dos AHCI/NVMe command builders.
- **Slice 3E.2.A entregue em 2026-05-21 (alpha.247):** unified block-I/O error classifier `block_io_classify_ahci`/`block_io_classify_nvme` com 5 classes. AHCI integrado em 3 sites de `ahci_exec`; NVMe em 4 sites. 15 novos host tests.
- **Slice 3E.2.B entregue em 2026-05-21 (alpha.248):** recoverable retry + reset escalation. `block_device_ops` ganha `read_block_ex`/`write_block_ex`/`reset` opcionais. Retry loop unificado aplica budget per-class. AHCI implementa COMRESET; NVMe implementa Controller Level Reset. 12 novos host tests.
- **Slice 3E.3 entregue em 2026-05-21 (alpha.249, escopo reduzido):** infraestrutura multi-slot AHCI via novo `ahci_slot_allocator`. NVMe queue depth 64 + CID rolling auditados. 11 novos host tests. Concurrent inflight real diferido para Slice 3F.
- **Slice 3E.4 entregue em 2026-05-21 (alpha.250):** storage stack smoke marker `[smoke] storage-stack ready`. 9 novos host tests. klog full migration deferida para sub-slice 3E.4.B.
- **Slice 3E.5 entregue em 2026-05-21 (alpha.251, scaffolding):** external validation gate `smoke-x64-vmware-storage-resilience` plumado.
- **Audit fix entregue em 2026-05-21 (alpha.252):** revisão crítica de Slices 3E.1–3E.5 identificou e corrigiu dois bugs críticos antes da execução externa: (1) double-emission do smoke marker em VMs dual-storage; (2) NVMe Controller Level Reset não reemitia Create I/O CQ/SQ após CC.EN=1. 4 novos host tests de regressão.
- **Sub-slice 3E.4.B entregue em 2026-05-21 (alpha.253):** migração mecânica de `dbg_puts`/`dbg_hex*`/`dbg_label_hex32` para `klog(KLOG_*, ...)` / `klog_hex(...)` em `ahci.c` e `nvme.c` (108 call sites em 2 arquivos). Helpers locais file-static removidos; output migra de port 0xE9 (QEMU-only) para o klog ring (recuperável em runtime). Como efeito colateral, **corrige bug latente**: 2 chamadas a `dbg_label_hex32` em `nvme_controller_reset` referenciavam o helper static de ahci.c (undefined-reference no escopo de TU). Outros 13 arquivos do projeto com ~126 sites `dbg_*` ficam como sub-slice 3E.4.C (follow-up).
- **Etapa 3 fechada formalmente em 2026-05-21 (alpha.253):** gate externo `make smoke-x64-vmware-storage-resilience` aprovado em VMware + UEFI + E1000 com marker `[smoke] storage-stack ready` no COM1. Encerrou os 8 sub-slices 3D + 3E.1-3E.5 + audit fix + 3E.4.B. Slices 3F-3J e sub-slices 3E.4.C/3E.5.B continuam como follow-ups não-bloqueantes.
- **Etapa 4 fechada em `alpha.262+20260602`:** Fase F validada externamente em VMware + UEFI + E1000 (`make smoke-x64-vmware-etapa-4`, 5 markers em ordem: DHCP → gui-session → scheduler-fairness → compositor-damage-track → thread-crash-survives) + batch de 5 fixes de hardening regressivo (ATA-PIO DF/ERR, fsck geometry overflow, compositor surface-dim cap, TLS free-wipe, memcpy/memset word-at-a-time). Detalhe por fase em [`active/etapa-4-closure-tracker.md`](active/etapa-4-closure-tracker.md).
- **Etapa 5 (TLS userland real) — progresso in-tree até o fecho:** **Slice 5.1 entregue in-tree** = syscall de entropia userland `SYS_GETRANDOM` (=42; `SYSCALL_COUNT`=43) backed pela CSPRNG do kernel (handler `sys_getrandom`, cap 256 B/chamada, fail-closed), stub `capy_getrandom` + decl capylibc + assert de ABI. `make test` verde (`SYS_GETRANDOM == 42 OK`, `SYSCALL_COUNT == 43 OK`), `layout-audit` sem warnings, `syscall.c` syntax OK; **TLS intocado** (`capy_tls_is_supported()` continua 0). **Slice 5.2 parcial in-tree** = include-path do BearSSL ligado (`USERLAND_CFLAGS` + `HOST_CFLAGS`) + host test validando os 146 trust anchors BearSSL reais com tipos reais host-side (`tls_trust_anchors OK (146 anchors)`); link ring-3 do subset pendente de `make all64`. **Engine smoke host-side entregue** = `tests/security/test_tls_client_engine.c` constrói um `br_ssl_client` real com os 146 trust anchors de produção e emite um ClientHello TLS válido com SNI (`tls_client_engine OK`), provando engine + anchors + config host-side **sem ligar o handshake** (userland segue fail-closed). **Slice 5.4 parcial** = handshake-drive loop real (`capy_tls_handshake.c`: engine BearSSL ↔ transport seam) implementado e host-testado (`tls_handshake_drive OK`: ClientHello + fail-closed em EOF/garbage/write-fail), **sem tocar o seam de produção** (`capy_tls_backend_connect` segue `EUNSUPPORTED`). **Validação de cert host-testada (5.5 parcial)** = `tls_cert_validation OK`: `br_x509_minimal` (o engine que o handshake arma) aceita cadeia válida p/ o host certo e **falha fechado** em hostname errado / expirado / issuer não-confiável (PKI de teste só-público; sem chaves privadas no tree). **Wall-clock para X.509 (Slice 5.x)** = `SYS_CLOCK_REALTIME=43` (`SYSCALL_COUNT`=44) via RTC do kernel + `capy_clock_realtime` + helper puro `capy_tls_unix_to_x509_time` (host-testado), fechando um gap real: `SYS_TIME` retornava ticks do APIC (não data), inútil p/ validade de cert. **Prerequisites da Etapa 5 todos prontos host-side** (entropia, anchors, engine, handshake-loop, validação de cert, wall-clock). **Handshake PLUGADO no backend de produção (gated)** = `capy_tls_backend_connect` executa o handshake real BearSSL (init_full + time + buffer + TLS1.2 + ALPN + seed + reset + `br_sslio_flush`), `capy_tls_connect_tcp` devolve o contexto vivo, send/recv/close via `br_sslio`, `capy_tls_is_supported()→1`, contexto zeroizado no release — tudo sob `CAPYOS_TLS_USERLAND_HANDSHAKE` (**default OFF**, default build/tests intactos; espelha o kernel `tls.c`). **Não validado aqui** (review/edit-only + sem cross-toolchain). Validação externa: `make all64 PROFILE=full CAPYOS_TLS_USERLAND_HANDSHAKE=1` + `make smoke-x64-vmware-tls-handshake`; depois promover o flag a default (5.6). Plano por slice em [`../architecture/etapa-5-tls-userland-readiness.md`](../architecture/etapa-5-tls-userland-readiness.md).
- **Atualização in-tree pós-`alpha.263` (validada externamente — `make test`, `layout-audit`, `make all64 iso-uefi` verdes; ainda não taggeada):** o backend BearSSL userland está **plugado (gated)** sob `CAPYOS_TLS_USERLAND_HANDSHAKE` (5.4) e o **caminho HTTPS de `capy_net` foi ligado de verdade (Slice 5.5)** — `capy_http_get` ganhou um seam de transporte (`struct capy_http_conn`) que, sob a flag, conecta via `capy_tls_connect_tcp` e usa `capy_tls_send/recv/close`; com a flag OFF segue fail-closed e o caminho HTTP fica byte-idêntico (coberto pelos testes host de `capy_net`). O **gate externo `make smoke-x64-vmware-tls-handshake` (Slice 5.6) está scaffoldado** (programa ring-3 `tls_smoke` + latch kernel host-testável que emite o marker no COM1 via exit-code, lançado pós-rede em `kernel_main`, tudo gated por `CAPYOS_TLS_HANDSHAKE_SMOKE`), com runbook em [`../operations/etapa-5-external-validation-playbook.md`](../operations/etapa-5-external-validation-playbook.md). Hardening host-testável somado: Host header omite a porta default (80/443); o fail-closed de `capy_tls_config_resolve` (não dá p/ desligar verify_peer) está travado por teste; e a robustez do parser DNS contra pacotes malformados (header truncado, label reservado/oversize, ponteiro de compressão truncado, RDLENGTH/ANCOUNT além do fim) está travada por testes. **Gate de fecho (executado externamente em alpha.264):** build flag-on (`make all64 PROFILE=full CAPYOS_TLS_USERLAND_HANDSHAKE=1 CAPYOS_TLS_HANDSHAKE_SMOKE=1`) + smoke VMware + `release-check` — todos aprovados; flag promovida a default (ver bullet de fecho abaixo).
- **Etapa 5 fechada em `alpha.264`:** `libcapy-tls` userland agora faz handshake BearSSL **real** (`capy_tls_is_supported()==1`) — entropia/wall-clock syscalls, trust anchors reais, ClientHello+SNI, handshake-drive, validação X.509 fail-closed e o seam HTTPS de `capy_net`. A flag `CAPYOS_TLS_USERLAND_HANDSHAKE` foi **promovida a default** após o gate externo (`make smoke-x64-vmware-tls-handshake`, marker `[smoke] tls-handshake ready` no COM1, + `release-check`). Hardening de segurança nesta janela: overflows de integer no ELF loader (userland + boot, corrigidos + testes), tetos de custo KDF no volume header, robustez adversarial de DNS/DHCP/ICMP/ARP e bound do `names_equal` do CAPYFS.
- **Etapas bloqueadas:** Etapas 7-16 dependem do fechamento integral da etapa anterior.

## Repositórios apartados (estado em alpha.308, Etapa 7 ativa)

Os contratos de integração cross-repo são autoritativos em
[`docs/reference/integration/`](../reference/integration/README.md). A
matriz pinada está em
[`compatibility-matrix.md`](../reference/integration/compatibility-matrix.md)
e o snapshot técnico atual está em
[`compatibility-audit-2026-06-11.md`](../reference/integration/compatibility-audit-2026-06-11.md).

| Repo apartado | Versão atual | Owner autoritativo | Gate de integração CapyOS |
|---|---|---|---|
| [`CapyUI`](../../../CapyUI) | `2.22.6` | widget model (`capy-ui-widget` v2.22, display-list schema v7) **e** desktop session (`capy-ui-desktop-session` v1, publicado em `alpha.241`) | Etapas 4 e 6 |
| [`CapyAgent`](../../../CapyAgent) | `0.0.8` | formato `.capypkg`, component-index, resolver, **signer Ed25519 publicado host-side** (`capy-agent-component-index` v1; verifier pendente de KAT externo + registro) | Etapas 8-9 |
| [`CapyBrowser`](../../../CapyBrowser) | `0.6.6` | browser-core text/HTML/CSS est?tico (`capy-browser-core`; pacote textual `org.capyos.browser.text` consumido pelo adapter CapyOS-side da 6.4 (app ring-3 `capybrowse`, build-validado; gate externo pendente); core gráfico ainda Etapa 7) | Etapas 6-7 |
| [`CapyCodecs`](../../../CapyCodecs) | `0.0.8` | image codecs portáveis (`capy-codec-image` v2: per-call limits, detect/generic, metadata, QOI) | Etapas 6-7 (imagem); Etapa 10 (áudio/vídeo) |
| [`CapyLang`](../../../CapyLang) | `0.1.9` | S1-S7 + S6.3 structs/enums entregues (host-only; `capy-lang-host` v0 parcial; +opcodes 0x64-0x66 + trap V0018) | Etapa 15 |
| [`CapyBenchmark`](../../../CapyBenchmark) | `0.0.9` | harness + baseline (`capy-benchmark-report` v1 planejado; +serialização report/eval/replay) | Etapas 15-16 |

Regras gerais (válidas mesmo antes da etapa abrir):

- Repositório externo **não conta como progresso oficial de etapa** até
  ser integrado por contrato versionado, adaptador CapyOS pequeno e gate
  externo aprovado.
- O adapter in-tree `services/capypkg` é a fronteira de recepção alpha
  para módulos remotos; signature verifier do `CapyAgent` permanece
  intencionalmente NULL até o signer Ed25519 ser publicado e plugado
  via `capypkg_set_signature_verifier`.
- Cada repo apartado mantém `docs/compatibility.md` próprio com a
  versão pinada do CapyOS, ABI declarada, limites e gate de integração.

## Higiene do core (concluída)

Snapshots seguros foram registrados em
[`external-core-repositories.md`](../reference/integration/external-core-repositories.md).
A higiene total do core foi concluída
([`core-migration-quarantine.md`](../reference/integration/core-migration-quarantine.md)):
os fontes e headers legados sem callers ativos foram **removidos do tree**
e o flag `CAPYOS_ENABLE_LEGACY_MIGRATED` foi aposentado. O adaptador
in-tree `services/capypkg` recebe pacotes Capy remotos via `capysh`.

Fluxo modular alpha: tags de release GitHub + sha256 + índice de ABI
mínima conforme
[`tag-release-component-index.md`](../reference/integration/tag-release-component-index.md);
assinatura e certificados ficam diferidos para hardening antes de
qualquer release oficial.

## Entrega antecipatória vigente: `services/capypkg` (alpha.239+)

Infra de recepção de pacotes Capy publicada in-tree em `services/capypkg`
(4 TUs runtime + 1 header público + 1 header interno, todas < 900 LOC),
com 9 comandos CLI tri-língua (`pkg-list`, `pkg-info`, `pkg-fetch`,
`pkg-install`, `pkg-remove`, `pkg-update`, `pkg-source-list`,
`pkg-source-add`, `pkg-source-remove`), supervisor de serviço
`SYSTEM_SERVICE_CAPYPKG` integrado ao target `FULL`, 28 testes host-side
passando (`make test-capypkg`) e trilha auditável via klog
(`[audit] [capypkg] …`) em todas as mutações de pacote/repo, com
variantes WARN distintas para falhas de digest/signature/dependency/
fetch/write/quota/persistence (forensicamente reconstruíveis).

Política de segurança documentada em
[`../architecture/capypkg-adapter.md`](../architecture/capypkg-adapter.md):
HTTPS-only no transporte, SHA-256 obrigatório, signature gate
fail-closed (Ed25519 só é aceito quando `CapyAgent` plugar o verificador
externamente), escopo de filesystem restrito a `/var/capypkg` ou `/opt/`,
e zero execução de payload pelo adapter.

**Não fecha a Etapa 9:** o gate oficial continua bloqueado por Etapas
3-8 conforme tabela vigente abaixo; este entregável apenas garante que,
quando a Etapa 9 abrir, a fronteira de recepção já estará verificada
e estável.

Extensões posteriores:

- `alpha.240` — install profile (`/system/install/profile.ini`), comando
  `pkg-bootstrap`, auto-bootstrap em kernel poll, `make package` em cada
  repo apartado e aggregator `make modules-index`.
- `alpha.241` — higienização end-to-end + wizard de primeiro boot
  interativo TUI (idioma, teclado, hostname, tema, splash, usuário,
  senha, **seleção de módulos**) + comando `capy` unificado +
  **migração da desktop session para `CapyUI`** (sources `gui/desktop/`,
  `gui/window/` e `apps/` agora têm o `CapyUI` como owner autoritativo;
  in-tree permanece como fallback de build) + activation gate em
  `kernel/module_gate.c` que consulta `/var/capypkg/<name>/installed`.
- `alpha.242` — hardening de redirect HTTP e staging persistente
  (`HTTP_MAX_URL`/`HTTP_MAX_PATH` para 2048; payloads rejeitados ficam
  em `/var/capypkg/updates` para diagnóstico).
- `alpha.243` — correção de HTTP redirect/bodyless no bootstrap remoto;
  validação real de ISO instalada com persistência.
- `alpha.244` — instalação remota completa de módulos via GitHub
  Release: download HTTPS de payload grande, staging dividido no CAPYFS,
  marker de ativação e smoke ISO com desktop ativado no reboot.
- `alpha.259` — Slices 1+2 da stack de compatibilidade Hyper-V
  (track laboratorial, **não** promove plataforma oficial): ATA-PIO
  promovido a backend nativo via novo `X64_STORAGE_BACKEND_ATA_PIO`
  (append-only), habilitando Hyper-V Generation 1 + IDE legado sem
  tocar NVMe/AHCI; boot policy troca fail-closed por fail-degraded com
  warning ruidoso quando storage persistente é indisponível, liberando
  wizard em RAM ao invés de aprisionar usuário em maintenance no
  Hyper-V Gen2. Slice 3 (StorVSC I/O wire-up para Gen2) detalhada
  em [`../architecture/hyperv-compatibility-stack-plan.md`](../architecture/hyperv-compatibility-stack-plan.md).
  Plataforma oficial VMware + UEFI + E1000 inalterada porque o probe
  ATA-PIO só executa quando NVMe **e** AHCI não promovem `block_device`.
- `alpha.260` — hardening + cleanup batch sobre alpha.259 (Sub-slice 3E.4.C
  klog migration, 3E.5.B `nvme_reset`, Etapa 4 Fases D+E smokes, P1 hardening);
  trabalho in-tree não taggeado isoladamente.
- `alpha.261` — **release atual (a taggear manualmente)**: corrige o
  provisionamento das pastas padrão do usuário no first-boot wizard.
  `src/config/first_boot/program.c` agora inclui `auth/user_home.h` e chama
  `user_home_prepare(admin_home, admin_uid, admin_gid)` no ponto em que o home
  do admin é confirmado, provisionando `Desktop`/`Documents`/`Personal`/
  `Professional` para o usuário primário (antes só `add-user` e o recovery
  chamavam, então o usuário de instalação ficava com home vazio e nada
  aparecia no desktop nem no file manager). Compliance cross-repo: CapyUI
  `2.13.1` → `2.19.0` (ABI `capy-ui-widget` v2.13 → v2.19; display-list schema
  7 inalterado) propagado na matriz/STATUS/pins/.windsurf; pins do core nos 6
  sisters movidos para `alpha.261`. Bundla o trabalho in-tree de alpha.259/260.
- `alpha.262` — **release atual (a taggear; lote coordenado de 7 repos)**:
  fecha a Etapa 4 via Fase F (validação externa em VMware + UEFI + E1000) +
  batch de 5 fixes de hardening regressivo (ATA-PIO DF/ERR fatal-status,
  fsck geometry overflow guard, compositor surface-dim cap, TLS free-wipe,
  memcpy/memset word-at-a-time). Compliance cross-repo (pivot da matriz):
  CapyUI `2.19.0 -> 2.22.0` (ABI `capy-ui-widget` v2.19 -> v2.22),
  CapyCodecs `0.0.6 -> 0.0.7` (`capy-codec-image` v1 -> v2), CapyLang
  `0.1.7 -> 0.1.8` (+opcodes 0x64-0x66 + V0018), CapyBrowser
  `0.0.6 -> 0.3.0`, CapyAgent `0.0.6 -> 0.0.7` (**Ed25519 signer publicado
  host-side**; verifier pendente de KAT externo + registro via
  `capypkg_set_signature_verifier`), CapyBenchmark `0.0.6 -> 0.0.7`; pins
  do core nos 6 sisters -> `alpha.262`; novo
  `compatibility-audit-2026-06-06.md`. Release note:
  `docs/releases/capyos-0.8.0-alpha.262+20260602.md`. Fase F continua sendo
  o gate de fechamento da Etapa 4 (a executar externamente).

## Hardening cross-module ativo

Gate de printable-ASCII propagado a todos os módulos que ecoam dados
externos via `shell_print` → `vga_write` → COM1:

- `src/services/update_agent_parse.c::parse_buffer_line` — manifests,
  `state.ini` e `repository.ini` que carreguem control bytes em
  qualquer value são silenciosamente descartados na ingestão (sem
  alterar contrato externo).
- `src/net/services/http/url_request_builder.c::http_parse_url` —
  fechado vetor de HTTP request smuggling: o parser rejeita
  0x00-0x20 e 0x7F antes de qualquer parsing.
- `src/net/services/http/prelude_headers_encoding.c::http_store_headers`
  — response headers de servidores hostis com bytes não-printáveis
  são substituídos por `?` em parse time, sem afetar
  Content-Length / chunked / Content-Encoding.

## Visão executiva das etapas concluídas

### Etapa 1 — CapyUI Shell Polish v1 (concluída em `alpha.100`)

Entregou o desktop visual familiar Ubuntu/Win7-like sem GPU 3D:
tema `classic-modern`, taskbar inferior com botão Capy + relógio,
launcher com busca textual/categorias/ações de sessão, decoração
de janelas com estados ativo/inativo/minimizar/maximizar/fechar,
wallpaper 2D + grid de ícones, toasts/notificações e system tray
NET/SND/SYS/USR.

**Owner autoritativo pós-alpha.241:** repositório [`CapyUI`](../../../CapyUI)
via módulo capypkg `org.capyos.ui.desktop-session` (compositor session,
window manager, apps). O CapyOS mantém in-tree um fallback de build em
`src/gui/desktop/`, `src/gui/window/` e `src/apps/` para sustentar o
caminho `make all64` quando o sibling `../CapyUI` não está presente,
mas o owner de feature é o repo `CapyUI` (versão `2.22.0`+ no pin
vigente da Etapa 4).

### Etapa 2 — Sessão gráfica operacional (concluída em `alpha.237`)

Sessão gráfica completa com login GUI real, dispatcher central de
input, frame pacing ocioso, fallback `CTRL+ALT+F1` para TTY, terminal
gráfico consumindo shell real e gates de evidência externa
`gui-session` + `mouse-events`. Em paralelo, segurança/auth/storage
para sessão persistente: header-managed encrypted volumes em produção
via `volume_provider`, migração legacy → header-managed transacional
com checkpoint persistente + rollback/abort/cleanup, login com
constant-time PBKDF2/Argon2id + lockout timing-equalised, CSPRNG
hardenado, fundação cripto canônica completa (SHA-256, SHA-512, HMAC,
PBKDF2, HKDF, CSPRNG, AES-XTS, ChaCha20-Poly1305 AEAD, X25519 ECDH,
Ed25519 signatures, Argon2id, BLAKE2b — 11 primitivas auditadas).

**Owner autoritativo pós-alpha.241:** a parte de desktop/window/apps
foi migrada para o repositório [`CapyUI`](../../../CapyUI) como
`org.capyos.ui.desktop-session`. A parte de auth, criptografia,
volume header, runtime de input, dispatcher e fallback textual
permanece no CapyOS core.

**Aceite externo:** em 2026-05-18 o operador informou execução
bem-sucedida fora desta máquina de `make test`, `make layout-audit`,
`make all64`, `make release-check`, `make smoke-x64-vmware-mouse-events`
e dos gates de readiness/evidência/aceitação/promoção com
`RELEASE_TAG=0.8.0-alpha.237+20260514` na plataforma oficial
VMware + UEFI + E1000.

**Runbook único para o operador externo / CI privada:**
[`docs/operations/etapa-2-external-validation-playbook.md`](../operations/etapa-2-external-validation-playbook.md)
orquestra build gates + provisionamento + smoke real + evidência/aceitação + promoção pública.

## Sequência ativa

> **Nota:** após a reordenação por ROI em 2026-05-15, a numeração das
> Etapas 3-16 mudou. A sequência antiga está em
> [`historical/capyos-master-plan-pre-roi-reorder.md`](historical/capyos-master-plan-pre-roi-reorder.md).

Resumo executivo vigente:

| Etapa | Tema | Status | Bloqueio / Repo apartado relacionado |
|---|---|---|---|
| 1 | CapyUI Shell Polish v1 | Concluída | owner pós-alpha.241: `CapyUI` |
| 2 | Sessão gráfica operacional | Concluída | desktop session: `CapyUI`; auth/crypto/runtime: core |
| 3 | Driver framework + entrada USB HID + storage estável | Concluída | fechada em alpha.253 (2026-05-21); follow-ups 3F-3J não-bloqueantes |
| 4 | CapyDisplay 2D + scheduler/multithread runtime | **Em andamento** | etapa atual; consome `capy-ui-widget` v2.22/schema v7 do sister repo `CapyUI` |
| 5 | TLS userland real | Bloqueada | depende da Etapa 4; sem repo apartado |
| 6 | Apps básicos do desktop maduros | Bloqueada | inclui integração de `CapyBrowser` (HTML-to-text core) e `CapyCodecs` (image) por contrato |
| 7 | Browser usável com web estática moderna | Bloqueada | integra `CapyBrowser` core + `CapyCodecs` imagem |
| 8 | Release/update gate oficial + instalador polido | Bloqueada | hardening do canal de release; sem repo apartado |
| 9 | Package manager + SDK + ABI estável | Bloqueada | integra `CapyAgent` (signer Ed25519 + component index oficial) |
| 10 | Áudio + multimídia básica | Bloqueada | integra `CapyCodecs` áudio por contrato |
| 11 | WiFi + power management + suspend/resume | Bloqueada | sem repo apartado |
| 12 | JS engine sandboxed | Bloqueada | engine pode ser apartada por contrato |
| 13 | CapyLX L0-L5 unificado | Bloqueada | sem repo apartado |
| 14 | Wayland bridge + apps Linux GUI | Bloqueada | sem repo apartado |
| 15 | Mesa/Vulkan path + CapyLang | Bloqueada | integra `CapyLang` (VM bytecode via host ABI versionada) |
| 16 | Plataforma 1.0 hardening | Bloqueada | inclui baseline regressivo de `CapyBenchmark` + compatibilidade oficial Hyper-V planejada |

---

## Etapa 3 (concluída em alpha.253) — resumo

Etapa fechou formalmente em 2026-05-21 (build `0.8.0-alpha.253+20260521`)
após aprovação externa do gate `make smoke-x64-vmware-storage-resilience`
em VMware + UEFI + E1000. Marker `[smoke] storage-stack ready` observado
no COM1 exatamente uma vez; regressão de Slice 3D (`[smoke] usb-hid-keyboard
ready`, aprovado em alpha.245) manteve-se verde.

Sub-slices entregues (alpha.245 → alpha.253):

- **3A-3D** — Device manager + XHCI + USB HID (boot protocol completo);
  gate `make smoke-x64-vmware-usb-hid-keyboard` aprovado em alpha.245.
- **3E.1** — AHCI/NVMe command builders host-testable (alpha.246).
- **3E.2.A** — Classifier de erro de bloco unificado em 5 classes (alpha.247).
- **3E.2.B** — Retry budget per-class + reset escalation
  (COMRESET para AHCI, Controller Level Reset para NVMe) (alpha.248).
- **3E.3** — AHCI slot allocator infraestrutura (alpha.249).
- **3E.4** — Storage smoke marker `[smoke] storage-stack ready` (alpha.250).
- **3E.5** — External validation gate scaffolding (alpha.251).
- **audit fix** — BUG #1 (smoke marker double-emission cross-driver)
  + BUG #2 (NVMe CLR missing queue recreation) (alpha.252).
- **3E.4.B** — Migração mecânica `dbg_*` → `klog`/`klog_hex` em ahci.c
  e nvme.c, com efeito colateral de fechar undefined-reference latente
  em `nvme_controller_reset` (alpha.253).

Detalhe técnico por sub-slice e implementação log em
[`../architecture/etapa-3-driver-foundation-plan.md`](../architecture/etapa-3-driver-foundation-plan.md)
e [`../architecture/etapa-3-slice-3e-plan.md`](../architecture/etapa-3-slice-3e-plan.md).

Runbooks operacionais do fechamento:

- [`../operations/etapa-3-external-validation-playbook.md`](../operations/etapa-3-external-validation-playbook.md) (Slice 3D).
- [`../operations/etapa-3-slice-3e-validation-playbook.md`](../operations/etapa-3-slice-3e-validation-playbook.md) (Slice 3E).

Documento arquitetural novo: `docs/architecture/smoke-marker-pattern.md`
canoniza o pattern de smoke markers (resultado do BUG #1 audit) para
prevenir reincidência em Etapas futuras.

**Follow-ups não-bloqueantes** (ficam como bug fixes oportunísticos):

- Slice 3F — Multi-table AHCI dispatch concorrente + remoção de spin-wait.
- Slice 3G — Política de fallback de driver no nível do kernel.
- Slice 3H — VirtIO-net/block prioritização VM.
- Slice 3I — VMware SVGA II como backend secundário.
- Slice 3J — USB mass storage.
- Sub-slice 3E.4.C — Migração `dbg_*` → `klog` nos arquivos restantes
  pós-3E.4.B (alpha.253). **Concluída em 2026-05-25** em dois batches:
  - **Batch LF:** `format_mount.c` (6 sites), `filesystem_helpers.c`
    (3), `public_mount_api.c` (6), `mount_initialize.c` (16) e
    `crypt_aes_xts.c` (35), totalizando ~66 call sites + remoção dos
    helpers locais `dbg_putc`/`dbg_puts`/`dbg_hex32` e variantes
    `_serial` em `capyfs_runtime_internal.h` e
    `kernel_volume_runtime_internal.h`. As utilidades puras
    `dbg_be32_local` e `crypt_be32` (renomeado a partir de
    `dbg_be32`) permanecem porque carregam u32 big-endian para as
    novas linhas `klog_hex`.
  - **Batch CRLF:** normalização in-place (`perl -i -pe 's/\r$//g'`)
    seguida de migração mecânica para `ramdisk.c` (9 sites),
    `buffer_cache.c` (13), `offset_wrapper.c` (15), `chunk_wrapper.c`
    (19), `efi_block.c` (21) e `storage_runtime.c` (15), totalizando
    ~92 call sites + remoção dos helpers locais homônimos. A
    utilidade pura `ramdisk_be32` (renomeada a partir do `dbg_be32`
    local) permanece pelo mesmo motivo do batch LF. Estes arquivos
    também migraram de CRLF para LF, alinhando com a convenção
    majoritária do tree.
  - **Não migrados (por design):** `kernel_main.c` mantém
    `dbg_hex64`/`dbg_hex8` no boot pulse anterior ao `klog_init`;
    `capyfs_dbg_puts`/`capyfs_dbg_hex32` em `capyfs_runtime_internal.h`
    + `format_mount.c`/`directory_entries.c`/`namespace_ops.c`
    permanecem porque já estão gated por `CAPYFS_DEBUG_CREATE=0`
    (no-op em builds de produção). Total migrado: ~158 sites em 11
    TUs + 2 headers limpos.
- Sub-slice 3E.5.B — Extração de `nvme_controller_reset` em passos
  puros para unit test do BUG #2 fix. **Concluída em 2026-05-25:**
  novo `include/drivers/nvme/nvme_reset.h` + `src/drivers/nvme/nvme_reset.c`
  expõem a lógica não-MMIO em quatro símbolos puros:
  `nvme_reset_reprime_queue_state` (zera heads/tails, seta phases=1),
  `nvme_reset_csts_rdy_cleared` / `nvme_reset_csts_rdy_set` (predicados
  contra o registrador CSTS) e o planner `nvme_reset_next_admin_action`
  que retorna `enum nvme_reset_admin_action` ordenado (CREATE_IO_CQ →
  CREATE_IO_SQ → DONE). `nvme_controller_reset` em
  `src/drivers/nvme/nvme.c` foi refatorado para dirigir o planner em
  loop em vez de chamar `nvme_create_io_cq`/`nvme_create_io_sq`
  diretamente em sequência rígida; isso preserva bit-for-bit o
  comportamento existente (incluindo o fix da BUG #2 do alpha.252) e
  trava a ordem CQ→SQ via teste host. Novo host test
  `tests/drivers/test_nvme_controller_reset.c` com 13 casos cobre:
  reprime (zera heads/tails, seta phases, NULL-safe), predicados CSTS
  (cleared/set + complementaridade XOR=1 em todas as entradas),
  planner (NULL → DONE, vazio → CREATE_IO_CQ, CQ feito → CREATE_IO_SQ,
  ambos → DONE, SQ-antes-de-CQ defensivo → CREATE_IO_CQ, sequência
  completa drive-2-then-DONE, valores estáveis do enum). Makefile
  wired (`nvme_reset.o` no `KERNEL_OBJS64`, TEST_SRCS atualizado);
  `tests/test_runner.c` registra `run_nvme_controller_reset_tests`.
  Zero cross-repo: tudo dentro do CapyOS core; ABI pública nova
  (`drivers/nvme/nvme_reset.h`) é aditiva.
  **Extension 2026-05-25:** novo predicate
  `nvme_reset_csts_fatal(csts)` testa CSTS.CFS (Controller Fatal
  Status, bit 1 do CSTS). `nvme_controller_reset` em
  `src/drivers/nvme/nvme.c` agora checa CFS em **quatro pontos**
  durante o reset path: dentro de cada um dos dois spin loops
  (stage 2 wait RDY=0 + stage 4 wait RDY=1) E após cada spin sair.
  Antes, se o controlador entrasse em CSTS.CFS durante reset, o
  host spinava todo o budget de 1M iterações antes de desistir —
  burning latency em hardware já wedged. Agora bail early com
  log forense `[nvme] reset CSTS.CFS during/after disable/enable`
  identificando exatamente qual stage falhou. Companheiro do
  early-exit já existente em `nvme_wait_ready` (linha 77-80) que
  faltava no caminho de reset. 5 testes host novos cobrem o
  predicate (csts=0 não fatal, CFS bit 1 fatal, independente de
  RDY, ignora bits espúrios, MSB sem CFS não é fatal).

### Driver safety audit 2026-05-25 — xHCI HSE early-exit

Auditoria de spin loops em todos os drivers procurando padrões
similares ao bug de CFS-missing do nvme reset. Bug real encontrado
em `src/drivers/usb/xhci.c`: o constante `XHCI_STS_HSE` (Host
System Error, bit 2 do USBSTS) estava declarado em
`include/drivers/usb/xhci.h` desde sempre mas **nunca usado**.
Quatro spin loops do controlador xHCI ignoravam HSE
silenciosamente:

- `xhci_reset` — loop "wait for HCHalted" (100k iter) + loop
  "wait for HCRST clear + CNR clear" (1M iter)
- `xhci_start` — loop "wait for running" (HCH clear, 100k iter)
- `xhci_stop` — loop "wait for halted" (HCH set, 100k iter)

Per xHCI 1.2 §5.4.2, USBSTS.HSE significa que "o xHC parou de
emitir TRBs e DMA transfers devido a erro interno sério". É o
equivalente direto do CSTS.CFS do NVMe. Adicionado HSE check no
topo de cada um dos 4 loops + logs forenses identificando qual
estágio falhou (`[xhci] USBSTS.HSE during stop/reset/start`).
Novos códigos de retorno -3/-4 para distinguir HSE de timeout
genérico; callers existentes (`xhci_start` chamado por
`usb_core.c`, `xhci_reset` chamado por `xhci_init`) usam `!= 0`
então tratam corretamente como falha. xhci.c também ganhou
`#include "kernel/log/klog.h"` (faltava antes; outros drivers já
usam). Zero novos host tests porque xhci_reset/start/stop são
MMIO-bound e exigem um xHC emulator para testar end-to-end; o
padrão é o mesmo do CFS predicate em nvme_reset (que SIM tem
testes host porque é função pura).

**Code review fix 2026-05-25 — HSE precedence:** revisão crítica
identificou que sites 1 (xhci_reset stop wait) e 4 (xhci_stop
standalone) usavam **HSE-first precedence** que era over-eager.
Per xHCI 1.2 §5.4.1, HCRST limpa HSE; logo, HSE pre-reset deveria
deixar o reset prosseguir, não bailar. E em xhci_stop, se HCH+HSE
ambos set, o intent do caller ("stop") já está satisfeito.
Refatorado: sites 1 e 4 checam HCH primeiro; HSE só dispara erro
se HCH for clear (anomalia). Sites 2 (xhci_reset post-HCRST wait)
e 3 (xhci_start) mantêm HSE-first porque post-reset HSE é fatal
e start não pode prosseguir com HSE set.

Outros drivers auditados sem encontrar bug:

- `ahci_port_wait_idle` — checar TFD.ERR é tentador mas semantica
  exata depende do controller; manter as-is para evitar regressão
  sutil. AHCI dispatch loop já usa `ahci_dispatch_classify_tick`
  (Slice 3F initial extraction) que cobre IS.TFES + TFD.ERR.
- `e1000`, `rtl8139`, `tulip` — descriptor-bound loops com per-
  descriptor error bits já checados (TX status DD + ES patterns).
- `vmbus_wait_message` — caller-controlled `timeout_loops` já
  bounded; companheiro do P1-C fix em `vmbus_transport_drain_simp`.
- `serial_com1` — short FIFO-empty wait, no fatal state aplica.
- `mouse_ps2_*` — short PS/2 protocol waits, no fatal state aplica.

### Driver safety audit 2026-05-29 — ATA-PIO DF/ERR fatal-status

Hardening regressivo (não-bloqueante) que **fecha um gap do audit de
2026-05-25**: `ata_pio.c` foi promovido a backend nativo só em alpha.259
(`X64_STORAGE_BACKEND_ATA_PIO`, Hyper-V Gen1) e não estava na lista de
drivers auditados acima.

**Bug encontrado (mesma classe do NVMe CSTS.CFS / xHCI USBSTS.HSE):**
`ata_wait_ready()` retornava sucesso assim que **BSY** limpava, **sem
inspecionar Device Fault (DF=0x20) nem ERR (0x01)**. Como
`ata_pio_write_sector_ctx()` chama `ata_wait_ready()` para "esperar o
device terminar a escrita", uma falha de hardware durante a escrita era
reportada como **sucesso silencioso** — risco de integridade de dados
(escrita/leitura aceita sobre device em falha). `ata_wait_drq()` checava
só ERR (não DF).

**Fix:** lógica de status extraída como predicados puros host-testáveis
em `include/drivers/storage/ata_status.h` + `src/drivers/storage/ata_status.c`
(`ata_status_is_fatal` = DF|ERR, `ata_status_busy`, `ata_status_drq_ready`),
seguindo o padrão de `nvme_reset.c` e `ahci_dispatch.c`. `ata_wait_ready`
agora bail fail-closed com log forense (`falha de dispositivo (DF/ERR)
apos BSY`) quando DF/ERR está setado após BSY limpar; `ata_wait_drq` usa
`ata_status_is_fatal` (agora cobre DF além de ERR). Bail antecipado
também evita queimar todo o budget `ATA_POLL_MAX` (2M) sobre hardware
travado (ganho de performance, mesma motivação do fix NVMe CFS). As
macros `ATA_STATUS_*` migraram para o header (single source of truth);
`ata_pio.c` normalizado de CRLF→LF (convenção majoritária do tree).

**Testes:** `tests/drivers/test_ata_status.c` (11 casos) trava o contrato
fatal/busy/drq (DF, ERR, DF|ERR, DRDY|ERR, bits benignos CORR/IDX/DSC,
0xFF). `run_ata_status_tests` registrado no `tests/test_runner.c`.

**Validação local:** `make test` verde (`[tests] ata_status OK`,
"Todos os testes passaram"); `make layout-audit` sem warnings;
`ata_status.c` compila standalone. `ata_pio.c` exige o cross-toolchain
x86_64 (ausente nesta máquina) — **gate externo recomendado: `make all64`**
(regra 30 para mudança de driver/storage), e regressão
`make smoke-x64-vmware-storage-resilience` em VMware oficial.

### Filesystem safety audit 2026-05-29 — fsck superblock geometry

Hardening regressivo (não-bloqueante) de memory-safety em
`src/fs/fsck/fsck.c`, que lê o superblock CAPYFS de uma imagem
**não-confiável** (validar essa imagem é, literalmente, o trabalho de um
checker) e então deriva tamanhos de bitmap com aritmética **uint32**:
`inode_bytes=(inode_count+7)/8`, `block_bytes=(block_count+7)/8`,
`imap_blocks=(inode_bytes+bs-1)/bs` (aloca `imap_blocks*bs`) e
`inode_block_count=(inode_count+ipb-1)/ipb`.

**Bug:** um superblock com `inode_count`/`block_count` próximo de
UINT32_MAX faz a soma `(+7)`/`(+ipb-1)`/`(*bs)` dar **wrap**, produzindo
alocação **subdimensionada** que o walk de inodes/blocos depois escreve
além do fim — **heap overflow controlado por metadados on-disk
hostis**. Domínio de alto risco (rule 20: metadados CAPYFS), sem
validação de geometria antes da alocação. `fsck_repair` sequer checava
o magic antes de reconstruir bitmaps.

**Fix (fail-closed, padrão de predicado puro host-testável):** novo
`include/fs/fsck_geometry.h` + `src/fs/fsck/fsck_geometry.c` com
`fsck_super_geometry_valid(sb, dev_block_count, dev_block_size)`, que
**replica em uint64** toda a aritmética uint32 do fsck e rejeita (retorna
0) quando: `block_size` do superblock difere do device; counts são zero
ou excedem a capacidade física (`block_count<=dev_block_count`,
`inode_count<=block_count*inodes_per_block`); offsets de layout caem fora
do device; ou qualquer tamanho derivado faria wrap de uint32 / não
caberia no device. `fsck_check` e `fsck_repair` chamam o validador logo
após ler o superblock e abortam com `FSCK_ERR_BAD_SUPERBLOCK` / retorno
-1 antes de qualquer `kmalloc`. O validador é uint64-puro (não pode ele
mesmo dar overflow).

**Testes:** `tests/fs/test_fsck_geometry.c` (11 casos) cobre baseline
válido, NULL, device zero, block_size pequeno/divergente, counts zero,
count > capacidade, offsets fora do device, região de bitmap
extrapolando o device e os casos de overflow `inode_count`/`block_count`
≈ UINT32_MAX. `run_fsck_geometry_tests` registrado no `tests/test_runner.c`.

**Validação local:** `make test` verde (`[tests] fsck_geometry OK`,
"Todos os testes passaram"); `make layout-audit` sem warnings; `fsck.c`
e `fsck_geometry.c` passam `gcc -fsyntax-only` (sem inline asm x86).
Gate externo recomendado: `make all64` + `make test` em CI.

### Compositor safety audit 2026-05-29 — surface dimension overflow

Hardening regressivo (não-bloqueante) em `src/gui/core/compositor.c`.
`alloc_surface(w, h)` computava `pixels = (size_t)w*(size_t)h` e alocava
`pixels * sizeof(uint32_t)` **sem limite superior** de dimensão.
`compositor_create_window`/resize passam `w`/`h` direto; um caller com
dimensões absurdas (~2^31 por lado, ex.: app bugado/hostil) faria
`pixels*4` dar **wrap de size_t** → buffer subdimensionado tratado depois
como `w*h` pixels (escrita out-of-bounds). Severidade baixa (exige
dimensões absurdas em 64-bit) mas é um integer-overflow→OOB latente no
único choke point de alocação de surface.

**Fix:** nova constante `COMPOSITOR_MAX_SURFACE_DIM` (32768, folga acima
de qualquer painel real e bem abaixo do ponto de wrap) em
`include/gui/compositor.h`; `alloc_surface` rejeita fail-closed
(`return NULL`, já tratado por ambos os callers) quando `w`/`h`
excedem o limite. Guard inline (2 linhas) — proporcional à lógica
trivial, diferente dos predicados puros extraídos para fsck/ata; os
host tests existentes do compositor (`test_compositor_events`,
`test_compositor_smoke_gate`, `test_widget_damage`, `test_overlay_damage`)
cobrem create/resize. `make test` verde, `make layout-audit` sem warnings.

### Audit de parsers de input não-confiável 2026-05-29 — sem alteração

Varredura com **atenção extra a drivers e Hyper-V** (host→guest e input
remoto) confirmou que os caminhos de parse de maior risco **já estão
robustamente hardened e cobertos por host tests** — nenhuma alteração
justificada (mexer em código testado e correto só adicionaria risco):

- **DNS** (`src/net/services/dns.c`): `skip_name` limita label a 63,
  cap de 8 saltos de ponteiro de compressão e `(pos+1+count) > len`;
  `net_dns_parse_first_a` e o parser de SOA negativo checam
  `offset+4/+10/+rdlen` e confinam o skip de nomes do RDATA a
  `rdata_start+rdlen` antes de ler MINIMUM.
- **RNDIS** (`src/drivers/net/rndis.c`): todo parser valida
  `len >= sizeof(msg)` e `msg->len <= len`; `rndis_parse_query_complete`
  calcula `payload_offset` em `size_t` e checa `payload_offset > len` e
  `info_len > (len - payload_offset)` antes de expor payload.
- **VMBus ring** (`src/drivers/hyperv/vmbus_ring.c`):
  `vmbus_read_raw_packet_runtime` clampa `packet_len` a
  `available - trailer` e recusa `packet_len > buffer_size` (sem cópia);
  `vmbus_packet_extract_payload` clampa `declared_len`/`offset` e deriva
  `data_len = declared_len - offset` dentro do pacote.
- **netvsc** (`src/drivers/net/netvsc.c`): consome só os payloads já
  validados do RNDIS e checa `payload_len` antes de ler (ex.: `< 6` p/ MAC).
- **HTTP** (`src/net/services/http/`): corpo limitado a
  `HTTP_MAX_RESPONSE_SIZE` (`RESPONSE_TOO_LARGE`); cópias usam
  `body_received`/`buffer_size` reais, não o `Content-Length` do servidor
  (que só dirige detecção de fim); chunked decode é bounded.
- **storvsc** (`src/drivers/storage/storvsc_scsi.c`): build de CDB com
  `cdb_len > STORVSC_SCSI_CDB_MAX` e `alloc_len > 0xFF`, sobre a extração
  de pacote VMBus já validada.

### Secret-zeroization audit 2026-05-29 — TLS free-wipe

Hardening regressivo (não-bloqueante) de higiene de segredos (rule 20:
"wipe sensitive buffers using the project volatile-safe wipe pattern").
Auditoria dos sites que liberam material secreto:

- **Bug em `src/security/tls.c`:** (1) `tls_memzero` usava ponteiro
  **não-volátil** (`uint8_t *`), que o compilador pode eliminar; (2)
  `tls_free` fazia `kfree` do `iobuf` (plaintext TLS + registros de
  handshake) e do `ctx` (que embute `br_ssl_client_context`/`x509` com
  chaves de sessão + master secret) **sem zerar antes** — segredos de
  TLS ficavam residentes na heap liberada a cada fecho de conexão HTTPS
  (usado por capypkg, update-agent, `net-fetch`).
- **Fix:** `tls_memzero` agora é volatile-safe (`volatile uint8_t *`,
  mesmo padrão de `crypt_secure_clear`/`vp_wipe`); `tls_free` zera
  `iobuf` (`BR_SSL_BUFSIZE_BIDI`) e o `ctx` inteiro (`sizeof(*ctx)`)
  antes dos respectivos `kfree`. Mínimo e contido; preserva o
  comportamento (só acrescenta o wipe no teardown).

**Sites auditados e já corretos (sem alteração):**

- `src/security/volume_provider.c` — política "Wipe hygiene" documentada
  + `vp_wipe` volatile-safe em **todo** caminho de saída (salt, key1,
  key2, tag, header_buf, hdr).
- `src/security/crypt*.c` — `crypt_secure_clear` volatile-safe usado
  pervasivamente (round keys, tweak, k_pad, hashes; wipe-before-free do
  `crypt_device`).
- `src/auth/login_runtime/credential_buffer.c` — wipe canônico
  volatile-safe (`login_window_credential_buffer_wipe`) com
  `password_wipe_required`/`wiped` enforced pela política.

**Validação local:** `tls.c` passa `gcc -fsyntax-only` (com
`-Ithird_party/bearssl/inc`); `make test` verde (tls.c é kernel-side,
não está na suíte host — gate externo `make all64`); `make layout-audit`
sem warnings.

### Performance audit 2026-05-29 — word-at-a-time memcpy/memset

Primeira fatia de performance da varredura (segurança + performance). O
`memcpy`/`memset` freestanding do kernel em `src/arch/x86_64/stubs.c`
(o próprio comentário os chamava de "Temporary stubs ... until proper
implementations are added") eram **loops byte-a-byte** — e o compilador
rebaixa cópias de struct, init de array e zeramento grande **kernel-wide**
para esses símbolos (page tables, buffer cache, framebuffer, pacotes de
rede, buffers de cripto). Byte-a-byte é ~8x mais iterações de load/store
que word-a-word no bulk alinhado.

**Otimização (comportamento idêntico):** novos cores `static inline`
`capy_word_memset`/`capy_word_memcpy` em `include/util/string_ops.h`,
word-at-a-time (8 bytes) com **prólogo byte para alinhar a 8 bytes**
(sem acesso desalinhado, sem UB de aliasing/alinhamento); `memcpy` usa o
caminho word só quando origem e destino compartilham alinhamento mod 8
(caso comum: buffers page/struct/block-alinhados) e cai para bytes caso
contrário. `memcpy`/`memset` em `stubs.c` passam a chamar os cores
(inlinados → zero overhead de chamada). `memmove`/`memcmp` permanecem
byte-a-byte (correção de overlap/sinal, fora de hot path).

**Equivalência provada por host test:** `tests/util/test_string_ops.c`
compara os cores contra uma referência byte-a-byte para **todo** tamanho
(0..256), todos os offsets de alinhamento de destino (0-7) e, no memcpy,
todas as combinações de alinhamento origem×destino (8×8), verificando
também bytes-guarda fora do range (sem escrita OOB). `run_string_ops_tests`
registrado no `tests/test_runner.c`.

**Validação local:** `make test` verde (`[tests] string_ops OK`,
"Todos os testes passaram"); `make layout-audit` sem warnings; `stubs.c`
e o teste passam `gcc -fsyntax-only`; `stubs.c` normalizado CRLF→LF.
Ganho de throughput real (não medível nesta workspace) deve ser
confirmado em `make all64` + smoke/benchmark externo.

### Hardening da revisão regressiva (não-bloqueante)

A auditoria informal de 2026-05-24 identificou alguns pontos
P1/P2 de hygiene/safety. Fatia entregue em 2026-05-25:

- **P1-A — Hyper-V storage retry:** novo work item
  `SYSTEM_WORK_STORAGE_HYPERV_RETRY` em `include/core/work_queue.h`
  + `src/core/work_queue.c` + handler em
  `src/arch/x86_64/kernel_services_work.c::kernel_work_storage_hyperv_retry`
  com self-disable em non-Hyper-V e backoff exponencial.
- **P1-E — `mouse_ps2_init` hardcoded 800×600 bounds:** removido o
  par de atribuições estáticas que sobrescrevia qualquer
  `mouse_set_bounds(W, H)` chamado antes da probe PS/2. O default
  800×600 já é aplicado lazily por `mouse_ensure_bounds()` no
  primeiro `mouse_apply_event`, então o caminho de boot continua
  seguro mas os bounds reais do framebuffer agora sobrevivem ao
  init do PS/2.
- **P1-F — USB poll defense-in-depth:** adicionado guard
  `i < USB_MAX_DEVICES` ao loop de `usb_poll_all()` em
  `src/drivers/usb/usb_core.c`. `g_device_count` é normalmente
  cappado por `usb_enumerate_devices`, mas a redundância
  garante terminação mesmo se um write stray no contador
  empurrar o valor para além do tamanho do array.
- **P1-C — `vmbus_transport_drain_simp` unbounded loop:** loop
  reescrito para (a) continuar apenas em retorno POSITIVO de
  `vmbus_consume_simp_slot` (matches a semântica documentada
  "bytes consumed"; 0=nada pra consumir, negativo=erro com slot já
  tratado) e (b) cap de 256 iterações com log de excedência.
  Antes o `while (consume() != 0)` poderia pinar o caller
  indefinidamente se `g_simp_page` fosse desanexada mid-drain
  (consume retornaria -1 a cada chamada). 256 é folgado para
  qualquer burst real do SIMP (16 slots × handful de retries) mas
  pequeno o suficiente para causar stall visível se atingido.

**P1-B (login_runtime polls 12×)** investigado e classificado como
**design intencional**, não bug: as 12 chamadas a
`login_service_poll(ops)` em `src/auth/login_runtime.c` são pontos
de drain em transições significativas do loop interativo (após
prompt, após dispatch, após cada short-circuit de comando interno).
Consolidação para um único ponto de drain afetaria latência sem
ganho mensurável; mantido como-is.

**P1-D (`vmbus_mouse` experimental loop)** permanece gated atrás
de `CAPYOS_EXPERIMENTAL_HYPERV_MOUSE` (default OFF). Sem código
ativo em produção, sem prioridade.

Pendências da revisão regressiva: todos os P1 endereçados ou
classificados.

### Slice 3F (initial extraction, 2026-05-25)

Fatia inicial de Slice 3F entregue: extração da lógica pura do
dispatch loop do AHCI seguindo o padrão de 3E.5.B (nvme_reset).
Novo `include/drivers/storage/ahci_dispatch.h` +
`src/drivers/storage/ahci_dispatch.c` expõem **cinco** símbolos
puros que serão o building block do futuro dispatch multi-slot:

- `ahci_dispatch_classify_tick(ci, is, tfd, slot_bit)` retorna
  `enum ahci_dispatch_observation` com três valores estáveis
  (INFLIGHT=0, COMPLETED=1, ABORTED=2). Trava a precedência usada
  em `ahci_exec_classified`: CI cleared > IS.TFES > TFD.ERR.
- `ahci_dispatch_completed_slots(prev_ci, cur_ci, inflight_mask)`
  retorna o bitmask de slots que transitaram de inflight para
  completed entre duas amostras CI, filtrado pelo
  `inflight_mask` autoritativo do host (defesa contra clears
  espúrios em paths de reset).
- `ahci_dispatch_inflight_count(inflight_mask)` — popcount via
  Brian Kernighan (sem builtins, compila identicamente em
  clang/gcc + kernel/host).
- `ahci_dispatch_can_admit(inflight_mask, concurrent_limit)` —
  gate de admissão com cap configurável; sentinel
  `concurrent_limit=0` significa "no limit" (delegate ao
  allocator); `>0` retorna 1 sse `popcount(inflight) < limit`.
  Útil para backpressure tuning (e.g. throttle controlador
  lento para 4 inflights mesmo com NCS=32) e para host tests
  exercitar branch "all-busy" deterministicamente.
- `ahci_dispatch_first_slot(mask)` — retorna lowest-set-bit
  index 0..31 ou -1 se mask==0; usado para drenar completions
  em ordem determinística (lowest-first matches AHCI 1.3.1
  §5.3.5 recommendation).

`src/drivers/storage/ahci.c::ahci_exec_classified` refatorado
para chamar o tick classifier em vez de duplicar a lógica de
precedência inline. Comportamento observável preservado
bit-for-bit; o downstream `block_io_classify_ahci` continua sendo
chamado para mapear (IS, TFD, timed_out, port_present) ->
`enum block_io_error_class`. Os helpers de fan-in/popcount/
admission/first-slot ainda não têm caller live — são scaffolding
para a próxima fatia 3F (provisioning de N cmd_tables +
IRQ-driven completion). Novo `tests/drivers/test_ahci_dispatch.c`
com **31 casos** cobre: tick classifier (precedência COMPLETED >
ABORTED > INFLIGHT, isolamento de slot_bit, valores estáveis do
enum, bits espúrios de IS/TFD), completion fan-in (filtragem de
inflight_mask, partial completion, multi-slot transitions,
freshly-dispatched slots não contam como completed), inflight
count (zero, single LSB/MSB, full, sparse), admission gate
(no-limit sentinel, below/at/above/serialized), first-slot picker
(zero→-1, sweep todas 32 posições, lowest-bit-wins, MSB-only).
Makefile wired (`ahci_dispatch.o` adjacente a `ahci_commands.o`
em `KERNEL_OBJS64`; TEST_SRCS atualizado); `tests/test_runner.c`
registra `run_ahci_dispatch_tests`. Zero cross-repo: tudo
interno; ABI pública nova (`drivers/storage/ahci_dispatch.h`) é
aditiva. Os demais entregáveis de Slice 3F (IRQ-driven completion
+ remoção do spin-wait + dispatch de múltiplos slots simultâneos
+ N cmd_tables) permanecem pendentes — esta fatia entrega
**toda a lógica pura** que o futuro multi-slot dispatch precisará,
travada por testes host antes mesmo do live driver ser
modificado.

**Extension 2026-05-25 — ahci_slot_inflight_mask:** novo accessor
`ahci_slot_inflight_mask(const struct ahci_slot_allocator *alloc)`
em `include/drivers/storage/ahci_slot_allocator.h` retorna o
bitmask dos slots inflight (bit `i` set sse slot `i` foi
allocado e não released). A invariante "bits acima de slot_count
sempre zero" é garantida pela máscara explícita
`((1u << slot_count) - 1u)` (com special case para slot_count=32
para evitar UB de shift). O resultado é safe para alimentar
direto `ahci_dispatch_completed_slots(prev_ci, cur_ci,
inflight_mask)` sem masking adicional, fechando a ponte entre o
allocator e os helpers de Slice 3F. `ahci_slot_inflight_count`
refatorado para reusar o novo accessor (DRY: um único lugar
implementa a máscara, popcount lê dela). 9 testes host novos em
`tests/drivers/test_ahci_slot_allocator.c` cobrem: mask vazia
após init, NULL-safe, allocator sem config, single alloc, múltiplos
allocs, post-release, bits acima de slot_count zero, full 32
slots, cross-check popcount(mask) == count.

### Gate agregado Etapa 4 (alpha.260)

Novo target Makefile `smoke-x64-vmware-etapa-4` consolida a Fase F
externa em uma única VM boot. Faz `make clean` + `make all64
PROFILE=full EXTRA_CFLAGS64='-DCAPYOS_SCHEDULER_FAIRNESS_SMOKE
-DCAPYOS_THREAD_CRASH_SURVIVES_SMOKE'` + `iso-uefi` + `manifest64`
e roda `tools/scripts/smoke_x64_vmware.py` com cinco markers em
ordem estrita (DHCP -> gui-session -> scheduler-fairness ->
compositor-damage-track -> thread-crash-survives). Os targets
per-Fase (`smoke-x64-vmware-scheduler-fairness`,
`smoke-x64-vmware-compositor-damage-track`,
`smoke-x64-vmware-thread-crash-survives`) permanecem para triagem
isolada quando algum marker falha. Documentação completa em
§5.6 do `docs/operations/etapa-4-external-validation-playbook.md`.

## Etapa 4 (concluída em alpha.262) — detalhes operacionais

A etapa entregou CapyDisplay 2D + scheduler/multithread runtime e
**abriu o primeiro gate cross-repo com sister** depois do fechamento da
Etapa 3: o contrato real `capy-ui-widget` v2.22 / display-list schema v7
com o repo `CapyUI`.

**Runbook autoritativo:**
[`../operations/etapa-4-external-validation-playbook.md`](../operations/etapa-4-external-validation-playbook.md).

**Fases planejadas** (extraídas do runbook):

| Fase | Sub-gate | Owner |
|---|---|---|
| A | Adapter CapyOS-side consumindo `capy-ui-widget` v2.22/schema v7 | CapyOS core |
| B | Integração visual do produtor real CapyUI com o adapter | CapyOS core + CapyUI |
| C | Scheduler cooperativo + multithread runtime + smoke `scheduler-fairness` | CapyOS core |
| D | Damage tracking + double buffering + smoke `compositor-damage-track` | CapyOS core |
| E | Política de panic/oops para thread de app + smoke `thread-crash-survives` | CapyOS core |
| F | Aprovação externa final + fechamento da Etapa 4 | operador |

**Fase A revertida em alpha.255 (2026-05-21):** o scaffolding entregue
em alpha.254 era um contrato paralelo + incompatível com a ABI real
do sister `CapyUI`.

**Fase A corrigida (alpha.255+):** a matriz foi sincronizada para
`CapyUI` `2.19.0` / `capy-ui-widget` v2.19 e o core ganhou
`include/gui/capyui_display_adapter.h` +
`src/gui/widgets/capyui_display_adapter.c`, que consome
`CapyUI/src/widget/capy_display_list.h` quando o sibling existe via
Makefile sibling detection. O adapter renderiza o subconjunto 2D
básico e ignora/falha de forma segura para ops que exigem providers
dedicados.

**Estado por fase (alpha.260+, fonte única de detalhe:
[`active/etapa-4-closure-tracker.md`](active/etapa-4-closure-tracker.md)):**
Fase A (adapter) ✅ código + host tests; Fase B (produtor real CapyUI)
🟡 capability entregue e exercitada — fluxos core (Terminal, Context
menu, Inline prompt) + fluxos do sibling via `capy_widget_emit`
(Calculator, Text Editor, Settings, File Manager, Task Manager, Taskbar,
Notification, Desktop icons); a migração dos demais fluxos de produção é
polish **não-bloqueante** porque o critério de capability já está
atendido; Fases C (scheduler cooperativo), D (damage tracking + double
buffering) e E (thread-crash survives) ✅ código + host tests, cada uma
com seu latch de smoke.

**Único bloqueador para fechar a Etapa 4:** a **Fase F** — validação
externa em VMware oficial (`make smoke-x64-vmware-etapa-4` + regressões
da Etapa 3 + `release-check`). Essa validação **não roda nesta
workspace** (review/edit only).

**Critérios de aceite (implementados em código + host tests; permanecem
`[ ]` até a confirmação externa da Fase F; rastreabilidade critério →
fase → evidência → gate em
[`active/etapa-4-closure-tracker.md`](active/etapa-4-closure-tracker.md) §3):**

- [ ] Compositor redesenha somente regiões danificadas quando possível.
- [ ] Cursor e texto não piscam sob resize/move de janela.
- [ ] Fallback framebuffer continua funcionando.
- [ ] Apps single-threaded existentes continuam funcionais como regressão.
- [ ] Thread de app crashando não derruba kernel nem desktop.
- [ ] Widget model desacoplado consegue renderizar display list por
      adaptador CapyOS sem acessar compositor diretamente.

**Cross-repo handshake esperado:** qualquer nova alteração no layout
do display-list ou no pin do sister deve passar pelo workflow
`cross-repo-contract-sync`; o consumo atual é do header real v2.22
(schema v7 inalterado) já publicado no sibling `../CapyUI`.

## Bloqueio das etapas 5-16

Todas dependem do fechamento integral da etapa anterior conforme
[`active/capyos-master-plan.md`](active/capyos-master-plan.md). Repositórios
apartados podem evoluir em paralelo (CapyUI já entregou v2.22.0 com
desktop session e widget/display-list schema v7; CapyLang já entregou S1-S7 host-only (lexer/parser/diagnostics/bytecode v0/VM/host bridge); demais permanecem em
ABI host-only ou planejada) — mas só contam como progresso oficial
quando a etapa correspondente abrir e o adapter + gate externo aceitarem
a integração.

Novas regressões em código já entregue são bugs da etapa ativa
correspondente, salvo mudança explícita deste plano.
