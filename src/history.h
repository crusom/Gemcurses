#ifndef MY_HISTORY_H
#define MY_HISTORY_H

#include "page.h"
#include "tls.h"

struct history_list_t {
  struct page_t page;
  struct response resp;
  struct history_list_t *next, *prev;
};

struct history_list_t *add_next_node(struct history_list_t *list, struct history_list_t *node);

#endif
