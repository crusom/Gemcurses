#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/pem.h>
#include <openssl/opensslv.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#include <tls.h>

#define GEMINI_SCHEME "gemini://"
#define HOST_PORT ":1965"
#define HOST_RESOURCE "/"
#define CLRN "\r\n"
#define SESSION_NUM 30

#define CERT_FILENAME "key.pem" 
#define KEY_FILENAME  "cert.pem"

enum {
  TLS_DEBUGGING = 1 << 0,
};

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


static void init_openssl_library(void) {
  (void)SSL_library_init();
  SSL_load_error_strings();
}

static int ssl_session_callback(SSL *ssl, SSL_SESSION *session) {

  struct gemini_tls *gem_tls;

  if((gem_tls = (struct gemini_tls*)SSL_get_ex_data(ssl, 0)) == NULL) {
    fprintf(stderr, "ERROR: cant get ex data");
    exit(EXIT_FAILURE);
  }
  
  struct session_reuse *sess_p = gem_tls->session;
  
  while(sess_p) {
    if(strcmp(gem_tls->cur_hostname, sess_p->hostname) == 0){    
//      SSL_SESSION_free(sess_p->session);
      sess_p->session = session;
      return 0;
    }
    sess_p = sess_p->next;
  }
 
  struct session_reuse *tmp_sess;
  tmp_sess = (struct session_reuse*) calloc(1, sizeof(struct session_reuse));
  tmp_sess->hostname = strdup(gem_tls->cur_hostname);  
  tmp_sess->session = session;
  tmp_sess->next = gem_tls->session;
  gem_tls->session = tmp_sess;

  gem_tls->session_indx++;
  return 0;
}

// for debugging
static void ssl_info_callback(const SSL * ssl, int where, int ret){
  if(where & SSL_CB_HANDSHAKE_START){
    SSL_SESSION *session = SSL_get_session(ssl);
    if(session) {
      printf("handshake begin %p %p %ld %ld\n",ssl,session,SSL_SESSION_get_time(session), SSL_SESSION_get_timeout(session));
    }
    else {
      printf("handshake begin %p %p \n",ssl,session);
    }
  }

  if(where & SSL_CB_HANDSHAKE_DONE){
    SSL_SESSION * session = SSL_get_session(ssl);
    printf("handshake done %p reused %d %ld %ld\n",session,SSL_session_reused((SSL*)ssl),
                                  SSL_SESSION_get_time(session), SSL_SESSION_get_timeout(session));
    
    SSL_SESSION_print_fp(stdout,session);
  }

}


static SSL_SESSION *tls_get_session(struct gemini_tls *gem_tls, const char *hostname) {
 
  struct session_reuse *sess_p = gem_tls->session;
  
  while(sess_p) {
    if(strcmp(hostname, sess_p->hostname) == 0){    
      return sess_p->session;
    }
    sess_p = sess_p->next;
  }

  return NULL;

}

// TODO
void handleFailure() {
  fprintf(stderr, "ERROR: smth went wrong");
}

int parse_url(char *error_message, char *hostname, char **host_resource, char *port) {
  int gemini_scheme_length = strlen(GEMINI_SCHEME);
  // at first delete gemini scheme from hostname if it is included
  if(strncmp(hostname, GEMINI_SCHEME, gemini_scheme_length) == 0) {
    // + 1 for '\0'
    memmove(hostname, hostname + gemini_scheme_length, strlen(hostname) - gemini_scheme_length + 1);
  }
  
  // then check if there's port number included
  char *p_port = strchr(hostname, ':');
  if(p_port != NULL) {
    port[0] = ':';
    p_port++;
    int i = 1;

    while(*p_port){
      if(isdigit(*p_port)) {
        if(i < 6)
          port[i] = *p_port;
        else {
          error_message = "ERROR: invalid port number\n";
          return 0;
        }
        p_port++;
        i++;
      }
      else {
        break;
      }
    }

    if(strlen(port) == 1){
      error_message = "ERROR: Invalid port number\n";
      return 0;
    }

    // and cut the port number from the hostname
    memmove(p_port - i, p_port, strlen(p_port) + 1);
  }
  else {
    // default port
    strcpy(port, ":1965");
  }

  // get the resource if included and cut it from the hostname
  char *tmp_resource = strchr(hostname, '/');
  if(tmp_resource != NULL) {
    int resource_offset = tmp_resource - hostname;
    int resource_lenght = strlen(hostname) - resource_offset;
    *host_resource = (char *)malloc(resource_lenght + 1);
    
    memcpy(*host_resource, hostname + resource_offset, resource_lenght);
    (*host_resource)[resource_lenght] = '\0';    
    hostname[resource_offset] = '\0';
  }
  else {
    // default resource
    *host_resource = strdup("/");
  }

    return 1;
}

