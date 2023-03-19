#ifndef GEMINI_UTIL_H
#define GEMINI_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>

#include "tls.h"

#define _LOG(format, ...) { \
  char buf[80]; \
  get_datatime(buf, sizeof(buf)); \
  fprintf(stderr, "%s: " format, buf, ##__VA_ARGS__); \
  fflush(stderr); \
}

#define INFO_LOG(format, ...) do { _LOG("INFO: " format, ##__VA_ARGS__); } while(0)

#define ERROR_LOG(format, ...) do { _LOG("ERROR: " format, ##__VA_ARGS__); } while(0)
#define ERROR_LOG_AND_ABORT(format, ...) do { _LOG("ERROR: " format, ##__VA_ARGS__); abort(); } while(0)
#define ERROR_LOG_AND_EXIT(format, ...) do { _LOG("ERROR: " format, ##__VA_ARGS__); exit(EXIT_FAILURE); } while(0)

#define MALLOC_ERROR ERROR_LOG_AND_ABORT("MALLOC ERROR (line: %d, file: %s, func: %s)\n", __LINE__, __FILE__, __func__)

enum mime_error { MIME_ERROR_NONE = 0, MIME_ERROR_NOT_UTF8 = 1, MIME_ERROR_TOO_LONG = 2, MIME_ERROR_NO_MIME = 3, MIME_ERROR_MORE_THAN_ONE_SPACE = 4, MIME_ERROR_NO_SPACE_AFTER_STATUS = 5};

void get_datatime(char *buf, int size);
int get_valid_query(char **query);
int get_default_app(char *mime_type, char default_app[NAME_MAX + 1]);
enum mime_error get_mime_type(char *str, char **mime);
int open_file(char *buf, char *filename, char *app, int size, int offset);
int save_file(char save_path[PATH_MAX + 1], char *buf, char *filename, int size, int offset);
int save_gemsite(char save_path[PATH_MAX + 1], char *url, struct response *resp);
void open_link(char *link);

void free_char_pp(char **p, int n);

#endif
