#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <netdb.h>

#include "tls.h"
#include "util.h"

#define GEMINI_SCHEME "gemini://"
#define DEFAULT_HOST_PORT "1965"
#define CLRN "\r\n"

#define KEY_FILENAME "key.pem" 
#define CERT_FILENAME  "cert.pem"

enum {
  TLS_DEBUGGING = 1 << 0,
};

static int all_status_codes[] = {10, 11, 20, 30, 31, 40, 41, 42, 43, 44, 50, 51, 52, 53, 59, 60, 61, 62};

struct session_reuse {
  char *hostname;
  SSL_SESSION *session;
  struct session_reuse *next;
};
 
struct gemini_tls {
  SSL_CTX *ctx;
  SSL *ssl;
  BIO *bio_web, *bio_out, *bio_mem;
  struct known_host *host;
  struct session_reuse *session;
  int session_indx;
  int res;
  char *cur_hostname;
};


struct known_host *gem_tls_get_known_hosts(struct gemini_tls *gem_tls) {
  return gem_tls->host;
}

char *gem_tls_get_cur_hostname(struct gemini_tls *gem_tls) {
  return gem_tls->cur_hostname;
}

static void init_openssl_library(void) {
  (void)SSL_library_init();
  SSL_load_error_strings();
}

static int ssl_session_new_callback(SSL *ssl, SSL_SESSION *session) {
  if(!session) return 0;

  struct gemini_tls *gem_tls;
  if((gem_tls = (struct gemini_tls*)SSL_get_ex_data(ssl, 0)) == NULL)
    ERROR_LOG_AND_EXIT("Can't get ex data");
  
  struct session_reuse *sess_p = gem_tls->session;
  while(sess_p) {
//    INFO_LOG("sess_p->hostname: %s, gem_tls->cur_hostname: %s, are_equal: %d\n", sess_p->hostname, gem_tls->cur_hostname, strcmp(gem_tls->cur_hostname, sess_p->hostname));
    if(strcmp(gem_tls->cur_hostname, sess_p->hostname) == 0){
      sess_p->session = session;
      return 0;
    }
    sess_p = sess_p->next;
  }

 
  struct session_reuse *tmp_sess;
  tmp_sess = (struct session_reuse*) calloc(1, sizeof(struct session_reuse));
  //INFO_LOG("cur_hostname: %s", gem_tls->cur_hostname);
  tmp_sess->hostname = strdup(gem_tls->cur_hostname);  
  tmp_sess->session = session;
  tmp_sess->next = gem_tls->session;
  gem_tls->session = tmp_sess;

  gem_tls->session_indx++;
  return 0;
}

static void tls_remove_session(struct gemini_tls *gem_tls, SSL_SESSION *session) {

  struct session_reuse *sess_p = gem_tls->session;
  struct session_reuse *sess_found = NULL;

  if(sess_p->session == session) {
    sess_found = sess_p;
    if(sess_p->next)
      gem_tls->session = sess_p->next;
    else
      gem_tls->session = NULL;
  }

  while(sess_p) {
    if(sess_p->next && sess_p->next->session == session){
      sess_p->next = sess_p->next->next;
      break;
    }
    sess_p = sess_p->next;
  }

  if(sess_found) {
    if(sess_found->hostname)
      free(sess_found->hostname);
    free(sess_found);
  }
}

static void ssl_session_remove_callback(SSL_CTX *ctx, SSL_SESSION *session) {
  struct gemini_tls *gem_tls;
  if((gem_tls = (struct gemini_tls*)SSL_CTX_get_ex_data(ctx, 0)) == NULL)
    ERROR_LOG_AND_EXIT("Can't get ex data");

  tls_remove_session(gem_tls, session);
}

// for debugging
static void ssl_info_callback(const SSL * ssl, int where, int ret){
  (void)ret; // unused
  
  if(where & SSL_CB_HANDSHAKE_START){
    SSL_SESSION *session = SSL_get_session(ssl);
    if(session) {
      printf("handshake begin %p %p %ld %ld\n", (void*)ssl, (void*)session, SSL_SESSION_get_time(session), SSL_SESSION_get_timeout(session));
    }
    else {
      printf("handshake begin %p %p \n", (void*)ssl, (void*)session);
    }
  }

  if(where & SSL_CB_HANDSHAKE_DONE){
    SSL_SESSION * session = SSL_get_session(ssl);
    printf("handshake done %p reused %d %ld %ld\n",(void*)session, SSL_session_reused((SSL*)ssl),
                                  SSL_SESSION_get_time(session), SSL_SESSION_get_timeout(session));
    
    SSL_SESSION_print_fp(stdout,session);
  }

}