//static int tls_handle_response(char *resp) {}

static int tls_hex_string(const unsigned char *in, size_t inlen, char **out,
                          size_t *outlen) {
  static const char hex[] = "0123456789abcdef";
  size_t i, len;
  char *p;

  if(outlen != NULL)
    *outlen = 0;

  if(inlen >= SIZE_MAX)
    return -1;
  
  if((*out = (char *)malloc((inlen + 1) * 2)) == NULL)
    return -1;

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

  return 0;
}

static int tls_get_peer_fingerprint(SSL_CTX *ctx, X509 *cert, char **hash) {
  unsigned char d[EVP_MAX_MD_SIZE];
  char *dhex = NULL;
  unsigned int dlen;

  *hash = NULL;
  if (cert == NULL)
    return -1;

  if (X509_digest(cert, EVP_sha256(), d, &dlen) != 1) {
    fprintf(stderr, "ERROR: cant get cert digest\n");
    goto err;
  }

  if (tls_hex_string(d, dlen, &dhex, NULL) != 0) {
    fprintf(stderr, "ERROR: cant get tls hex string\n");
    goto err;
  }

  *hash = dhex;

  return 0;

err:
  if(dhex != NULL)
    free(dhex);

  return -1;
}

static int tls_create_cert() {
  
  EVP_PKEY_CTX *ctx;
  EVP_PKEY *pkey = NULL;
  
  ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
  
  if(!ctx){ 
    fprintf(stderr, "ERROR: cant create EVP context\n");
    return -1;
  }
     
  if(EVP_PKEY_keygen_init(ctx) <= 0) {
    fprintf(stderr, "ERROR: cant init EVP\n");
    return -1;
  }       

  if(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
    fprintf(stderr, "ERROR: cant set RSA bits\n");
    return -1;
  }

  if(EVP_PKEY_keygen(ctx, &pkey) <= 0){
    fprintf(stderr, "ERROR: cant generate RSA key\n");
    return -1;
  }

  X509 *x509 = X509_new();
  if(!x509) {
    fprintf(stderr, "ERROR: cant create x509 structure\n");
    return -1;
  }

  ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
  X509_gmtime_adj(X509_get_notBefore(x509), 0);
  X509_gmtime_adj(X509_get_notAfter(x509), 31536000L);

  X509_set_pubkey(x509, pkey);
  
  X509_NAME *name = X509_get_subject_name(x509);
  X509_NAME_add_entry_by_txt(name, "C",  MBSTRING_ASC,
                                 (unsigned char *)"PL", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "O",  MBSTRING_ASC,
                                 (unsigned char *)"MyCompany Inc.", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                 (unsigned char *)"localhost", -1, -1, 0);

  X509_set_issuer_name(x509, name);
  if(!X509_sign(x509, pkey, EVP_sha256())) {
    fprintf(stderr, "ERROR: cant sign certificate\n");
    return -1;
  }

  FILE *f = fopen(KEY_FILENAME, "wb");
  if(!f) {
    fprintf(stderr, "ERROR: cant open private key file for writing\n");
    return -1;
  }
 
  int ret = PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL);
  fclose(f);

  if(!ret) {
    fprintf(stderr, "ERROR: cant write private key to file\n");
    return -1;
  }

  f = fopen(CERT_FILENAME, "wb");
  if(!f) {
    fprintf(stderr, "ERROR: cant open cert file for writing\n");
    return -1;
  }


  ret = PEM_write_X509(f, x509);
  fclose(f);
  
  if(!ret) {
    fprintf(stderr, "ERROR: cant write cert to file\n");
    return -1;
  }

  return 0;
}

