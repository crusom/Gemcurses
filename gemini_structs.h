#ifndef GEMINI_STRUCTS_H
#define GEMINI_STRUCTS_H
#include <stdbool.h>

struct screen_line {
  char *text;
  char *link;
  unsigned int attr;
};

struct gemini_site {
  char *url;
  struct screen_line **lines;
  int lines_num;
  int first_line_index, last_line_index, selected_link_index;
  bool is_bookmarked;
};


#endif