SSL_SESSION *tls_get_session(struct gemini_tls *gem_tls, const char *hostname) {
  struct session_reuse *sess_p = gem_tls->session;

  while(sess_p) {
    if(strcmp(hostname, sess_p->hostname) == 0){    
      return sess_p->session;
    }
    sess_p = sess_p->next;
  }

  return NULL;
}


int parse_url(const char **error_message, char *hostname, char **host_resource, char port[6]) {
  size_t gemini_scheme_length = strlen(GEMINI_SCHEME);
  // at first delete gemini scheme from hostname if it is included
  if(strncmp(hostname, GEMINI_SCHEME, gemini_scheme_length) == 0) {
    // + 1 for '\0'
    memmove(hostname, hostname + gemini_scheme_length, strlen(hostname) - gemini_scheme_length + 1);
  }
  
  // then check if there's port number included
  char *p_port = strchr(hostname, ':');
  char *p_slash = strchr(hostname, '/');
  // ':' may be included in url, so check if it's after domain name and before any dir
  if(p_port != NULL && p_port < p_slash) {
    p_port++;
    int i = 0;

    for(i = 0; *p_port && i < 5; p_port++, i++){
      if(isdigit(*p_port)) 
        port[i] = *p_port;
      else
        break;
    }

    if(i <= 1){
      *error_message = "Invalid port number\n";
      return 0;
    }

    // and cut the port number from the hostname
    memmove(p_port - i - 1, p_port, strlen(p_port) + 1);
  }
  else {
    strcpy(port, DEFAULT_HOST_PORT);
  }

  // get the resource if included and cut it from the hostname
  if(host_resource != NULL) {
    char *tmp_resource = strchr(hostname, '/');
    if(tmp_resource != NULL) {
      size_t resource_offset = (size_t)(tmp_resource - hostname);
      size_t resource_lenght = strlen(hostname) - resource_offset;
      *host_resource = (char *)malloc(resource_lenght + 1);
      
      memcpy(*host_resource, hostname + resource_offset, resource_lenght);
      (*host_resource)[resource_lenght] = '\0';    
      hostname[resource_offset] = '\0';
    }
    else {
      // default resource
      *host_resource = strdup("/");
    }
  }

    return 1;
}

static int tls_hex_string(const unsigned char *in, size_t inlen, char **out,
                          size_t *outlen) {
  static const char hex[] = "0123456789abcdef";
  size_t i, len;
  char *p;

  if(outlen != NULL)
    *outlen = 0;

  if(inlen >= SIZE_MAX)
    return 0;
  
  if((*out = (char *)malloc((inlen + 1) * 2)) == NULL)
    return 0;

  p = *out;
  len = 0;
  for (i = 0; i < inlen; i++) {
    // highest 4 bits
    p[len++] = hex[(in[i] >> 4) & 0x0f];
    // lowest  4 bits
    p[len++] = hex[in[i] & 0x0f];
  }
  
  p[len++] = 0;

  if (outlen != NULL)
    *outlen = len;

  return 1;
}

static int tls_get_peer_fingerprint(X509 *cert, char **hash) {
  unsigned char d[EVP_MAX_MD_SIZE];
  char *dhex = NULL;
  unsigned int dlen;

  *hash = NULL;
  if (cert == NULL)
    return 0;

  if (X509_digest(cert, EVP_sha256(), d, &dlen) != 1) {
    fprintf(stderr, "Can't get cert digest\n");
    goto err;
  }

  if (!tls_hex_string(d, dlen, &dhex, NULL)) {
    fprintf(stderr, "Can't get tls hex string\n");
    goto err;
  }

  *hash = dhex;

  return 1;

err:
  if(dhex != NULL)
    free(dhex);

  return 0;
}

