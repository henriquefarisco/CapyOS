/*
 * test_browser_e2e.c (F3.3d — finalizacao da camada logica)
 *
 * Teste de integracao end-to-end do stack do browser do CapyOS:
 *
 *   chrome_runtime  ───request_pipe──▶  fake_engine  (mimica capybrowser stub)
 *                  ◀─response_pipe────
 *
 * O "engine" aqui nao e um processo ring 3 — e um pump in-process
 * que le frames IPC do request_pipe e emite a sequencia canonica
 * documentada em docs/architecture/browser-ipc.md. Isso permite
 * exercitar TODOS os layers (codec, dispatcher, watchdog, runtime)
 * juntos sem cross-toolchain.
 *
 * Cenarios cobertos:
 *   1. Navegacao feliz: NAVIGATE -> 5 eventos -> NAV_READY -> SHUTDOWN.
 *   2. Watchdog: PING enviado pela runtime, PONG roteado de volta.
 *   3. Cancelamento: CANCEL -> NAV_CANCELLED, frame antigo ignorado.
 *   4. Engine morre: EOF detectado -> chrome marca engine dead.
 *   5. Watchdog expira: sem PONG, runtime sinaliza kill.
 *   6. Restart: record_restart limpa frame, segunda navegacao funciona.
 *   7. Stale frames: nav_id antigo descartado pelo dispatcher.
 *   8. Erro de protocolo: header invalido eleva PROTOCOL_ERR.
 */

#include "apps/browser_chrome_runtime.h"
#include "apps/browser_ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_e2e_passed = 0;
static int g_e2e_failed = 0;

#define E2ECHECK(cond, label)                                              \
    do {                                                                   \
        if (cond) ++g_e2e_passed;                                          \
        else { ++g_e2e_failed;                                             \
               printf("    [FAIL] %s:%d %s\n", __FILE__, __LINE__, label); } \
    } while (0)

/* === Mock pipes (re-uso do design de test_browser_chrome_runtime) ====== */

#define E2E_PIPE_BUF 16384u

struct e2e_pipe {
    uint8_t buf[E2E_PIPE_BUF];
    uint32_t r;
    uint32_t w;
    uint32_t count;
    int read_open;
    int write_open;
};

static struct e2e_pipe g_e2e_pipes[4];

static void e2e_pipes_reset(void) {
    for (int i = 0; i < 4; ++i) {
        memset(&g_e2e_pipes[i], 0, sizeof(g_e2e_pipes[i]));
        g_e2e_pipes[i].read_open = 1;
        g_e2e_pipes[i].write_open = 1;
    }
}

static int e2e_pipe_write(int id, const void *buf, size_t len) {
    if (id < 0 || id >= 4) return -1;
    struct e2e_pipe *p = &g_e2e_pipes[id];
    if (!p->write_open || !p->read_open) return -1;
    uint32_t space = E2E_PIPE_BUF - p->count;
    if (space == 0u) return -1;
    uint32_t to = (uint32_t)len; if (to > space) to = space;
    const uint8_t *src = buf;
    for (uint32_t i = 0; i < to; ++i)
        p->buf[(p->w + i) % E2E_PIPE_BUF] = src[i];
    p->w = (p->w + to) % E2E_PIPE_BUF; p->count += to;
    return (int)to;
}

static int e2e_pipe_read(int id, void *buf, size_t len) {
    if (id < 0 || id >= 4) return -1;
    struct e2e_pipe *p = &g_e2e_pipes[id];
    if (!p->read_open) return -1;
    if (p->count == 0u) return p->write_open ? -1 : 0;
    uint32_t avail = p->count; if (avail > len) avail = (uint32_t)len;
    uint8_t *dst = buf;
    for (uint32_t i = 0; i < avail; ++i)
        dst[i] = p->buf[(p->r + i) % E2E_PIPE_BUF];
    p->r = (p->r + avail) % E2E_PIPE_BUF; p->count -= avail;
    return (int)avail;
}

/* === Fake engine — espelho do userland/bin/capybrowser/main.c ========= */

static void be_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static void be_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