struct gemini_tls* init_tls(int flag) {
  
  struct gemini_tls* gem_tls = (struct gemini_tls*) calloc(1, sizeof(struct gemini_tls));
  int res;

  if(!gem_tls) {
    return NULL;
  }

  gem_tls->session_indx = 0;
  gem_tls->session = NULL;
  gem_tls->host = NULL;
  if(tofu_load_certs(&gem_tls->host) != 0)
    gem_tls->host = NULL;

  init_openssl_library();
  
  const SSL_METHOD* method = TLS_client_method();
  if(method == NULL) {
    fprintf(stderr, "ERROR: cant set SSL method\n");
    goto cleanup;
  }

  gem_tls->ctx = SSL_CTX_new(method);
  if(gem_tls->ctx == NULL) {
    fprintf(stderr, "ERROR: cant create new context\n");
    goto cleanup;
  }


  // TODO auto generate cert
  // https://stackoverflow.com/questions/256405/programmatically-create-x509-certificate-using-openssl/15082282#15082282

  if(access(CERT_FILENAME, F_OK ) != 0 || access(KEY_FILENAME, F_OK) != 0) {
    if(tls_create_cert() != 0) {
      goto cleanup;
    }
  }


  if(SSL_CTX_use_certificate_file(gem_tls->ctx, CERT_FILENAME, SSL_FILETYPE_PEM) != 1) {
    fprintf(stderr, "ERROR: cant load client cert\n");
    goto cleanup;
  }
  if(SSL_CTX_use_PrivateKey_file(gem_tls->ctx, KEY_FILENAME, SSL_FILETYPE_PEM) != 1) {
    fprintf(stderr, "ERROR: cant load client private key\n");
    goto cleanup;  
  }

  SSL_CTX_sess_set_new_cb(gem_tls->ctx, ssl_session_callback);
  SSL_CTX_set_session_cache_mode(gem_tls->ctx, SSL_SESS_CACHE_CLIENT);
  
  if((flag & TLS_DEBUGGING) == 1)
    SSL_CTX_set_info_callback(gem_tls->ctx, ssl_info_callback);
  

  SSL_CTX_set_verify_depth(gem_tls->ctx, 4);

  // disable SSL cause its obolete and dangerous
  const long flags = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
  SSL_CTX_set_options(gem_tls->ctx, flags);

  gem_tls->bio_web = BIO_new_ssl_connect(gem_tls->ctx);
  if(gem_tls->bio_web == NULL) {
    fprintf(stderr, "ERROR: cant create bio new ssl connect\n");
    goto cleanup;
  }
  
  gem_tls->bio_out = BIO_new_fp(stdout, BIO_NOCLOSE);
  if(gem_tls->bio_out == NULL) {
    fprintf(stderr, "ERROR: cant create new fp\n");
    goto cleanup;
  }
 
  gem_tls->bio_mem = BIO_new(BIO_s_mem());
  if(gem_tls->bio_mem == NULL) {
    fprintf(stderr, "ERROR: cant create new fp\n");
    goto cleanup;
  }

  BIO_get_ssl(gem_tls->bio_web, &gem_tls->ssl);
  if(gem_tls->ssl == NULL)  {
    fprintf(stderr, "ERROR: cant get ssl\n");
    goto cleanup;
  }
  
