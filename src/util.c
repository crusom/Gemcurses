#include "util.h"
#include <time.h>

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
  " ", "%20",
};

void get_datatime(char buf[], int size) {
  time_t now = time(0);
  struct tm tstruct = *localtime(&now);
  strftime(buf, size, "%Y-%m-%d %X", &tstruct);
}

int get_valid_query(char **query) {
  
  if(query == NULL || *query == NULL || **query == '\0')
    return 0;

  int query_size = strlen(*query);
  int i = 0;

  while(i < query_size) {
    char c = (*query)[i];
    if(isalpha(c) || isdigit(c) ||
           c == '-' || c == '_' || 
           c == '.' || c == '~') {
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


void open_link(char *link) {
  pid_t pid = fork();
  if(pid == 0) {
    int null_fd = open("/dev/null", O_WRONLY);
    dup2(null_fd, 2);
    dup2(null_fd, 2);
    
    execlp("xdg-open", "xdg-open", link, (char *)0);
    exit(1);
  }
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
    int path_length = ftell(f);
    fseek(f, 0L, SEEK_SET);

    // something terrible happened
    if(path_length > NAME_MAX)
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

void get_cache_path(char *cache_path, int size) {
  char *xdg_cache_home = getenv("XDG_CACHE_HOME");
  if(xdg_cache_home) {
    if(snprintf(cache_path, size, "%s/%s", xdg_cache_home, ".cache/gemcurses") > size)
      ERROR_LOG_AND_EXIT("XDG_DATA_HOME directory is too long\n");
    goto create_dir; 
  }
  char *home = getenv("HOME");
  // no home? :/
  // ok let's try old good /tmp
  if(home == NULL) {
    INFO_LOG("No $HOME environment variable, using /tmp instead");
    home = "/tmp";
  }

  if(snprintf(cache_path, size, "%s/%s", home, ".cache/gemcurses") > size)
    ERROR_LOG_AND_EXIT("HOME directory is too long\n");
  
create_dir:;
  // add a new cache dir if it doesn't exist yes
  struct stat st;
  if(stat(cache_path, &st) == -1)
    mkdir(cache_path, 0700);
  
  return;
}


void get_data_path(char data_path[], int size) {
  char *xdg_data_home = getenv("XDG_DATA_HOME");
  if(xdg_data_home) {
    if(snprintf(data_path, size, "%s/%s", xdg_data_home, ".local/share/gemcurses") > size)
      ERROR_LOG_AND_EXIT("XDG_DATA_HOME directory is too long\n");
    return;
  } 

  char *home = getenv("HOME");
  if(home == NULL)
    ERROR_LOG_AND_EXIT("No $HOME environment variable!\n");

  if(snprintf(data_path, size, "%s/%s", home, ".local/share/gemcurses") > size)
    ERROR_LOG_AND_EXIT("HOME directory is too long\n");
    
  // add a new cache dir if it doesn't exist yes
  struct stat st;
  if(stat(data_path, &st) == -1)
    mkdir(data_path, 0700);
  
  return;
}
void get_file_path_in_data_dir(const char *filename, char buffer[], int size) {
  char data_path[PATH_MAX - 40];
  int bytes_written = 0; 
  get_data_path(data_path, sizeof(data_path));
  bytes_written = snprintf(buffer, size, "%s/%s", data_path, filename);
  if(bytes_written < 0 || bytes_written > size) 
    ERROR_LOG_AND_EXIT("Path to %s in data dir is too long\n", filename);
}

void get_file_path_in_cache_dir(const char *filename, char buffer[], int size) {
  char cache_path[PATH_MAX - 40];
  int bytes_written = 0; 
  get_cache_path(cache_path, sizeof(cache_path));
  bytes_written = snprintf(buffer, size, "%s/%s", cache_path, filename);
  if(bytes_written < 0 || bytes_written > size) 
    ERROR_LOG_AND_EXIT("Path to %s in cache dir is too long\n", filename);
}

int get_downloads_path(char downloads_dir[]) {
  char buf[PATH_MAX + 1];
  char cache_path[PATH_MAX + 1];
  FILE *f = NULL;
  pid_t pid;

  get_cache_path(cache_path, sizeof(cache_path));

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
      ERROR_LOG_AND_EXIT("Can't open %s for reading\n", cache_path);

    fseek(f, 0L, SEEK_END);
    int path_length = ftell(f);
    fseek(f, 0L, SEEK_SET);

    // something terrible happened
    if(path_length > PATH_MAX + 1 || path_length < 2) {
      INFO_LOG("Download path too long");
      goto err;
    }

    if(fgets(buf, PATH_MAX + 1, (FILE *)f) == NULL) {
      INFO_LOG("Can't read download path");
      goto err;
    }
  
    buf[path_length - 1] = '\0';

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
  if(f == NULL) 
    ERROR_LOG_AND_EXIT("ERROR: cant open save_path\n");
  
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
  char *env = getenv("GEMCURSES_SAVE_PATH");  
  if(env == NULL) {
    if(!get_downloads_path(save_path))
      return 0;
  }
  else
    strcpy(save_path, env);
  
  strcat(save_path, filename);  
  write_file(buf, save_path, size, offset);

  return 1;
}


int open_file(char *buf, char *filename, char *app, int size, int offset) {
  
  char save_path[PATH_MAX + 1];
  get_cache_path(save_path, sizeof(save_path));
  strcat(save_path, filename);
  write_file(buf, save_path, size, offset);

  exec_app(app, save_path);
  return 1;
}

inline void free_char_pp(char **p, int n) {
  for(int i = 0; i < n; i++)
    free(p[i]);
}

int save_gemsite(char save_path[PATH_MAX + 1], int buf_size, char *url, struct response *resp) {
  if(resp->status_code != CODE_SUCCESS || resp->body == NULL) return 0;
  // get the pwd, and iterate over the url, to copy the gemsite into the right directory
  // eg. gem.saayaa.space/gemlog/2022-09-12-like-a-bike.gmi to:
  // ./saved/gem_saayaa_space/gemlog/2022-09-12-like-a-bike.gmi
  char data_path[PATH_MAX - 20];
  get_data_path(data_path, sizeof(data_path));
  
  if(strncmp(url, "gemini://", 9) == 0) url += 9;

  if((ssize_t)strlen(data_path) + (ssize_t)strlen(url) >= buf_size) {
    ERROR_LOG("Url (%s) and data_path (%s) are longer than PATH_MAX\n", url, data_path);
    return 0;
  }

  struct stat st;
  strcpy(save_path, data_path);
  strcat(save_path, "/saved/");
  if(stat(save_path, &st) == -1)
    mkdir(save_path, 0700);

  char *dir = strchr(url, '/');
  assert(dir);

  int count_dots = 0;
  for(int i = 0; i < dir - url; i++) {
    if(url[i] == '.') count_dots++;
  } 
  // if we have 2 dots in domainname then there's a subdomain
  // so create a directory of domainname and a subdomain in it
  if(count_dots == 2) {
    char *dot = strchr(url, '.');
    dot++;
    strncat(save_path, dot, dir - dot);
  }
  else {
    strncat(save_path, url, dir - url); 
  }
  if(stat(save_path, &st) == -1)
    mkdir(save_path, 0700);

  char *dir_end;
  while((dir_end = strchr(dir, '/'))) {
    strncat(save_path, dir, dir_end - dir + 1);
    if(stat(save_path, &st) == -1)
      mkdir(save_path, 0700);
    
    dir = dir_end;
    dir++;
  }
  if(*dir == 0) 
    strcat(save_path, "MAIN.gmi"); 
  else
    strcat(save_path, dir);
  
  // add timestamp at the end, to allow to save same page multiple times, and know when each shoot was done
  time_t my_time;
  struct tm *timeinfo; 
  time (&my_time);
  timeinfo = localtime (&my_time);
 
  char timestamp[200];
  // i want this format, because i dont like escape characters
  snprintf (timestamp, 100, "-%d-%d-%d--%d-%d", 
    timeinfo->tm_mday, timeinfo->tm_mon+1, timeinfo->tm_year+1900,
    timeinfo->tm_hour, timeinfo->tm_min
  );
  
  strcat(save_path, timestamp);

  write_file(resp->body, save_path, resp->body_size, 0);
  INFO_LOG("saved gemsite: %s\n", save_path);

  return 1;
}