// https://stackoverflow.com/questions/256405/programmatically-create-x509-certificate-using-openssl/15082282#15082282
static void tls_create_cert(char *key_path, char *cert_path) {
  
  EVP_PKEY_CTX *ctx;
  EVP_PKEY *pkey = NULL;
  
  ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
  
  if(!ctx)
    ERROR_LOG_AND_ABORT("Can't create EVP context\n");
     
  if(EVP_PKEY_keygen_init(ctx) <= 0)
    ERROR_LOG_AND_ABORT("Can't init EVP\n");

  if(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0)
    ERROR_LOG_AND_ABORT("Can't set RSA bits\n");

  if(EVP_PKEY_keygen(ctx, &pkey) <= 0)
    ERROR_LOG_AND_ABORT("Can't generate RSA key\n");

  X509 *x509 = X509_new();
  if(!x509)
    ERROR_LOG_AND_ABORT("Can't create x509 structure\n");

  ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
  X509_gmtime_adj(X509_get_notBefore(x509), 0);
  // quite random value, long enough
  X509_gmtime_adj(X509_get_notAfter(x509), 2137 * 31536000L);

  X509_set_pubkey(x509, pkey);
  
  X509_NAME *name = X509_get_subject_name(x509);
  X509_NAME_add_entry_by_txt(name, "C",  MBSTRING_ASC,
                                 (unsigned char *)"PL", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "O",  MBSTRING_ASC,
                                 (unsigned char *)"MyCompany Inc.", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                 (unsigned char *)"localhost", -1, -1, 0);

  X509_set_issuer_name(x509, name);
  if(!X509_sign(x509, pkey, EVP_sha256()))
    ERROR_LOG_AND_ABORT("Can't sign certificate\n");

  FILE *f = fopen(key_path, "wb");
  if(!f)
    ERROR_LOG_AND_ABORT("Can't open private key file for writing\n");
 
  int ret = PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL);
  // free pkey
  EVP_PKEY_free(pkey);
  pkey = NULL;
  // close the written pkey file
  fclose(f);

  if(!ret)
    ERROR_LOG_AND_ABORT("Can't write private key to file\n");

  f = fopen(cert_path, "wb");
  if(!f) 
    ERROR_LOG_AND_ABORT("Can't open cert file for writing\n");

  ret = PEM_write_X509(f, x509);
  fclose(f);
  
  if(!ret) 
    ERROR_LOG_AND_ABORT("Can't write cert to file\n");

  EVP_PKEY_CTX_free(ctx);
  X509_free(x509);
}

struct gemini_tls* init_tls(int flag) {
  
  struct gemini_tls* gem_tls = (struct gemini_tls*) calloc(1, sizeof(struct gemini_tls));
  int res;

  if(!gem_tls)
    MALLOC_ERROR;

  gem_tls->session_indx = 0;
  gem_tls->session = NULL;
  gem_tls->host = NULL;
  if(!tofu_load_certs(&gem_tls->host))
    gem_tls->host = NULL;

  init_openssl_library();
  
  const SSL_METHOD* method = TLS_client_method();
  if(method == NULL) 
    ERROR_LOG_AND_ABORT("Can't set SSL method\n");

  gem_tls->ctx = SSL_CTX_new(method);
  if(gem_tls->ctx == NULL) 
    ERROR_LOG_AND_ABORT("Can't create new context\n");


  char key_path[PATH_MAX + 1], cert_path[PATH_MAX + 1];
  get_file_path_in_data_dir(KEY_FILENAME, key_path, sizeof(key_path));  
  get_file_path_in_data_dir(CERT_FILENAME, cert_path, sizeof(cert_path));  

  if(access(cert_path, F_OK ) != 0 || access(key_path, F_OK) != 0)
    tls_create_cert(key_path, cert_path);

  if(SSL_CTX_use_certificate_file(gem_tls->ctx, cert_path, SSL_FILETYPE_PEM) != 1)
    ERROR_LOG_AND_EXIT("Can't load client cert, check if it's valid\n");

  if(SSL_CTX_use_PrivateKey_file(gem_tls->ctx, key_path, SSL_FILETYPE_PEM) != 1) 
    ERROR_LOG_AND_EXIT("Can't load client private key, check if it's valid\n");

