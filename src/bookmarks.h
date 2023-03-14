#ifndef GEMINI_BOOKMARKS_H
#define GEMINI_BOOKMARKS_H

char **load_bookmarks(int *num_of_links);
int add_bookmark(char *str);
int delete_bookmark(char *str);
int is_bookmark_saved(char **bookmarks_saved, int n, char *str);
#endif
