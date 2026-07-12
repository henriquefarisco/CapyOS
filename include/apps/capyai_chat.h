#ifndef APPS_CAPYAI_CHAT_H
#define APPS_CAPYAI_CHAT_H

/*
 * CapyAI graphical chat app (desktop). A window with the capybara system
 * logo, a scrolling conversation, a text input line, and three permission
 * toggle buttons (Escrita / Editar / Deletar) that map to the executor's
 * risk gates. Shares the same brain as the `capyai` terminal command
 * (services/capyai.h + capy-ai-core). Rendered directly into the window
 * surface (font + rect fills), so it needs no widget/display-list bridge.
 */

#include <stdint.h>
#include "gui/compositor.h"

#define CAPYAI_CHAT_MAX_MSGS 40
#define CAPYAI_CHAT_MSG_MAX 128
#define CAPYAI_CHAT_INPUT_MAX 200
#define CAPYAI_CHAT_LAST_FILE_MAX 192

enum capyai_chat_role {
    CAPYAI_CHAT_ROLE_USER = 0,
    CAPYAI_CHAT_ROLE_ASSISTANT = 1,
    CAPYAI_CHAT_ROLE_SYSTEM = 2,
};

struct capyai_chat_msg {
    char text[CAPYAI_CHAT_MSG_MAX];
    uint8_t role;
};

struct capyai_chat_app {
    struct gui_window *window;
    struct capyai_chat_msg msgs[CAPYAI_CHAT_MAX_MSGS];
    int msg_count;
    char input[CAPYAI_CHAT_INPUT_MAX];
    int input_len;
    /* permission toggles = the three buttons; map to capyai_perms */
    int allow_write;
    int allow_delete;
    int allow_system;
    char last_file[CAPYAI_CHAT_LAST_FILE_MAX];
    uint32_t turns;
    char pending_intent[CAPYAI_CHAT_INPUT_MAX];
    int pending;
    uint64_t generation;
    uint64_t job_id;
};

/* Open (or focus if already open) the graphical CapyAI chat window.
 * Returns 0 on success and -1 when its compositor surface could not be
 * allocated.  Callers must surface that failure to the user. */
int capyai_chat_open(void);

/* Initialize the persistent one-thread service after the desktop has joined
 * the scheduler. Idempotent across close/reopen cycles. */
int capyai_chat_runtime_init(void);

/* True while the shared slot is queued, running or awaiting foreground
 * collection. Desktop teardown is deferred until this becomes false. */
int capyai_chat_busy(void);

/* Execute one queued request outside the compositor's preemption guard.
 * Submission from on_key only enqueues, so a blocking shell handler can never
 * pin the current GUI frame. */
void capyai_chat_pump(void);

#ifdef CAPYOS_CAPYAI_GUI_ASYNC_SMOKE
/* Build-only QEMU probe hooks. They drive the real compositor window and real
 * worker while production builds contain no smoke state or branches. */
int capyai_chat_smoke_start(void);
void capyai_chat_smoke_note_frame(void);
#endif

#endif /* APPS_CAPYAI_CHAT_H */