  SSL_CTX_sess_set_new_cb(gem_tls->ctx, ssl_session_new_callback);
  SSL_CTX_sess_set_remove_cb(gem_tls->ctx, ssl_session_remove_callback);
  SSL_CTX_set_session_cache_mode(gem_tls->ctx, SSL_SESS_CACHE_CLIENT);
  
  if((flag & TLS_DEBUGGING) == 1)
    SSL_CTX_set_info_callback(gem_tls->ctx, ssl_info_callback);
  
  SSL_CTX_set_verify_depth(gem_tls->ctx, 4);
  // disable SSL because it's obsolete and dangerous
  const uint64_t flags = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
  SSL_CTX_set_options(gem_tls->ctx, flags);

  gem_tls->bio_web = BIO_new_ssl_connect(gem_tls->ctx);
  if(gem_tls->bio_web == NULL)
    ERROR_LOG_AND_ABORT("Can't create bio new ssl connect\n");
  
  gem_tls->bio_out = BIO_new_fp(stdout, BIO_NOCLOSE);
  if(gem_tls->bio_out == NULL)
    ERROR_LOG_AND_ABORT("Can't create new fp\n");
 
  gem_tls->bio_mem = BIO_new(BIO_s_mem());
  if(gem_tls->bio_mem == NULL)
    ERROR_LOG_AND_ABORT("Can't create new fp\n");

  BIO_get_ssl(gem_tls->bio_web, &gem_tls->ssl);
  if(gem_tls->ssl == NULL)
    ERROR_LOG_AND_ABORT("Can't get ssl\n");
  
  const char* const PREFERRED_CIPHERS = "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4";
  res = SSL_set_cipher_list(gem_tls->ssl, PREFERRED_CIPHERS);
  if(res == 0)
    ERROR_LOG_AND_ABORT("Can't set cipher list\n");

  // use ex data in callbacks 
  SSL_set_ex_data(gem_tls->ssl, 0, gem_tls);
  SSL_CTX_set_ex_data(gem_tls->ctx, 0, gem_tls);

  return gem_tls;
}

const char *tls_get_error(SSL *ssl, int res) {
// https://www.openssl.org/docs/man1.1.1/man3/SSL_get_error.html
  switch(SSL_get_error(ssl, res)) {
    case SSL_ERROR_NONE: return "SSL_ERROR_NONE\n";
    case SSL_ERROR_ZERO_RETURN : return "SSL_ERROR_ZERO_RETURN\n";
    case SSL_ERROR_WANT_READ: return "SSL_ERROR_WANT_READ\n";
    case SSL_ERROR_WANT_WRITE: return "SSL_ERROR_WANT_WRITE\n";
    case SSL_ERROR_WANT_CONNECT: return "SSL_ERROR_WANT_CONNECT\n";
    case SSL_ERROR_WANT_ACCEPT: return "SSL_ERROR_WANT_ACCEPT\n";
    case SSL_ERROR_WANT_X509_LOOKUP: return "SSL_ERROR_WANT_X509_LOOKUP\n";
    case SSL_ERROR_WANT_ASYNC: return  "SSL_ERROR_WANT_ASYNC\n";
    case SSL_ERROR_WANT_ASYNC_JOB: return "SSL_ERROR_WANT_ASYNC_JOB\n";
    case SSL_ERROR_WANT_CLIENT_HELLO_CB: return "SSL_ERROR_WANT_CLIENT_HELLO_CB\n";
    case SSL_ERROR_SYSCALL: return "SSL_ERROR_SYSCALL\n";
    case SSL_ERROR_SSL: return "SSL_ERROR_SSL\n";
    default: return "Unknown SSL error\n";
  }
}