  const char* const PREFERRED_CIPHERS = "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4";
  res = SSL_set_cipher_list(gem_tls->ssl, PREFERRED_CIPHERS);
  if(res == 0) {
    fprintf(stderr, "ERROR: cant set cipher list\n");
    goto cleanup;
  }

  // use ex data in callbacks 
  SSL_set_ex_data(gem_tls->ssl, 0, gem_tls);

  return gem_tls;

cleanup:
  free(gem_tls);
  return NULL;
}

inline static void alloc_error() {
  fprintf(stderr, "ERROR: cant allocate memory\n");
  exit(EXIT_FAILURE);
}

int tls_connect(struct gemini_tls *gem_tls, const char *h, struct response *resp) {
  long res = 1;
  int return_val = 1;
  char *hostname = strdup(h), host_port[6] = {0};
  char *hostname_with_portn = NULL, *url = NULL;
  char *host_resource = NULL, *hash = NULL;

  if(hostname == NULL)
    alloc_error();
  
  if(!parse_url(resp->error_message, hostname, &host_resource, host_port)){
    resp->error_message = "ERROR: cant parse url";
    goto error;
  }

  if(host_resource == NULL)
    alloc_error();

//  TODO DEBUGGING
//  printf("host name: %s\n", hostname);
//  printf("host resource: %s\n", host_resource);
//  printf("port number: %s\n", host_port);

  hostname_with_portn = strdup(hostname);
  if(hostname_with_portn == NULL)
    alloc_error();

  char* temp = realloc(hostname_with_portn, 
                       strlen(hostname_with_portn) + strlen(host_port) + 1);
  if(temp == NULL)
    alloc_error();

  hostname_with_portn = temp;
  strcat(hostname_with_portn, host_port);
  gem_tls->cur_hostname = strdup(hostname_with_portn);

  res = BIO_set_conn_hostname(gem_tls->bio_web, hostname_with_portn);
  if(res == 0) {
    resp->error_message = "ERROR: cant set conn hostname\n";
    goto error;
  }

  
  res = SSL_set_tlsext_host_name(gem_tls->ssl, hostname);
  if(res == 0) {
    resp->error_message = "ERROR: cant set hostname\n";
    goto error;
  }

  SSL_SESSION *session;
  if((session = tls_get_session(gem_tls, hostname)) != NULL) {
    SSL_set_session(gem_tls->ssl, session);
  }


  #include <sys/poll.h>
  int fdSocket;
  fd_set connectionfds;
  
  // I need non-blocking socket for poll()
  BIO_set_nbio(gem_tls->bio_web, 1);
  
  res = BIO_do_connect(gem_tls->bio_web);
  if((res <= 0) && !BIO_should_retry(gem_tls->bio_web)) {
    resp->error_message = "ERROR: cant connect\n";
    goto error;
  }
 
  if(BIO_get_fd(gem_tls->bio_web, &fdSocket) < 0) {
    resp->error_message = "ERROR: cant get fd\n";
    goto error;
  }
  
  if(res <= 0) {
    FD_ZERO(&connectionfds);
    FD_SET(fdSocket, &connectionfds);

    struct pollfd fd;
    fd.fd = fdSocket;
    fd.events = POLLOUT;

    res = poll(&fd, 1, 1000);
    if(res == 0){
      resp->error_message = "ERROR: timeout\n"; 
      goto error;
    }
  }

  // this busy loop is bad for performance but i have no idea
  // how to do it efficiently.
  // (blocking sockets seem efficient but then i can't use poll())
  // without flushing in some cases we may be stuck forever
  while(BIO_should_retry(gem_tls->bio_web) && BIO_flush(gem_tls->bio_web) > 0) {
    res = BIO_do_connect(gem_tls->bio_web);
  }
  
  if(res <= 0) {
    resp->error_message = "ERROR: cant connect to the peer\n";
    goto error;
  }

  res = BIO_do_handshake(gem_tls->bio_web);
  if(res == 0) {
    resp->error_message = "ERROR: cant do handshake\n";
    goto error;
  }

  if(SSL_session_reused(gem_tls->ssl) == 1)
    resp->was_resumpted = true;
  else
    resp->was_resumpted = false;


  //TODO add client certificate
  // for test: gemini.thegonz.net/diohsc/

  X509* cert = SSL_get_peer_certificate(gem_tls->ssl);
  if(cert == NULL) {
    resp->error_message = "ERROR: cant get peer cert\n";
    goto error;
  }
   
  tls_get_peer_fingerprint(gem_tls->ctx, cert, &hash);
  // there may be diffrent certs on different ports and subdomains
  resp->cert_result = tofu_check_cert(&gem_tls->host, hostname_with_portn, hash); 
  
//  printf("CERT: %s", hash);
  if(cert) { 
    X509_free(cert); 
  }

  int url_len = strlen("gemini://") + strlen(hostname) + strlen(host_resource) + strlen(CLRN) + 1;
  url = (char*)malloc(url_len);
  
  strcpy(url, "gemini://");
  strcat(url, hostname);
  strcat(url, host_resource);
  strcat(url, CLRN);
  url[url_len - 1] = '\0';
  
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
  if(hostname_with_portn != NULL)
    free(hostname_with_portn); 
  if(url != NULL)
    free(url);
  if(hash != NULL)
    free(hash);
 
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
  } while (len > 0 || BIO_should_retry(gem_tls->bio_web));

  BUF_MEM *bptr = NULL;
  if(BIO_get_mem_ptr(gem_tls->bio_mem, &bptr) > 0) {
    if(bptr != NULL) {
      if(bptr->length <= 0 || bptr->data == NULL)
        return 0;
    }
    else return 0;
  }
  else
    return 0;

  char *res = malloc(bptr->length + 1);
  if(res == NULL) {
    fprintf(stderr, "Cant malloc");
    exit(EXIT_FAILURE);
  }

  memcpy(res, bptr->data, bptr->length);
  // ensure that we have null byte at the end, or some terrible things may happen
  res[bptr->length] = '\0';
  resp->body_size = bptr->length;
  resp->body = res;
  return 1;
}