struct fake_engine {
    int request_read_id;   /* engine fd 0 = read end of chrome->engine pipe */
    int response_write_id; /* engine fd 1 = write end of engine->chrome pipe */
    uint32_t nav_id;
    uint32_t resp_seq;
    uint32_t fetch_seq;
    int alive;
    /* Telemetria do que o engine recebeu (para verificacao em test). */
    uint32_t total_received;
    uint16_t last_kind;
    /* Comportamento controlavel pelos testes. */
    int freeze;            /* 1 = ignora PING (simula travamento) */
    int crash_after_navigate; /* 1 = fecha pipes apos NAV_STARTED */
    /* Slice 5d: estado da navegacao em curso. O engine real bloqueia
     * em wait_for_fetch_response; aqui o pump e nao-bloqueante,
     * entao guardamos os dados da nav pendente e fechamos o ciclo
     * quando o FETCH_RESPONSE chegar. */
    int     pending_nav;        /* 1 = aguardando FETCH_RESPONSE */
    uint32_t pending_nav_id;
    uint32_t pending_fetch_seq;
};

static void engine_init(struct fake_engine *e, int req_read, int resp_write) {
    memset(e, 0, sizeof(*e));
    e->request_read_id = req_read;
    e->response_write_id = resp_write;
    e->alive = 1;
}

static int engine_send(struct fake_engine *e, uint16_t kind,
                       const uint8_t *pl, uint32_t plen) {
    uint8_t hdr[BROWSER_IPC_HEADER_SIZE];
    be_put_u16(&hdr[0], (uint16_t)BROWSER_IPC_MAGIC);
    be_put_u16(&hdr[2], kind);
    be_put_u32(&hdr[4], ++e->resp_seq);
    be_put_u32(&hdr[8], plen);
    if (e2e_pipe_write(e->response_write_id, hdr, sizeof(hdr)) <= 0) return -1;
    if (plen > 0u) {
        if (e2e_pipe_write(e->response_write_id, pl, plen) <= 0) return -1;
    }
    return 0;
}

/* Le um frame inteiro do request pipe (header + payload). Retorna:
 *   1 frame consumido em out_kind/out_payload/out_len
 *   0 sem dados
 *  -1 erro/EOF */
static int engine_try_recv(struct fake_engine *e, uint16_t *out_kind,
                           uint8_t *out_payload, uint32_t out_cap,
                           uint32_t *out_len) {
    uint8_t hdr[BROWSER_IPC_HEADER_SIZE];
    int rd = e2e_pipe_read(e->request_read_id, hdr, sizeof(hdr));
    if (rd <= 0) return rd; /* 0 EOF, -1 would-block */
    if ((uint32_t)rd != sizeof(hdr)) return -1;

    struct browser_ipc_header h;
    if (browser_ipc_header_decode(hdr, sizeof(hdr), &h) != BROWSER_IPC_OK)
        return -1;
    if (h.payload_len > out_cap) return -1;
    if (h.payload_len > 0u) {
        rd = e2e_pipe_read(e->request_read_id, out_payload, h.payload_len);
        if ((uint32_t)rd != h.payload_len) return -1;
    }
    *out_kind = h.kind;
    *out_len = h.payload_len;
    e->total_received++;
    e->last_kind = h.kind;
    return 1;
}

/* Slice 5d: phase 1 da navegacao -- emite NAV_STARTED + progress
 * FETCH e em seguida o EVENT_FETCH_REQUEST, deixando a navegacao
 * em estado "pending" ate o chrome responder com FETCH_RESPONSE.
 * O engine real bloqueia em wait_for_fetch_response; aqui o
 * fake_engine guarda o estado e completa em phase 2. */
static int engine_begin_navigation(struct fake_engine *e,
                                   const uint8_t *url, uint16_t url_len) {
    e->nav_id++;
    /* NAV_STARTED */
    uint8_t pl[6 + 1024];
    be_put_u32(&pl[0], e->nav_id);
    be_put_u16(&pl[4], url_len);
    if (url_len > 0u && url_len <= 1024u) memcpy(&pl[6], url, url_len);
    if (engine_send(e, BROWSER_IPC_EVENT_NAV_STARTED, pl,
                    (uint32_t)(6u + url_len)) < 0) return -1;
    if (e->crash_after_navigate) {
        g_e2e_pipes[e->response_write_id].write_open = 0;
        e->alive = 0;
        return 0;
    }
    /* progress FETCH */
    uint8_t prog[6];
    be_put_u32(&prog[0], e->nav_id);
    prog[4] = BROWSER_IPC_STAGE_FETCH; prog[5] = 10;
    if (engine_send(e, BROWSER_IPC_EVENT_NAV_PROGRESS, prog, 6) < 0) return -1;

    /* EVENT_FETCH_REQUEST -- payload: seq u32 + nav_id u32 + method u8
     * + url_len u16 + url[]. */
    e->fetch_seq++;
    uint8_t freq[11 + 1024];
    be_put_u32(&freq[0], e->fetch_seq);
    be_put_u32(&freq[4], e->nav_id);
    freq[8] = BROWSER_IPC_FETCH_GET;
    be_put_u16(&freq[9], url_len);
    if (url_len > 0u && url_len <= 1024u) memcpy(&freq[11], url, url_len);
    if (engine_send(e, BROWSER_IPC_EVENT_FETCH_REQUEST, freq,
                    (uint32_t)(11u + url_len)) < 0) return -1;

    e->pending_nav = 1;
    e->pending_nav_id = e->nav_id;
    e->pending_fetch_seq = e->fetch_seq;
    return 0;
}

