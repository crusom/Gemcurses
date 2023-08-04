#include "tofu.h"
#include "util.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <assert.h>

static const char host_filename[] = "known_hosts";

enum tofu_check_results tofu_check_cert(struct known_host **host, char *hostname, char *fingerprint) {
  if(host == NULL)
    goto save;

  struct known_host *tmp_host = *host;

  while(tmp_host) {
    if(strcmp(tmp_host->hostname, hostname) != 0) {
      goto next;
    }
      
    if(strcmp(tmp_host->fingerprint, fingerprint) == 0) {
      return TOFU_OK;
    }
    return TOFU_FINGERPRINT_MISMATCH;

next:
        tmp_host = tmp_host->next;
  }
    
save:
   
  tofu_save_cert(host, hostname, fingerprint);
  return TOFU_NEW_HOSTNAME;
}


int tofu_save_cert(struct known_host **host, char *hostname, char *fingerprint) {
  char hosts_path[PATH_MAX + 1];
  get_file_path_in_data_dir(host_filename, hosts_path, sizeof(hosts_path));  

  FILE *f = fopen(hosts_path, "a");
  if(f == NULL)
    ERROR_LOG_AND_EXIT("Can't open \"%s\"", hosts_path);  

  // i use SHA-512
  fprintf(f, "%s %s\n", hostname, fingerprint);
  fclose(f);

  struct known_host *tmp_host = (struct known_host*) calloc(1, sizeof(struct known_host));
  tmp_host->hostname = strdup(hostname);
  tmp_host->fingerprint = strdup(fingerprint);
  tmp_host->next = *host;
  *host = tmp_host;
  return 1;
}

void tofu_change_cert(struct known_host *host, char *hostname_with_portn, char *new_fingerprint) {
  char hosts_path[PATH_MAX + 1];
  get_file_path_in_data_dir(host_filename, hosts_path, sizeof(hosts_path));  

  FILE *f = fopen(hosts_path, "r+");
  if(f == NULL)
    ERROR_LOG_AND_EXIT("Can't open \"%s\" file", hosts_path);  

  char *line = NULL;
  size_t n = 0;
  size_t hostname_with_portn_len = strlen(hostname_with_portn);;
  ssize_t line_len;
  int found_hostname = 0;

//  INFO_LOG("hostname_with_portn: %s\n", hostname_with_portn);

  while((line_len = getline(&line, &n, f)) != -1) {
//    INFO_LOG("line: %s\n", line);
    if(strncmp(line, hostname_with_portn, hostname_with_portn_len) == 0) {
      fseek(f, -line_len + hostname_with_portn_len + 1, SEEK_CUR);
      fprintf(f, "%s", new_fingerprint);
      found_hostname = 1;
      break;
    }
  }

  fclose(f);
  assert(found_hostname);

  while(host && strcmp(hostname_with_portn, host->hostname) != 0) 
    host = host->next;

  assert(host);
  strcpy(host->fingerprint, new_fingerprint);
  free(line);
}

int tofu_load_certs(struct known_host **host) {
  char hosts_path[PATH_MAX + 1];
  get_file_path_in_data_dir(host_filename, hosts_path, sizeof(hosts_path));  
  
  FILE *f = fopen(hosts_path, "a+");
  if(f == NULL)
    ERROR_LOG_AND_EXIT("Can't open \"%s\" file", hosts_path);  
    
  size_t n = 0;
  int lineno = 1;
  char *line = NULL;
  ssize_t ln = 0;

  while((ln = getline(&line, &n, f)) != -1) {
    if(line[ln - 1] == '\n') {
      line[ln - 1] = '\0';
    }

    struct known_host *tmp_host = calloc(1, sizeof(struct known_host));
    if(tmp_host == NULL)
      MALLOC_ERROR;

    char *tok = strtok(line, " ");
    assert(tok);
    tmp_host->hostname = strdup(tok);
//
//    tok = strtok(NULL, " ");
//    assert(tok);
//    if(strcmp(tok, "SHA-512") != 0) {
//      free(tmp_host->hostname);
//      free(tmp_host);
//      continue;
//    }

    tok = strtok(NULL, " ");
    assert(tok);
    tmp_host->fingerprint = strdup(tok);
    tmp_host->lineno = lineno++;
    tmp_host->next = *host;
    
    *host = tmp_host;
  }

  // assert(*host);
  if (line)
    free(line);
  return 1;
}
