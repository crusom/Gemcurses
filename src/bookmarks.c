#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

#include "bookmarks.h"
#include "page.h"
#include "util.h"

static const char bookmarks_filename[] = "bookmarks";

char **load_bookmarks(int *num_of_links) {
  char bookmarks_path[PATH_MAX + 1];
  get_file_path_in_data_dir(bookmarks_filename, bookmarks_path, sizeof(bookmarks_path));

  FILE *f = fopen(bookmarks_path, "r");  
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
  char bookmarks_path[PATH_MAX + 1];
  get_file_path_in_data_dir(bookmarks_filename, bookmarks_path, sizeof(bookmarks_path));
  FILE *f = fopen(bookmarks_path, "a");
  if(f == NULL) return 0;
  fprintf(f, "%s\n", str);
  fclose(f);
  return 1;
}

int delete_bookmark(char *str) {
  char tmp_bookmarks_path[PATH_MAX + 1], bookmarks_path[PATH_MAX + 1];
  get_file_path_in_cache_dir("tmp_bookmarks", tmp_bookmarks_path, sizeof(tmp_bookmarks_path));
  get_file_path_in_data_dir(bookmarks_filename, bookmarks_path, sizeof(bookmarks_path));
  
  FILE *f = fopen(bookmarks_path, "r");
  if(!f) return 0;
  FILE *f_tmp = fopen(tmp_bookmarks_path, "w");
  if(!f_tmp) return 0;
  
  bool found = false;
  int num_lines = 0;
  int len = strlen(str);

  char buffer[1024];
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
//  remove(bookmarks_path);
  if(num_lines == 0) {
    remove(bookmarks_path);
    remove(tmp_bookmarks_path);
  }
  else
    rename(tmp_bookmarks_path, bookmarks_path);
  return 1;
}