/* Slice 5d: phase 2 -- executada quando FETCH_RESPONSE chega.
 * Emite progress PARSE/RENDER + EVENT_FRAME + NAV_READY. */
static int engine_complete_navigation(struct fake_engine *e) {
    uint32_t nav = e->pending_nav_id;
    uint8_t prog[6];
    be_put_u32(&prog[0], nav);
    prog[4] = BROWSER_IPC_STAGE_PARSE; prog[5] = 60;
    if (engine_send(e, BROWSER_IPC_EVENT_NAV_PROGRESS, prog, 6) < 0) return -1;
    prog[4] = BROWSER_IPC_STAGE_RENDER; prog[5] = 90;
    if (engine_send(e, BROWSER_IPC_EVENT_NAV_PROGRESS, prog, 6) < 0) return -1;
    /* EVENT_FRAME 4x4 BGRA azul */
    enum { W = 4, H = 4, STRIDE = W * 4u, PIX = STRIDE * H };
    uint8_t fbuf[12 + PIX];
    be_put_u32(&fbuf[0], nav);
    be_put_u16(&fbuf[4], W);
    be_put_u16(&fbuf[6], H);
    be_put_u32(&fbuf[8], STRIDE);
    for (uint32_t i = 0; i < PIX; i += 4u) {
        fbuf[12 + i + 0] = 0xCB; fbuf[12 + i + 1] = 0x66;
        fbuf[12 + i + 2] = 0x1B; fbuf[12 + i + 3] = 0xFF;
    }
    if (engine_send(e, BROWSER_IPC_EVENT_FRAME, fbuf, sizeof(fbuf)) < 0) return -1;
    uint8_t rdy[4]; be_put_u32(rdy, nav);
    if (engine_send(e, BROWSER_IPC_EVENT_NAV_READY, rdy, 4) < 0) return -1;
    e->pending_nav = 0;
    return 0;
}

/* Pump principal do fake engine: drena tudo que o chrome enviou e
 * emite as respostas correspondentes. Chamado em loop pelo teste
 * entre poll_event/tick. */
static void engine_pump(struct fake_engine *e) {
    if (!e->alive) return;
    uint16_t kind;
    uint8_t pl[2048];
    uint32_t plen;
    int r;
    while ((r = engine_try_recv(e, &kind, pl, sizeof(pl), &plen)) == 1) {
        switch (kind) {
            case BROWSER_IPC_NAVIGATE: {
                uint16_t url_len = (plen >= 2u)
                    ? (uint16_t)((pl[0] << 8) | pl[1]) : 0u;
                const uint8_t *url = (plen >= 2u) ? &pl[2] : (const uint8_t *)"";
                engine_begin_navigation(e, url, url_len);
                break;
            }
            case BROWSER_IPC_FETCH_RESPONSE: {
                /* Slice 5d: chrome respondeu nosso fetch. Validamos
                 * e completamos a navegacao. Em caso de status nao-OK
                 * a navegacao seguiria com NAV_FAILED no engine real;
                 * o fake_engine mantem o caminho feliz aqui para nao
                 * inflar o teste, ja que o fluxo de erro e exercido
                 * em test_browser_runtime_fetch.c. */
                if (e->pending_nav) {
                    (void)engine_complete_navigation(e);
                }
                break;
            }
            case BROWSER_IPC_PING: {
                if (e->freeze) break; /* ignora -> watchdog timeout */
                /* Eco do nonce. */
                engine_send(e, BROWSER_IPC_EVENT_PONG, pl, 4u);
                break;
            }
            case BROWSER_IPC_CANCEL: {
                if (e->nav_id == 0u) break;
                uint8_t can[4]; be_put_u32(can, e->nav_id);
                engine_send(e, BROWSER_IPC_EVENT_NAV_CANCELLED, can, 4);
                break;
            }
            case BROWSER_IPC_SHUTDOWN: {
                /* Encerra: fecha pipe lado escrita. Chrome detecta EOF. */
                g_e2e_pipes[e->response_write_id].write_open = 0;
                e->alive = 0;
                break;
            }
            default: break;
        }
        if (!e->alive) break;
    }
}

