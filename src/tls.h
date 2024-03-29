#ifndef GEMINI_TLS_H
#define GEMINI_TLS_H

#include <stddef.h>
#include <stdint.h>
#include "tofu.h"

enum tls_flags {
  TLS_DEBUGGING    = 1 << 0,
  TLS_NO_USER_CERT = 1 << 1,
};

enum response_status_codes {
// 1X INPUT
  CODE_INPUT = 10,
  CODE_SENSITIVE_INPUT = 11,
// 2X SUCCESS  
  CODE_SUCCESS = 20,
// 3X REDIRECT
  CODE_REDIRECT_TEMPORARY = 30,
  CODE_REDIRECT_PERMANENT = 31,
// 4X TEMPORARY FAILURE  
  CODE_TEMPORARY_FAILURE = 40,
  CODE_SERVER_UNAVAILABLE = 41,
  CODE_CGI_ERROR = 42,
  CODE_PROXY_ERROR = 43,
  CODE_SLOW_DOWN = 44,
// 5X PERMANENT FAILURE  
  CODE_PERMANENT_FAILURE = 50,
  CODE_NOT_FOUND = 51,
  CODE_GONE = 52,
  CODE_PROXY_REQUEST_REFUSED = 53,
  CODE_BAD_REQUEST = 59,
// 6X CLIENT CERTIFICATE REQUIRED
  CODE_CLIENT_CERTIFICATE_REQUIRED = 60,
  CODE_CERTIFICATE_NOT_AUTHORISED = 61,
  CODE_CERTIFICATE_NOT_VALID = 62,
};

struct response {
  char *body;
  char *meta;
  const char *error_message;
  size_t body_size;
  enum tofu_check_results cert_result;
  enum response_status_codes status_code;
  bool was_resumpted;
};


typedef struct gemini_tls *Gemini_tls;

// getters
struct known_host *gem_tls_get_known_hosts(struct gemini_tls *gem_tls);
char *gem_tls_get_cur_hostname(struct gemini_tls *gem_tls);

Gemini_tls init_tls(uint32_t flag);
void check_response(struct response *resp);
int tls_connect(Gemini_tls gem_tls, const char *h, struct response *resp, char fingerprint[]);
int tls_read(Gemini_tls gem_tls, struct response *resp);
void tls_reset(Gemini_tls gem_tls);
void tls_free(struct gemini_tls *gem_tls);

// helper
int parse_url(const char **error_message, char *hostname, char **host_resource, char port[6]);

#endif
