#ifndef PAGE_H
#define PAGE_H
#include <stdbool.h>
#include <stddef.h>

struct screen_line {
  char *text;
  char *link;
  unsigned int attr;
};

struct page_t {
  char *url;
  struct screen_line **lines;
  int lines_num;
  int first_line_index, last_line_index, selected_link_index;
  bool is_bookmarked;
};


#endif