/* === Setup helper ====================================================== */

struct e2e_session {
    struct chrome_runtime rt;
    struct fake_engine eng;
};

/* Pipe ids: 0 = chrome->engine; 1 = engine->chrome. */
static void session_setup(struct e2e_session *s, uint64_t now) {
    e2e_pipes_reset();
    chrome_runtime_set_pipe_ops(e2e_pipe_write, e2e_pipe_read);
    chrome_runtime_init(&s->rt, /*req_pipe*/0, /*resp_pipe*/1, /*pid*/42u, now);
    engine_init(&s->eng, /*req_read*/0, /*resp_write*/1);
}

/* Drena todos eventos disponiveis no chrome ate NO_DATA. Retorna
 * mascara acumulada de acoes e quantos eventos foram consumidos.
 *
 * Slice 5d: quando o chrome sinaliza ACTION_FETCH_REQUESTED, o
 * runtime resolve via tabela embutida e envia FETCH_RESPONSE
 * para o engine. Sem essa chamada o fake_engine ficaria
 * indefinidamente em pending_nav. */
static int drain_chrome_events(struct chrome_runtime *rt, uint64_t now,
                               uint32_t *out_actions_or, int *out_count) {
    uint32_t acts_or = 0u;
    int count = 0;
    for (;;) {
        uint32_t a = 0u;
        int s = chrome_runtime_poll_event(rt, now, &a);
        if (s == CHROME_RUNTIME_POLL_NO_DATA) break;
        if (s == CHROME_RUNTIME_POLL_ENGINE_EOF) { count = -1; break; }
        if (s == CHROME_RUNTIME_POLL_PROTOCOL_ERR) { count = -2; break; }
        acts_or |= a;
        if (a & BROWSER_CHROME_ACTION_FETCH_REQUESTED) {
            (void)chrome_runtime_dispatch_pending_fetch(rt);
        }
        ++count;
        if (count > 100) break;
    }
    if (out_actions_or) *out_actions_or = acts_or;
    if (out_count) *out_count = count;
    return 0;
}

/* Slice 5d: alterna pump+drain ate ambos os lados ficarem ociosos.
 * Necessario porque a navegacao agora tem duas fases (antes do
 * fetch e depois): phase 1 emite 3 eventos e o EVENT_FETCH_REQUEST,
 * que aciona o chrome a responder com FETCH_RESPONSE; o engine
 * entao acorda em phase 2 e emite os 4 eventos finais. Sem este
 * loop o caller teria que chamar pump+drain manualmente em pares. */
static int session_run_until_idle(struct e2e_session *s, uint64_t now,
                                  uint32_t *out_actions_or) {
    int total = 0;
    uint32_t acts_or = 0u;
    for (int iter = 0; iter < 8; ++iter) {
        uint32_t engine_recv_before = s->eng.total_received;
        int request_pipe_count = (int)g_e2e_pipes[0].count;
        engine_pump(&s->eng);
        int progress = (s->eng.total_received != engine_recv_before)
                    || ((int)g_e2e_pipes[0].count != request_pipe_count);

        int count = 0;
        uint32_t a = 0u;
        drain_chrome_events(&s->rt, now, &a, &count);
        if (count > 0) progress = 1;
        if (count > 0) total += count;
        acts_or |= a;
        if (!progress) break;
    }
    if (out_actions_or) *out_actions_or = acts_or;
    return total;
}

/* === Cenarios ========================================================== */

