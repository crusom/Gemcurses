#include <tofu.h>

static const char *host_filename = "known_hosts";


enum tofu_check_results tofu_check_cert(struct known_host **host, char *hostname, char *fingerprint) {
  if(host == NULL) {
    goto save;
  }

  struct known_host *tmp_host = *host;

  while(tmp_host) {

    // printf("HOST: %s", (tmp_host)->hostname);
    if(strcmp(tmp_host->hostname, hostname) != 0) {
      goto next;
    }
      
    if(strcmp(tmp_host->fingerprint, fingerprint) == 0) {
//      printf("Valid fingerprint found!\n");
      return TOFU_OK;
    }
//    TODO bad cert
//    fprintf(stderr, "FINGERPRINT MISMATCH!");
    return TOFU_FINGERPRINT_MISMATCH;
   
next:
        tmp_host = tmp_host->next;
  }
   
    
save:
//  printf("Fingerprint not found\n");
   
  tofu_save_cert(host, hostname, fingerprint);
  return TOFU_NEW_HOSTNAME;
}

int tofu_save_cert(struct known_host **host, char *hostname, char *fingerprint) {
  FILE *f = fopen(host_filename, "a");
  if(f == NULL)
    return -1;
  
  fprintf(f, "%s %s %s\n", hostname,
             "SHA-512", fingerprint);
  fclose(f);

  struct known_host *tmp_host = (struct known_host*) calloc(1, sizeof(struct known_host));
  tmp_host->hostname = strdup(hostname);
  tmp_host->fingerprint = strdup(fingerprint);
  tmp_host->next = *host;
  *host = tmp_host;
  return 0;
}


int tofu_load_certs(struct known_host **host) {
  FILE *f = fopen(host_filename, "r");
  if(f == NULL) {
    return -1;
  }
    
  size_t n = 0;
  int lineno = 1;
  char *line = NULL;

  while(getline(&line, &n, f) != -1) {
    int ln = strlen(line);
    if(line[ln - 1] == '\n') {
      line[ln - 1] = 0;
    }


    struct known_host *tmp_host = calloc(1, sizeof(struct known_host));

    char *tok = strtok(line, " ");
    assert(tok);
    tmp_host->hostname = strdup(tok);

    tok = strtok(NULL, " ");
    assert(tok);
    if(strcmp(tok, "SHA-512") != 0) {
      free(tmp_host->hostname);
      free(tmp_host);
      continue;
    }

    tok = strtok(NULL, " ");
    assert(tok);
    tmp_host->fingerprint = strdup(tok);
    tmp_host->lineno = lineno++;
    tmp_host->next = *host;
    
    *host = tmp_host;
  }

  assert(*host);

  free(line);
  return 0;

}

