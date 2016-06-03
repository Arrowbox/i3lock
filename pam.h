#ifndef _PAM_H_
#define _PAM_H_
typedef struct pam_ctx pam_ctx_t;

pam_ctx_t *pam_initialize(void);

bool pam_utf8_inc_password(pam_ctx_t *ctx, char *utf8_buf, int len);
bool pam_utf8_dec_password(pam_ctx_t *ctx);
bool pam_clear_password(pam_ctx_t *ctx);
bool pam_check_password(pam_ctx_t *ctx);
bool pam_password_is_empty(pam_ctx_t *ctx);

#endif // _PAM_H_