int tls_connect(struct gemini_tls *gem_tls, const char *h, struct response *resp, char *fingerprint) {
  long res = 1;
  int return_val = 1;
  char *hostname = strdup(h);
  char *host_resource = NULL, *hash = NULL;
  char hostname_with_portn[1024], host_port[6] = {0};
  X509* cert = NULL;
  
  if(hostname == NULL)
    MALLOC_ERROR;
  
  if(!parse_url(&resp->error_message, hostname, &host_resource, host_port))
    goto error;

  snprintf(
    hostname_with_portn,
    sizeof(hostname_with_portn),
    "%s:%s", hostname, host_port
  );

  gem_tls->cur_hostname = strdup(hostname_with_portn);

  struct addrinfo hints = {
    .ai_family = AF_UNSPEC,
    .ai_socktype = SOCK_STREAM,
    .ai_protocol = IPPROTO_TCP
  }, *result;

  int fd = -1;
  struct timeval tv = {0};
  tv.tv_sec = 2;

  if((res = getaddrinfo(hostname, host_port, &hints, &result)) != 0) {
    resp->error_message = "Can't get address info\n";
    goto error;
  }

  struct addrinfo *it;
  int err = 0;
  for(it = result; it && err != EINTR; it = it->ai_next) {
    if((fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol)) == -1) {
      err = errno;
      continue;
    }
    if(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
       setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0 &&
       connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
      break;
    }
    err = errno;
    close(fd);
    fd = -1;
  }

  freeaddrinfo(result);

  if(fd == -1) {
    resp->error_message = "Can't connect\n";
    goto error;
  }

  res = SSL_set_tlsext_host_name(gem_tls->ssl, hostname);
  if(res == 0) {
    resp->error_message = "Can't set hostname\n";
    goto error;
  }

  SSL_SESSION *session = NULL;
  if((session = tls_get_session(gem_tls, hostname_with_portn)) != NULL) {
    if(SSL_SESSION_is_resumable(session))
      SSL_set_session(gem_tls->ssl, session);
  }

  SSL_set_fd(gem_tls->ssl, fd);
  
  res = BIO_do_handshake(gem_tls->bio_web);
  if(res <= 0) {
    resp->error_message = tls_get_error(gem_tls->ssl, res);;
    goto error;
  }

  if(SSL_session_reused(gem_tls->ssl) == 1)
    resp->was_resumpted = true;
  else {
    resp->was_resumpted = false;
    if(session) {
      tls_remove_session(gem_tls, session);
    }
  }

  cert = SSL_get_peer_certificate(gem_tls->ssl);
  if(cert == NULL) {
    resp->error_message = "Can't get peer cert\n";
    goto error;
  }
   
  if(!tls_get_peer_fingerprint(cert, &hash)) {
    resp->error_message = "Can't get peer fingerprint\n";
    goto error; 
  }

  // there may be different certs on different ports and subdomains
  resp->cert_result = tofu_check_cert(&gem_tls->host, hostname_with_portn, hash); 
   
  strcpy(fingerprint, hash);
  // printf("CERT: %s", hash);

  char url[1024];
  int url_len = snprintf(url, sizeof(url), (GEMINI_SCHEME "%s%s" CLRN), hostname, host_resource);
  assert(url_len > 0);
  if((size_t)url_len > sizeof(url)) {
    resp->error_message = "Too long url (max 1024 bytes)\n";
    goto error;
  }

  BIO_puts(gem_tls->bio_web, url);
  BIO_puts(gem_tls->bio_out, "\n");
  
  assert(return_val == 1);
  goto cleanup;

error:
  return_val = 0;

cleanup:  
  if(hostname != NULL)
    free(hostname);
  if(host_resource != NULL)
    free(host_resource);
  if(hash != NULL)
    free(hash);
  if(cert != NULL)
    X509_free(cert); 
  
  return return_val;
}


int tls_read(struct gemini_tls *gem_tls, struct response *resp) {
  int len = 0;
  char buff[1536];
  int written = 0;

  do {
    len = BIO_read(gem_tls->bio_web, buff, sizeof(buff));
    BIO_flush(gem_tls->bio_web);
    if(len > 0){
      BIO_write(gem_tls->bio_mem, buff, len);
      written += len;
    }
    else if(len == 0) {
      break;
    }
    else {
      resp->error_message = tls_get_error(gem_tls->ssl, len);
      break;
    }
  } while (len > 0 || BIO_should_retry(gem_tls->bio_web));

  BUF_MEM *bptr = NULL;
  if(BIO_get_mem_ptr(gem_tls->bio_mem, &bptr) < 0 || bptr == NULL || bptr->length <= 0 || bptr->data == NULL)
    return 0;

  char *res = malloc(bptr->length + 1);
  if(res == NULL)
    MALLOC_ERROR;

  memcpy(res, bptr->data, bptr->length);
  // ensure that we have null byte at the end, or some terrible things may happen
  res[bptr->length] = '\0';
  resp->body_size = bptr->length;
  resp->body = res;
  
  return 1;
}


