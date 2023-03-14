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

enum mime_error { MIME_ERROR_NONE = 0, MIME_ERROR_NOT_UTF8 = 1, MIME_ERROR_TOO_LONG = 2, MIME_ERROR_NO_MIME = 3, MIME_ERROR_MORE_THAN_ONE_SPACE = 4, MIME_ERROR_NO_SPACE_AFTER_STATUS = 5};

int get_valid_query(char **query);
int get_default_app(char *mime_type, char default_app[NAME_MAX + 1]);
enum mime_error get_mime_type(char *str, char **mime);
int open_file(char *buf, char *filename, char *app, int size, int offset);
int save_file(char save_path[PATH_MAX + 1], char *buf, char *filename, int size, int offset);
int save_gemsite(char save_path[PATH_MAX + 1], char *url, struct response *resp);
void open_link(char *link);

void free_char_pp(char **p, int n);

#endif
