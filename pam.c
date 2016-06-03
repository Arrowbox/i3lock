#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <err.h>
#include <security/pam_appl.h>
#include <sys/mman.h>
#include "i3lock.h"
#include "pam.h"

extern bool debug_mode;

static int conv_callback(int num_msg, const struct pam_message **msg, struct pam_response **resp, void *appdata_ptr);
static void clear_password_memory(pam_ctx_t *ctx);

/* isutf, u8_dec © 2005 Jeff Bezanson, public domain */
#define isutf(c) (((c)&0xC0) != 0x80)

/*
 * Decrements i to point to the previous unicode glyph
 *
 */
static void u8_dec(char *s, int *i) {
    (void)(isutf(s[--(*i)]) || isutf(s[--(*i)]) || isutf(s[--(*i)]) || --(*i));
}

struct pam_ctx {
    pam_handle_t *handle;
    char password[512];
    int index;
};

static pam_ctx_t pam_ctx = {0};

pam_ctx_t *pam_initialize(void)
{
    int ret;
    struct pam_conv conv = {conv_callback, (void *)&pam_ctx};

    struct passwd *pw;
    if ((pw = getpwuid(getuid())) == NULL)
        err(EXIT_FAILURE, "getpwuid() failed");

    char *username;
    if ((username = pw->pw_name) == NULL)
        errx(EXIT_FAILURE, "pw->pw_name is NULL.\n");

    /* Initialize PAM */
    if ((ret = pam_start("i3lock", username, &conv, &pam_ctx.handle)) != PAM_SUCCESS)
        errx(EXIT_FAILURE, "PAM: %s", pam_strerror(pam_ctx.handle, ret));

    if ((ret = pam_set_item(pam_ctx.handle, PAM_TTY, getenv("DISPLAY"))) != PAM_SUCCESS)
        errx(EXIT_FAILURE, "PAM: %s", pam_strerror(pam_ctx.handle, ret));

/* Using mlock() as non-super-user seems only possible in Linux. Users of other
 * operating systems should use encrypted swap/no swap (or remove the ifdef and
 * run i3lock as super-user). */
#if defined(__linux__)
    /* Lock the area where we store the password in memory, we don’t want it to
     * be swapped to disk. Since Linux 2.6.9, this does not require any
     * privileges, just enough bytes in the RLIMIT_MEMLOCK limit. */
    if (mlock(pam_ctx.password, sizeof(pam_ctx.password)) != 0)
        err(EXIT_FAILURE, "Could not lock page in memory, check RLIMIT_MEMLOCK");
#endif

    return &pam_ctx;
}

bool pam_password_is_empty(pam_ctx_t *ctx) {
    return (ctx->index == 0);
}

bool pam_utf8_inc_password(pam_ctx_t *ctx, char *utf8_buf, int len) {
    if ((ctx->index + 8) >= sizeof(ctx->password)) {
        return false;
    }

    if (len < 2) {
        return false;
    }

    /* store it in the password array as UTF-8 */
    memcpy(ctx->password + ctx->index, utf8_buf, len - 1);
    ctx->index += len - 1;
    ctx->password[ctx->index] = '\0';

    DEBUG("current password = %.*s\n", ctx->index, ctx->password);

    return true;
}

bool pam_utf8_dec_password(pam_ctx_t *ctx) {

    if (ctx->index == 0)
        return false;

    /* decrement input_position to point to the previous glyph */
    u8_dec(ctx->password, &ctx->index);
    ctx->password[ctx->index] = '\0';

    return true;
}
bool pam_clear_password(pam_ctx_t *ctx) {

    clear_password_memory(ctx);
    ctx->index = 0;
    ctx->password[ctx->index] = '\0';

    return true;

}

bool pam_check_password(pam_ctx_t *ctx) {

    if (pam_authenticate(ctx->handle, 0) == PAM_SUCCESS) {
        DEBUG("successfully authenticated\n");
        clear_password_memory(ctx);

        /* PAM credentials should be refreshed, this will for example update any kerberos tickets.
         * Related to credentials pam_end() needs to be called to cleanup any temporary
         * credentials like kerberos /tmp/krb5cc_pam_* files which may of been left behind if the
         * refresh of the credentials failed. */
        pam_setcred(ctx->handle, PAM_REFRESH_CRED);
        pam_end(ctx->handle, PAM_SUCCESS);
        return true;
    }

    return false;
}

/*
 * Callback function for PAM. We only react on password request callbacks.
 *
 */
static int conv_callback(int num_msg, const struct pam_message **msg,
                         struct pam_response **resp, void *appdata_ptr) {

    pam_ctx_t *ctx = (pam_ctx_t *)appdata_ptr;

    if (num_msg == 0)
        return 1;

    /* PAM expects an array of responses, one for each message */
    if ((*resp = calloc(num_msg, sizeof(struct pam_response))) == NULL) {
        perror("calloc");
        return 1;
    }

    for (int c = 0; c < num_msg; c++) {
        if (msg[c]->msg_style != PAM_PROMPT_ECHO_OFF &&
            msg[c]->msg_style != PAM_PROMPT_ECHO_ON)
            continue;

        /* return code is currently not used but should be set to zero */
        resp[c]->resp_retcode = 0;
        if ((resp[c]->resp = strdup(ctx->password)) == NULL) {
            perror("strdup");
            return 1;
        }
    }

    return 0;
}

/*
 * Clears the memory which stored the password to be a bit safer against
 * cold-boot attacks.
 *
 */
static void clear_password_memory(pam_ctx_t *ctx) {
    /* A volatile pointer to the password buffer to prevent the compiler from
     * optimizing this out. */
    volatile char *vpassword = ctx->password;
    for (int c = 0; c < sizeof(ctx->password); c++) {
        /* We store a non-random pattern which consists of the (irrelevant)
         * index plus (!) the value of the beep variable. This prevents the
         * compiler from optimizing the calls away, since the value of 'beep'
         * is not known at compile-time. */
        vpassword[c] = c + (int)ctx->index;
    }
}