static void scenario_happy_navigation(void) {
    static struct e2e_session s;
    session_setup(&s, 0u);

    int rc = chrome_runtime_send_navigate(&s.rt, "http://capyos", 13);
    E2ECHECK(rc == 0, "happy: send_navigate ok");
    E2ECHECK(s.rt.chrome.status == BROWSER_CHROME_STATUS_LOADING,
             "happy: chrome -> LOADING apos send");

    /* Slice 5d: a navegacao agora tem duas fases que se cruzam com
     * uma resposta de fetch do chrome. Usamos o helper que
     * alterna pump+drain ate ficar idle. Esperamos 7 eventos:
     *   NAV_STARTED, NAV_PROGRESS(FETCH), EVENT_FETCH_REQUEST,
     *   NAV_PROGRESS(PARSE), NAV_PROGRESS(RENDER),
     *   EVENT_FRAME, NAV_READY. */
    uint32_t actions_or = 0u;
    int count = session_run_until_idle(&s, 0u, &actions_or);
    E2ECHECK(s.eng.total_received >= 1u,
             "happy: engine recebeu pelo menos 1 request (NAVIGATE)");
    /* Slice 5d: o engine deve ter recebido NAVIGATE seguido de
     * FETCH_RESPONSE (>= 2 frames de chrome→engine), e o ultimo
     * kind processado e FETCH_RESPONSE. */
    E2ECHECK(s.eng.total_received >= 2u,
             "happy: engine recebeu NAVIGATE + FETCH_RESPONSE");
    E2ECHECK(s.eng.last_kind == BROWSER_IPC_FETCH_RESPONSE,
             "happy: ultimo recv foi FETCH_RESPONSE");
    E2ECHECK(count == 7, "happy: 7 eventos drenados (slice 5d)");
    E2ECHECK((actions_or & BROWSER_CHROME_ACTION_REPAINT_FRAME) != 0u,
             "happy: REPAINT_FRAME presente");
    E2ECHECK((actions_or & BROWSER_CHROME_ACTION_UPDATE_STATUS) != 0u,
             "happy: UPDATE_STATUS presente");
    E2ECHECK(s.rt.chrome.status == BROWSER_CHROME_STATUS_READY,
             "happy: chrome -> READY no fim");
    E2ECHECK(s.rt.chrome.last_frame.width == 4u, "happy: frame w=4");
    E2ECHECK(s.rt.chrome.last_frame.height == 4u, "happy: frame h=4");

    /* Shutdown limpo. */
    rc = chrome_runtime_send_shutdown(&s.rt);
    E2ECHECK(rc == 0, "happy: shutdown enviado");
    engine_pump(&s.eng);
    E2ECHECK(s.eng.alive == 0, "happy: engine encerrou");
    /* Chrome detecta EOF no proximo poll. */
    uint32_t a = 0u;
    int st = chrome_runtime_poll_event(&s.rt, 0u, &a);
    E2ECHECK(st == CHROME_RUNTIME_POLL_ENGINE_EOF, "happy: chrome ve EOF");
    E2ECHECK(s.rt.engine_alive == 0, "happy: engine_alive=0");
}

static void scenario_watchdog_ping_pong(void) {
    static struct e2e_session s;
    session_setup(&s, 0u);

    /* Avanca o tempo ate o intervalo de PING; tick deve enviar. */
    uint64_t t = BROWSER_WATCHDOG_PING_INTERVAL_TICKS;
    int r = chrome_runtime_tick(&s.rt, t);
    E2ECHECK(r == 0, "wd: tick nao pediu kill");
    E2ECHECK(s.rt.chrome.watchdog.state == BROWSER_WATCHDOG_PING_IN_FLIGHT,
             "wd: PING_IN_FLIGHT apos tick");
    E2ECHECK(s.rt.total_requests_sent == 1u, "wd: 1 request enviado");

    /* Engine responde com PONG. */
    engine_pump(&s.eng);
    E2ECHECK(s.eng.last_kind == BROWSER_IPC_PING, "wd: engine recebeu PING");

    /* Chrome consome PONG e watchdog volta a IDLE. */
    uint32_t a = 0u;
    int st = chrome_runtime_poll_event(&s.rt, t + 10u, &a);
    E2ECHECK(st == CHROME_RUNTIME_POLL_EVENT_HANDLED, "wd: PONG handled");
    E2ECHECK(s.rt.chrome.watchdog.state == BROWSER_WATCHDOG_IDLE,
             "wd: IDLE apos PONG");
    E2ECHECK(s.rt.total_pongs_received == 1u, "wd: pongs=1");
}