void check_response(struct response *resp) {
  char *crlf;
  // <STATUS><SPACE><META><CR><LF>, minimal response is 5 bytes
  if(resp->body_size >= 5 && isdigit(resp->body[0]) && 
  isdigit(resp->body[1]) && resp->body[2] == ' ' && resp->body[3] != ' ' && 
  (crlf = strstr(resp->body, "\r\n")) && 
  (crlf - resp->body < 1024 + 3)) {

    if(crlf - 3 != resp->body) {
      char *meta = resp->body + 3;
      resp->meta = strndup(meta, crlf - meta);
    }
    else
      resp->meta = NULL;
//    else
//      resp->meta = "";

    int resp_num;
    resp_num = resp->body[1] - '0';
    resp_num += 10 * (resp->body[0] - '0');
    resp->status_code = resp_num;
    bool valid_response_code = false;
    for(size_t i = 0; i < sizeof(all_status_codes)/sizeof(all_status_codes[0]); i++) {
      if(resp_num == all_status_codes[i]) {
        valid_response_code = true;
        break;
      }
    }
    if (resp_num >= 20 && resp_num <= 29)
      valid_response_code = true;

    if(valid_response_code == false)
      resp->error_message = "Invalid status code!";
  }
  else {
    resp->error_message = "Can't connect to the host, invalid response header!";
    resp->status_code = 0;
  }
}


void tls_reset(struct gemini_tls *gem_tls) {
  BIO_ssl_shutdown(gem_tls->bio_web);
  BIO_reset(gem_tls->bio_web);
  BIO_reset(gem_tls->bio_out);
  BIO_reset(gem_tls->bio_mem);

  if(gem_tls->cur_hostname != NULL){
    free(gem_tls->cur_hostname);
    gem_tls->cur_hostname = NULL;
  }
}

void tls_free(struct gemini_tls *gem_tls) {

  // clean after yourself :)
  struct session_reuse *tmp_session;
  while(gem_tls->session != NULL) {
    tmp_session = gem_tls->session;
    gem_tls->session = gem_tls->session->next;
    free(tmp_session->hostname);
    free(tmp_session);
  }

  struct known_host *tmp_host;
  while(gem_tls->host != NULL) {
    tmp_host = gem_tls->host;
    gem_tls->host = gem_tls->host->next;
    free(tmp_host->hostname);
    free(tmp_host->fingerprint);
    free(tmp_host);
  }

  if(gem_tls->bio_web != NULL)
    BIO_free_all(gem_tls->bio_web);
  if(gem_tls->bio_out != NULL)
    BIO_free(gem_tls->bio_out);
  if(gem_tls->bio_mem != NULL)
    BIO_free(gem_tls->bio_mem);
  if(gem_tls->ctx != NULL)
    SSL_CTX_free(gem_tls->ctx);

  free(gem_tls);

}

// test usage
//
//int main(int argc, char *argv[]) {
//
//  if(argc < 2 || !argv[argc - 1]) return -1;
//  int flag = 0;
//  if(argc > 2){
//    if(strcmp(argv[1], "-d") == 0)
//      flag |= TLS_DEBUGGING;
//    
//    else
//      fprintf(stderr, "invalid flag, continuing...\n");
//  }
//
//  struct gemini_tls *gem_tls = init_tls(flag);
//  struct response *resp = calloc(1, sizeof(struct response));
//  if(gem_tls == NULL) return -1;
//
//  for(int i = 2; i < argc; i++) {
//    tls_connect(gem_tls, argv[i], resp); 
////      int tmp = tls_read(gem_tls, resp);
//      printf("%s\n\n\n", resp->body);
//      if(resp->error_message)
//        printf("%s", resp->error_message);
//      tls_reset(gem_tls);
//  }
//
//  tls_free(gem_tls);
//
//  return 0;
//}

