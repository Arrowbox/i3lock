#ifndef _UNLOCK_INDICATOR_H
#define _UNLOCK_INDICATOR_H

typedef enum {
    STATE_STARTED = 0,         /* default state */
    STATE_KEY_PRESSED = 1,     /* key was pressed, show unlock indicator */
    STATE_KEY_ACTIVE = 2,      /* a key was pressed recently, highlight part
                                   of the unlock indicator. */
    STATE_BACKSPACE_ACTIVE = 3 /* backspace was pressed recently, highlight
                                   part of the unlock indicator in red. */
} unlock_state_t;

typedef enum {
    STATE_PAM_IDLE = 0,   /* no PAM interaction at the moment */
    STATE_PAM_VERIFY = 1, /* currently verifying the password via PAM */
    STATE_PAM_WRONG = 2   /* the password was wrong */
} pam_state_t;

typedef struct modifiers {
    bool caps;
    bool alt;
    bool num;
    bool logo;
} modifiers_t;

typedef struct status {
    pam_state_t pam_state;
    unlock_state_t unlock_state;
    modifiers_t modifiers;
    int failed_attempts;
    uint32_t resolution[2];
} status_t;

typedef struct ui_opts {
    bool tile;
    bool show_failed_attempts;
    bool unlock_indicator;
    char color[7];
} ui_opts_t;

xcb_pixmap_t draw_image(const status_t *status, const ui_opts_t *ui_opts);
void redraw_screen(const status_t *status, const ui_opts_t *ui_opts);

#endif