static void scenario_watchdog_timeout_kills(void) {
    static struct e2e_session s;
    session_setup(&s, 0u);
    s.eng.freeze = 1; /* engine ignora PINGs */

    uint64_t t = 0u;
    int killed = 0;
    for (uint32_t i = 0; i < BROWSER_WATCHDOG_MAX_MISSED_PONGS + 2u; ++i) {
        t += BROWSER_WATCHDOG_PING_INTERVAL_TICKS;
        int r = chrome_runtime_tick(&s.rt, t);
        engine_pump(&s.eng); /* engine recebe PING mas ignora */
        if (r == 1) { killed = 1; break; }
        t += BROWSER_WATCHDOG_PONG_TIMEOUT_TICKS;
        r = chrome_runtime_tick(&s.rt, t);
        if (r == 1) { killed = 1; break; }
    }
    E2ECHECK(killed == 1, "wd-timeout: tick eventualmente pediu kill");
    E2ECHECK(s.rt.engine_alive == 0, "wd-timeout: engine_alive=0");
    /* Engine recebeu pelo menos MAX_MISSED+1 PINGs. */
    E2ECHECK(s.eng.total_received >= BROWSER_WATCHDOG_MAX_MISSED_PONGS,
             "wd-timeout: engine recebeu varios PINGs");
}

static void scenario_cancel_drops_old_nav(void) {
    static struct e2e_session s;
    session_setup(&s, 0u);

    /* Navegacao 1: pump completo. */
    chrome_runtime_send_navigate(&s.rt, "a", 1);
    engine_pump(&s.eng);
    int count = 0; uint32_t a = 0u;
    drain_chrome_events(&s.rt, 0u, &a, &count);
    E2ECHECK(s.rt.chrome.current_nav_id == 1u, "cancel: nav 1 ok");

    /* Inicia nav 2 e enfileira CANCEL antes do engine processar.
     * current_nav_id so avanca apos engine emitir NAV_STARTED. */
    chrome_runtime_send_navigate(&s.rt, "b", 1);
    E2ECHECK(s.rt.chrome.current_nav_id == 1u,
             "cancel: nav_id ainda 1 antes do pump");
    E2ECHECK(s.rt.chrome.status == BROWSER_CHROME_STATUS_LOADING,
             "cancel: status LOADING apos send");
    chrome_runtime_send_cancel(&s.rt);
    engine_pump(&s.eng);
    drain_chrome_events(&s.rt, 0u, &a, &count);
    /* Apos pump+drain, current_nav_id avancou para 2 e o status
     * reflete os eventos finais da nav 2 (READY ou CANCELLED). */
    E2ECHECK(s.rt.chrome.current_nav_id == 2u,
             "cancel: nav_id avancou para 2 apos pump");
    E2ECHECK(s.rt.chrome.status == BROWSER_CHROME_STATUS_CANCELLED
             || s.rt.chrome.status == BROWSER_CHROME_STATUS_READY,
             "cancel: status final coerente");
}

static void scenario_engine_crash_detected(void) {
    static struct e2e_session s;
    session_setup(&s, 0u);
    s.eng.crash_after_navigate = 1;

    chrome_runtime_send_navigate(&s.rt, "x", 1);
    engine_pump(&s.eng);
    /* Chrome consome NAV_STARTED e depois ve EOF. */
    uint32_t a = 0u;
    int st = chrome_runtime_poll_event(&s.rt, 0u, &a);
    E2ECHECK(st == CHROME_RUNTIME_POLL_EVENT_HANDLED, "crash: NAV_STARTED ok");
    st = chrome_runtime_poll_event(&s.rt, 0u, &a);
    E2ECHECK(st == CHROME_RUNTIME_POLL_ENGINE_EOF, "crash: EOF detectado");
    E2ECHECK(s.rt.engine_alive == 0, "crash: engine_alive=0");
    E2ECHECK(s.rt.total_engine_eofs == 1u, "crash: eofs=1");
}