struct response *tls_request(struct gemini_tls *gem_tls, const char *h) {
  
  int res;
  struct response *resp = calloc(1, sizeof(struct response));
  if(resp == NULL) return NULL;
  resp->error_message = NULL;
  resp->body = NULL;

  if(!(res = tls_connect(gem_tls, h, resp))) {
    tls_reset(gem_tls);
    return resp;
  }
  
  tls_read(gem_tls, resp);
  tls_reset(gem_tls);

  if(resp->body && resp->body[0] && resp->body[1]) {
    int resp_num;
    resp_num = resp->body[1] - '0';
    resp_num += 10 * (resp->body[0] - '0');
    resp->status_code = resp_num;
  }
  else {
    resp->error_message = "Can't connect to the host";
    resp->status_code = 0;
  }
  
  return resp;
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

//int main(int argc, char *argv[]) {
//
//  if(argc < 2 || !argv[argc - 1]) return -1;
//  int flag = 0;
//  if(argc > 2){
//    if(strcmp(argv[1], "-d") == 0)
//      flag |= TLS_DEBUGGING;
//    
//    else
//      fprintf(stderr, "ERROR: invalid flag, continuing...\n");
//  }
//
//  struct gemini_tls *gem_tls = init_tls(flag);
//  struct response *resp = calloc(1, sizeof(struct response));
//  if(gem_tls == NULL) return -1;
//
//  for(int i = 2; i < argc; i++) {
//    tls_connect(gem_tls, argv[i], resp); 
//      char *tmp = tls_read(gem_tls);
//      printf("%s\n\n\n", tmp);
//    tls_reset(gem_tls);
//  }
//
//  tls_free(gem_tls);
//
//  return 0;
//}

