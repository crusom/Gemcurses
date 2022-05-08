#include "util.h"

char percentage_encode_chars[][4] = {
  ":", "%3A",
  "/", "%2F",
  "?", "%3F",
  "#", "%23",
  "[", "%5B",
  "]", "%5D",
  "@", "%40",
  "!", "%21",
  "$", "%24",
  "&", "%26",
  "'", "%27",
  "(", "%28",
  ")", "%29",
  "*", "%2A",
  "+", "%2B",
  ",", "%2C",
  ";", "%3B",
  "=", "%3D",
  "%", "%25",
  "  ", "%20"
};


int get_valid_query(char **query) {
  
  if(query == NULL || *query == NULL || **query == '\0')
    return 0;

  int query_size = strlen(*query);
  int i = 0;

  while(i < query_size) {
    char c = (*query)[i];
    if(isalpha(c) || isdigit(c)) {
      i++;
      continue;
    }
 
    static int len = sizeof(percentage_encode_chars)/sizeof(percentage_encode_chars[0]);
    bool was_found = false;
    for(int j = 0; j < len; j += 2) {
      if(c == percentage_encode_chars[j][0]) {
        query_size += 2;
        *query = realloc(*query, query_size + 1);

        memmove(*query + i + 3, *query + i + 1, query_size - i - 2);
        (*query)[i]     = percentage_encode_chars[j + 1][0];
        (*query)[i + 1] = percentage_encode_chars[j + 1][1];
        (*query)[i + 2] = percentage_encode_chars[j + 1][2];
        i += 3;
        was_found = true;
        break;
      }
    };
    
    if(!was_found)
      return 0;
  }
  return 1;
}

char *get_mime_type(char *str) {
  if(str == NULL)
    return NULL;
  
  for(int i = 0; i < 3; i++) {
    str++;
    if(*str == '\0')
      return NULL;
  }

  char *p = str;
  int len = 0;
  while(*str && !isspace(*str) && *str != ';'){ 
    len++;
    str++;
  }

  if(len == 0)
    return NULL;

  char *res = malloc(len + 1);
  if(res == NULL) {
    MALLOC_ERROR();
  }

  strncpy(res, p, len);
  res[len] = '\0';

  return res; 
}

int get_default_app(char *mime_type, char default_app[NAME_MAX + 1]) {
  char buf[NAME_MAX + 1];
  char cache_path[PATH_MAX + 1];
  FILE *f = NULL;
  pid_t pid;

  char *pwd = getenv("PWD");
  // no pwd? :/
  if(pwd == NULL)
    goto err;

  strcpy(cache_path, pwd);
  // add a new cache dir if it doesn't exist yes
  struct stat st;
  strcat(cache_path, "/.cache/");
  if(stat(cache_path, &st) == -1)
    mkdir(cache_path, 0700);

  strcat(cache_path, "default_app.txt");
  remove(cache_path);

  pid = fork();
  if(pid == 0) {
    int fd = open(cache_path, O_CREAT | O_WRONLY, 0755);
    int null_fd = open("/dev/null", O_WRONLY);
    dup2(fd, 1);
    dup2(null_fd, 2);
    execlp("xdg-mime", "xdg-mime", "query", "default", mime_type, (char *)0);
    exit(1);
  }
  else {
    int status;
    waitpid(pid ,&status, 0);
    // Open cache file to read output
    f = fopen(cache_path, "r");
    if(f == NULL)
      goto err;

    fseek(f, 0L, SEEK_END);
    int file_size = ftell(f);
    fseek(f, 0L, SEEK_SET);

    // something terrible happened
    if(file_size > NAME_MAX)
      goto err;

    if(fgets(buf, NAME_MAX, (FILE *)f) == NULL)
      goto err;


    buf[NAME_MAX] = '\0';

    int len = strlen(buf);
    if(len == 0)
      goto err;
    
    char *p = buf;
    for(;;) {
      if(*p == '\0')
        goto err;
      if(*p == '.')
        break;
      ++p;
    }

    *p = '\0';
    if(p == buf)
      goto err;

    strcpy(default_app, buf);
  }
  
  fclose(f);
  return 1;

err:
  if(f)
    fclose(f);
  return 0;
}

char *get_cache_path(char cache_path[PATH_MAX + 1]) {

  char *home = getenv("HOME");
  // no home? :/
  if(home == NULL)
    return NULL;

  strcpy(cache_path, home);
  // add a new cache dir if it doesn't exist yes
  struct stat st;
  strcat(cache_path, "/.cache/");
  if(stat(cache_path, &st) == -1)
    mkdir(cache_path, 0700);
  
  return cache_path;
}

int get_downloads_path(char downloads_dir[]) {
  char buf[PATH_MAX + 1];
  char cache_path[PATH_MAX + 1];
  FILE *f = NULL;
  pid_t pid;

  get_cache_path(cache_path);

  strcat(cache_path, "default_download_path.txt");
  remove(cache_path);
  pid = fork();
  if(pid == 0) {
    int fd = open(cache_path, O_CREAT | O_WRONLY, 0755);
    int null_fd = open("/dev/null", O_WRONLY);
    dup2(fd, 1);
    dup2(null_fd, 2);
    execlp("xdg-user-dir", "xdg-user-dir", "DOWNLOAD", (char *)0);
    exit(1);
  }
  else {
    int status;
    waitpid(pid ,&status, 0);
    // Open cache file to read output
    f = fopen(cache_path, "r");
    if(f == NULL)
      goto err;

    fseek(f, 0L, SEEK_END);
    int file_size = ftell(f);
    fseek(f, 0L, SEEK_SET);

    // something terrible happened
    if(file_size > PATH_MAX + 1 || file_size < 2)
      goto err;

    if(fgets(buf, PATH_MAX + 1, (FILE *)f) == NULL)
      goto err;
  
    buf[file_size - 1] = '\0';

    int len = strlen(buf);
    if(len == 0)
      goto err;

    strcpy(downloads_dir, buf);
    strcat(downloads_dir, "/");
  }
  return 1;

err:
    if(f)
      fclose(f);
    return 0;
}


void write_file(char *buf, char *save_path, int size, int offset) {
  FILE *f = fopen(save_path, "wb");
  if(f == NULL) {
    MALLOC_ERROR();
  }
  
  fwrite(buf + offset, sizeof(char), size - offset, f);
  fclose(f);
}

void exec_app(char *app, char *path) {

  pid_t pid;
  pid = fork();
  if(pid == 0) {
    execlp(app, app, path, (char *)0);
    exit(EXIT_SUCCESS);
  }
}

int save_file(char save_path[PATH_MAX + 1], char *buf, char *filename, int size, int offset) {

  get_downloads_path(save_path);
  strcat(save_path, filename);  
  write_file(buf, save_path, size, offset);

  return 1;
}


int open_file(char *buf, char *filename, char *app, int size, int offset) {
  
  char save_path[PATH_MAX + 1];
  get_cache_path(save_path);
  strcat(save_path, filename);
  write_file(buf, save_path, size, offset);

  exec_app(app, save_path);
  return 1;
}