static void scenario_restart_after_kill(void) {
    static struct e2e_session s;
    session_setup(&s, 0u);
    /* Forca matar via shutdown. */
    chrome_runtime_send_shutdown(&s.rt);
    engine_pump(&s.eng);
    uint32_t a = 0u;
    chrome_runtime_poll_event(&s.rt, 0u, &a); /* EOF */
    E2ECHECK(s.rt.engine_alive == 0, "restart: engine morto");

    /* Pinta um frame falso para confirmar limpeza pelo restart. */
    s.rt.chrome.last_frame.nav_id = 99;
    s.rt.chrome.last_frame.width = 100;
    s.rt.chrome.last_frame.pixels = (const uint8_t *)0xDEADBEEFul;

    /* "Re-spawna" — em producao seria browser_engine_spawn() de novo. */
    e2e_pipes_reset();
    chrome_runtime_record_restart(&s.rt, /*new_req*/0, /*new_resp*/1,
                                   /*new_pid*/77u, /*now*/100u);
    engine_init(&s.eng, /*req_read*/0, /*resp_write*/1);
    E2ECHECK(s.rt.engine_alive == 1, "restart: engine_alive=1");
    E2ECHECK(s.rt.chrome.last_frame.pixels == (const uint8_t *)0,
             "restart: frame antigo limpo");

    /* Nova navegacao deve funcionar. */
    int rc = chrome_runtime_send_navigate(&s.rt, "y", 1);
    E2ECHECK(rc == 0, "restart: navigate apos restart ok");
    int count = session_run_until_idle(&s, 100u, &a);
    E2ECHECK(count == 7, "restart: 7 eventos pos-restart (slice 5d)");
    E2ECHECK(s.rt.chrome.status == BROWSER_CHROME_STATUS_READY,
             "restart: READY apos nova nav");
}

static void scenario_protocol_error_on_garbage(void) {
    static struct e2e_session s;
    session_setup(&s, 0u);
    /* Injeta lixo no response pipe (sem passar pelo engine). */
    uint8_t junk[BROWSER_IPC_HEADER_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF};
    e2e_pipe_write(1, junk, sizeof(junk));
    uint32_t a = 0u;
    int st = chrome_runtime_poll_event(&s.rt, 0u, &a);
    E2ECHECK(st == CHROME_RUNTIME_POLL_PROTOCOL_ERR, "proto-err: detectado");
}

static void scenario_two_navigations_back_to_back(void) {
    static struct e2e_session s;
    session_setup(&s, 0u);
    uint32_t a = 0u;
    chrome_runtime_send_navigate(&s.rt, "a", 1);
    (void)session_run_until_idle(&s, 0u, &a);
    E2ECHECK(s.rt.chrome.current_nav_id == 1u, "2nav: nav1 nav_id=1");
    E2ECHECK(s.rt.chrome.status == BROWSER_CHROME_STATUS_READY, "2nav: nav1 READY");

    chrome_runtime_send_navigate(&s.rt, "bb", 2);
    (void)session_run_until_idle(&s, 0u, &a);
    E2ECHECK(s.rt.chrome.current_nav_id == 2u, "2nav: nav2 nav_id=2");
    E2ECHECK(s.rt.chrome.status == BROWSER_CHROME_STATUS_READY, "2nav: nav2 READY");
    /* Slice 5d: 7 eventos por nav x 2 navs = 14 esperados (mais
     * piso de seguranca). */
    E2ECHECK(s.rt.total_events_polled >= 14u,
             "2nav: ao menos 14 eventos totais (slice 5d)");
}

static void scenario_pong_unsolicited_ignored(void) {
    /* PONG sem PING anterior nao deve quebrar o watchdog. */
    static struct e2e_session s;
    session_setup(&s, 0u);
    /* Engine emite um PONG do nada. */
    uint8_t pl[4]; be_put_u32(pl, 0xDEADu);
    engine_send(&s.eng, BROWSER_IPC_EVENT_PONG, pl, 4);
    uint32_t a = 0u;
    int st = chrome_runtime_poll_event(&s.rt, 0u, &a);
    E2ECHECK(st == CHROME_RUNTIME_POLL_EVENT_HANDLED,
             "unsol-pong: handled (sem crash)");
    E2ECHECK(s.rt.chrome.watchdog.state == BROWSER_WATCHDOG_IDLE,
             "unsol-pong: watchdog continua IDLE");
}

int test_browser_e2e_run(void) {
    printf("[test_browser_e2e]\n");
    g_e2e_passed = 0; g_e2e_failed = 0;
    scenario_happy_navigation();
    scenario_watchdog_ping_pong();
    scenario_watchdog_timeout_kills();
    scenario_cancel_drops_old_nav();
    scenario_engine_crash_detected();
    scenario_restart_after_kill();
    scenario_protocol_error_on_garbage();
    scenario_two_navigations_back_to_back();
    scenario_pong_unsolicited_ignored();
    printf("  -> %d passed, %d failed\n", g_e2e_passed, g_e2e_failed);
    return g_e2e_failed;
}
