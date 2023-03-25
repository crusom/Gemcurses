#ifndef TLS_TOFU_H
#define TLS_TOFU_H

enum tofu_check_results {
  TOFU_OK,
  TOFU_NEW_HOSTNAME,
  TOFU_FINGERPRINT_MISMATCH,
};

struct known_host;
  struct known_host {
  char *hostname, *fingerprint;
  int lineno;
  struct known_host *next;
};

int tofu_load_certs(struct known_host **host);
int tofu_save_cert(struct known_host **host, char *hostname, char *fingerprint);
enum tofu_check_results tofu_check_cert(struct known_host **host, char *hostname,
           char *fingerprint);
void tofu_change_cert(struct known_host *host, char *hostname, char *new_fingerprint);
#endif
