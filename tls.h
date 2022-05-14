#ifndef GEMINI_TLS_H
#define GEMINI_TLS_H
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

#include "tofu.h"


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
  const char *error_message;
  int body_size;
  enum tofu_check_results cert_result;
  enum response_status_codes status_code;
  bool was_resumpted;
};


typedef struct gemini_tls *Gemini_tls;

Gemini_tls init_tls(int flag);
struct response *tls_request(Gemini_tls gem_tls, const char *h);
int tls_connect(Gemini_tls gem_tls, const char *h, struct response *resp);
int tls_read(Gemini_tls gem_tls, struct response *resp);
void tls_reset(Gemini_tls gem_tls);
void tls_free(struct gemini_tls *gem_tls);

#endif
