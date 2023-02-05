#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

#include "bookmarks.h"
#include "page.h"

static const char bookmarks_filename[] = "bookmarks";

char **load_bookmarks(int *num_of_links) {
  FILE *f = fopen(bookmarks_filename, "r");  
  if(f == NULL) return NULL;

  char **bookmarks = malloc(sizeof(char*));

  size_t n = 0;
  int lineno = 0;
  char *line = NULL;

  while(getline(&line, &n, f) != -1) {
    lineno++;
    int ln = strlen(line);
    if(line[ln - 1] == '\n') {
      line[ln - 1] = '\0';
    }
    bookmarks = realloc(bookmarks, lineno * sizeof(char*));
    bookmarks[lineno - 1] = strdup(line);
  }
  free(line);
  fclose(f);
  *num_of_links = lineno;
  return bookmarks;
}

int is_bookmark_saved(char **bookmarks_saved, int n, char *str) {
  if(strncmp(str, "gemini://", 9) == 0) str += 9;
  for(int i = 0; i < n; i++)
    if(strcmp(bookmarks_saved[i], str) == 0)
      return i;
  return -1;
}

int add_bookmark(char *str) {
  FILE *f = fopen(bookmarks_filename, "a");
  if(f == NULL) return 0;
  fprintf(f, "%s\n", str);
  fclose(f);
  return 1;
}

int delete_bookmark(char *str) {
  char buffer[1024];
  const char tmp_bookmarks_filename[] = "tmp_bookmarks_file";
  
  FILE *f = fopen(bookmarks_filename, "r");
  if(!f) return 0;
  FILE *f_tmp = fopen(tmp_bookmarks_filename, "w");
  if(!f_tmp) return 0;
  
  bool found = false;
  int num_lines = 0;
  int len = strlen(str);

  while((fgets(buffer, 1024, f)) != NULL) {
    if(found == true || strncmp(buffer, str, len) != 0) {
      fputs(buffer, f_tmp);
      num_lines++;
    }
    else
      found = true;
  }
  
  fclose(f);
  fclose(f_tmp);
  remove(bookmarks_filename);
  if(num_lines == 0) {
    remove(bookmarks_filename);
    remove(tmp_bookmarks_filename);
  }
  else
    rename(tmp_bookmarks_filename, bookmarks_filename);
  return 1;
}

