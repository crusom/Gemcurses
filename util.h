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

int get_valid_query(char **query);
int get_default_app(char *mime_type, char default_app[NAME_MAX + 1]);
char *get_mime_type(char *str);
int open_file(char *buf, char *filename, char *app, int size, int offset);
int save_file(char save_path[PATH_MAX + 1], char *buf, char *filename, int size, int offset);
void save_gemsite(char *url, struct response *resp);
void open_link(char *link);

void free_char_pp(char **p, int n);

#endif
