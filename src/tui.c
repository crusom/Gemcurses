#include <ncurses.h>
#include <form.h>
#include <panel.h>
#include <string.h>
#include <locale.h>
#include <sys/types.h>
#include <dirent.h>
#include <wctype.h>
#include <errno.h>

#include "tls.h"
#include "page.h"
#include "bookmarks.h"
#include "util.h"
#include "utf8.h"

#define SAVED_DIR "saved/"
#define MAIN_GEM_SITE "warmedal.se/~antenna/"
#define set_main_win_x(max_x) main_win_x = max_x - offset_x - 1
#define set_main_win_y(max_y) main_win_y = max_y - search_bar_height - info_bar_height - 1 - 1

typedef void (*println_func_def) (WINDOW *, struct screen_line*, int x, int y);

WINDOW *main_win, *search_bar_win, *info_bar_win, *mode_win, *isbookmarked_win;
PANEL  *dialog_panel; 
WINDOW *dialog_win, *dialog_subwin, *dialog_title_win;
FIELD  *search_field[3];
FORM   *search_form;

const int search_bar_height = 1;
const int info_bar_height = 1;
const int scrolling_velocity = 2;
const int offset_x = 1;

int max_x, max_y;
int main_win_x, main_win_y;

int dialog_win_x, dialog_win_y;
int dialog_subwin_x, dialog_subwin_y;
char info_message[1000];
char *dialog_message = NULL;

char **bookmarks_links = NULL;
int num_bookmarks_links = 0;

enum element {SEARCH_FORM, MAIN_WINDOW, BOOKMARKS_DIALOG};
enum element current_focus = MAIN_WINDOW;

enum mode {SCROLL_MODE, LINKS_MODE};
enum mode current_mode = SCROLL_MODE;
bool is_offline = false;

enum color {LINK_COLOR = 1, H1_COLOR = 2, H2_COLOR = 3, H3_COLOR = 4, QUOTE_COLOR = 5, DIALOG_COLOR = 6};

enum protocols {HTTPS, HTTP, GOPHER, MAIL, FINGER, SPARTAN, GEMINI, null};
enum dialog_types {BOOKMARKS, INFO, OFFLINE} current_dialog_type;

const char *protocols_strings[] = {
  [HTTPS] = " [https]",
  [HTTP] = " [http]",
  [GOPHER] = " [gopher]",
  [MAIL] = " [mail]",
  [FINGER] = " [finger]",
  [SPARTAN] = " [spartan]",
};

const char yes_no_options[] = {'y', 'n', '\0'};
bool is_dialog_hidden = true;

struct page_t bookmarks;
struct page_t offline;
struct page_t offline_dirs;

char offline_path[PATH_MAX + 1] = {0};

// TODO ?
//struct gemini_history {
//  struct {
//    struct page_t **gem_sites;
//    int index;
//    int size;
//  } content_cache;
//  
//  struct {
//    char **urls;
//    int size;
//  } url_history;
//};

//struct history_list_t {
//  struct {
//    struct page_t page;
//    char *body;
//  } *current_page, *next_page, *prev_page;
//} history_list;

struct history_list_t *history_list;

static void refresh_windows() {
  if(wnoutrefresh(info_bar_win)     == ERR) goto err;
  if(wnoutrefresh(mode_win)         == ERR) goto err;
  if(wnoutrefresh(main_win)         == ERR) goto err;
  if(wnoutrefresh(isbookmarked_win) == ERR) goto err;
  if(wnoutrefresh(search_bar_win)   == ERR) goto err;

  doupdate();
  return;

err: 
  fprintf(stderr, "%s", "Can't refresh window\n");
  exit(EXIT_FAILURE);
}

// ########## INITIALIZATION ##########

static void init_colors() {
  start_color();

  // '#'
  init_pair(H1_COLOR, COLOR_WHITE, COLOR_BLACK); 
  // '##'
  init_pair(H2_COLOR, COLOR_BLUE, COLOR_BLACK); 
  // '###'
  init_pair(H3_COLOR, COLOR_RED, COLOR_BLACK); 
  // '=>'
  init_pair(LINK_COLOR, COLOR_YELLOW, COLOR_BLACK);
  init_pair(QUOTE_COLOR, COLOR_BLACK, COLOR_WHITE);
  // dialog borders
  init_pair(DIALOG_COLOR, COLOR_RED, COLOR_BLACK);
}

static void init_dialog_panel() {
  dialog_win_x = max_x * (6.0 / 8.0);
  dialog_win_y = max_y * (6.0 / 8.0);

  dialog_subwin_x = dialog_win_x - 2 - 1;
  dialog_subwin_y = dialog_win_y - 2 - 2;

  dialog_win       = newwin(dialog_win_y, dialog_win_x, max_y / 8, max_x / 8);
  dialog_subwin    = derwin(dialog_win, dialog_subwin_y, dialog_win_x - 2, 1 + 2, 1);
  dialog_title_win = derwin(dialog_win, 2, dialog_win_x - 2, 1, 1);

  box(dialog_win, 0, 0);
  keypad(dialog_win, true);

  dialog_panel = new_panel(dialog_win);
  is_dialog_hidden = true;
  hide_panel(dialog_panel);
  update_panels();
  doupdate();
}

static void init_windows() {
  initscr();
  keypad(stdscr, true); 
  mouseinterval(0);
  cbreak();
  noecho();
  curs_set(0);
  getmaxyx(stdscr, max_y, max_x);
  set_main_win_x(max_x);
  set_main_win_y(max_y);
  
  main_win = newwin(main_win_y, max_x, search_bar_height + 1, 0);
  scrollok(main_win, true);
  keypad(main_win, true);
  
  search_bar_win = newwin(search_bar_height, max_x - 2, 0, 0);
  keypad(search_bar_win, true);
  isbookmarked_win = newwin(1, 1, 0, max_x - 1);
  
  info_bar_win = newwin(info_bar_height, max_x - 6, max_y - 1, 0);  
  mode_win = newwin(1, 5, max_y - 1, max_x - 5);

  init_colors();
  refresh();  
}

static void init_search_form(bool resize) {
  // search_field[0] is a static field so we need to create it only once 
  if(!resize) {
    // coords are relative to the window
    // [0]
    search_field[0] = new_field(search_bar_height, 6, 0, 0, 0, 0);
    field_opts_off(search_field[0], O_EDIT);
    set_field_opts(search_field[0], O_VISIBLE | O_PUBLIC | O_AUTOSKIP);
    set_field_buffer(search_field[0], 0, "url:");
    // [2]
    search_field[2] = NULL;
  }
  // [1]
  search_field[1] = new_field(search_bar_height, max_x - 6 - 2, 0, 6, 0, 0);
  
  set_field_back(search_field[1], A_UNDERLINE);
  set_field_opts(search_field[1], O_VISIBLE | O_PUBLIC | O_ACTIVE | O_EDIT);
  // 1024 is max url length
  set_max_field(search_field[1], 1024);
  field_opts_off(search_field[1], O_STATIC);

  search_form = new_form(search_field);
  set_form_win(search_form, search_bar_win);
  set_form_sub(search_form, derwin(search_bar_win, search_bar_height, max_x - 2, 0, 0));
  post_form(search_form);
}

// ########## FREEING ##########

void free_windows() {
  if(search_form) {
    unpost_form(search_form);
    free_form(search_form);
  }
  if(search_field[0])
    free_field(search_field[0]);
  if(search_field[1])  
    free_field(search_field[1]); 
  
  endwin();
}

static void free_lines(struct page_t *page) {
  
  for(int i = 0; i < page->lines_num; i++) {
    if(page->lines[i]) {
      free(page->lines[i]->text);
      
      if(page->lines[i]->link != NULL) {
        char *p = page->lines[i]->link;
        int j = 0;
        free(p);

        // the  next line may have the same link pointer so take care of it
        while(i + j < page->lines_num) {
          if(page->lines[i + j]->link == p) {
            page->lines[i + j]->link = NULL;
            j++;
          }
          else {
            break;
          }
        }
      }

      free(page->lines[i]);
     }
  }
  free(page->lines);
  page->lines = NULL;
}

static void free_paragraphs(char **paragraphs, int paragraphs_num) {
  for(int i = 0; i < paragraphs_num; i++)
    if(paragraphs[i])
      free(paragraphs[i]);
 
  free(paragraphs);
}

static void free_resp(struct response *resp) {
  if(resp != NULL) {
    if(resp->body)
      free(resp->body);
    if(resp->meta)
      free(resp->meta);
    free(resp);
  }
  resp = NULL;
}

// ########## DRAWING ##########

static void draw_borders() {
  mvhline(search_bar_height, 0, 0, max_x);
  mvhline(max_y - 2, 0, 0, max_x);
  mvvline(max_y - 1, max_x - 6, 0, 1);
  mvvline(0, max_x - 2, 0, 1);
}

static void draw_scrollbar(WINDOW *win, struct page_t *page, int page_y, int page_x) {
  if(page->lines_num <= 0) return;

  double y;
  double scrollbar_height = (double)((page_y) * (page_y)) / (double)page->lines_num;
  if(scrollbar_height < 1.0)
    scrollbar_height = 1.0;

  if(page->first_line_index == 0) {
    y = 0;
  } 
  else if(page->last_line_index == page->lines_num) {
    if(scrollbar_height != 1.0)
      y = (double)(page_y + 1 - scrollbar_height);
    else
      y = (double)(page_y - scrollbar_height);
  }
  else {
    y = (double)(page->first_line_index + 1) / (double)page->lines_num;
    y = page_y * y;
    y = (int)(y + 0.5);
  }

  mvwvline(win, 0, page_x, ' ', page_y);
  mvwvline(win, y, page_x, 0, scrollbar_height);
}

// ########## STRINGS ##########

static char *skip_whitespaces(char *str) {
  while(isspace(*str))
    str++;
  return str;
}

static char *trim_whitespaces(char *str) {
  char *end;

  skip_whitespaces(str);
  
  if(*str == 0)
    return str;

  end = str + strlen(str) - 1;

  while(end > str && isspace(*end))
    end--;

  *(end + 1) = '\0';
  return str;
}

static inline int m_strncmp(char *a, const char *b) {
  return strncmp(a, b, strlen(b));
}

// we get a string and we cut it to equal parts, which are terminal width
// so every line is indepenent 
static char **string_to_paragraphs(char *str, int *paragraphs_num) {
  int i = 0, num_newlines = 0;
  // count new paragraphs to know how many gemini_paragraph to allocate
  while(str[i] != '\0') {
    if(str[i] == '\n') {
      num_newlines++;
    }
    i++;
  }
  
  char **paragraphs = (char **) calloc(1, num_newlines * sizeof(char*));

  char *str_tmp = str, *str_start = str;
  int index_line = 0;
  // fill paragraphs array
  while(*str_tmp != '\0') {
    if(*str_tmp == '\n') {
      paragraphs[index_line] = strndup(str_start, str_tmp - str_start);
      index_line++;
      str_start = str_tmp + 1;
    }
    str_tmp++;
  }

  // remainder paragraph if the body doesn't end with \n
  if(str_tmp > str_start) {
    num_newlines++; 
    paragraphs = realloc(paragraphs, num_newlines * sizeof(char*));
    paragraphs[index_line] = strndup(str_start, str_tmp - str_start);
  }

  *paragraphs_num = num_newlines;

  return paragraphs;
}

static enum protocols get_protocol(char *str) {
  
  enum protocols protocol = null;

  if(m_strncmp(str, "gemini://") == 0 || m_strncmp(str, "//") == 0)
    protocol = GEMINI;
  else if(m_strncmp(str, "https://") == 0)
    protocol = HTTPS;
  else if(m_strncmp(str, "http://") == 0)
    protocol = HTTP;
  else if(m_strncmp(str, "gopher://") == 0)
    protocol = GOPHER;
  else if(m_strncmp(str, "mailto:") == 0)
    protocol = MAIL;
  else if(m_strncmp(str, "finger://") == 0)
    protocol = FINGER;
  else if(m_strncmp(str, "spartan://") == 0)
    protocol = SPARTAN;

  return protocol;
}

static int get_paragraph_attr(char **paragraph, char **link, enum protocols *protocol, int *p_offset) {
  
  unsigned int attr = A_NORMAL;
  int offset = 0;
  if(strlen(*paragraph) > 0) {

    if(m_strncmp(*paragraph, "###") == 0) {
      attr |= A_BOLD | A_ITALIC;
      attr |= COLOR_PAIR(H3_COLOR);
      offset = 3;
    }
    else if(m_strncmp(*paragraph, "##") == 0) {
      attr |= A_BOLD | A_ITALIC;
      attr |= COLOR_PAIR(H2_COLOR);
      offset = 2;
    }
    else if(m_strncmp(*paragraph, "#") == 0) {
      attr |= A_BOLD;
      attr |= COLOR_PAIR(H1_COLOR);
      offset = 1;
    }
    else if(m_strncmp(*paragraph, ">") == 0) {
      attr |= A_ITALIC;
      attr |= COLOR_PAIR(QUOTE_COLOR);
      offset = 1;
    }
    else if(m_strncmp(*paragraph, "=>") == 0) {
      
      // Lines beginning with the two characters "=>" are link lines, which have the following syntax:
      // =>[<whitespace>]<URL>[<whitespace><USER-FRIENDLY LINK NAME>]
      attr |= A_UNDERLINE;
      attr |= COLOR_PAIR(LINK_COLOR);
      offset = 2;
      char *paragraph_end = NULL;
      char *link_start = *paragraph + 2;

      // skip the first whitespace
      while(*link_start != '\0' && isspace(*link_start)) {
        link_start++;
        offset++;
      }

      *protocol = get_protocol(link_start);

      // go throught the URL
      paragraph_end = link_start;
      while(*paragraph_end != '\0' && !isspace(*paragraph_end)) {
        paragraph_end++;
        offset++;
      }
      
      // copy the URL
      if(link_start != paragraph_end) {
        char *link_p = (char*) calloc(paragraph_end - link_start + 1, sizeof(char));
        memcpy(link_p, link_start, paragraph_end - link_start);
        link_p[paragraph_end - link_start] = '\0';
        *link = link_p;
      }
      
      // skip whitespace(s) after url
      while(*paragraph_end != '\0' && isspace(*paragraph_end)) {
        paragraph_end++;
        offset++;
      }
     
      // if there is no '<USER_FRIENDLY LINK NAME>' then show the plain URL
      if(*paragraph_end == '\0') {
        *protocol = null;
        offset = link_start - *paragraph;
      }
      else {
        // i found out that for some reasons, some links have
        // a '\r' symbol at the end?
        // so we need to check this
        // i suspect it's windows carriage return fault
        while(*paragraph_end && *paragraph_end != '\r')
          paragraph_end++;
        *paragraph_end = '\0';
      }
    }
  }
  
  *p_offset = offset;

  return attr;
}


static struct screen_line** paragraphs_to_lines(
    struct page_t *page, 
    char **paragraphs, 
    int paragraphs_num, 
    int page_x, 
    // this flag is used in bookmarks
    bool set_links_to_paragraphs
) {
  
  bool is_preformatted_mode = false;
 
  struct screen_line **lines = (struct screen_line**) calloc(1, 1 * sizeof(struct screen_line *));
  int num_lines = 0;
  
  for(int i = 0; i < paragraphs_num; i++) {
    char *line_str = paragraphs[i], *link = NULL;
    enum protocols protocol = null; 
    int offset_begin = 0;
    int attr = A_NORMAL;

    bool is_last_line = false;
    // this flag is used with set_links_to_paragraphs
    // if we're printing bookmarks, then we need to know which part (domain) should be bolded
    // so what we do, is we give attr A_BOLD to everything, that's before the slash mark '/' in a url
    bool make_domain_bold = false;
 
    if(m_strncmp(line_str, "```") == 0)
      is_preformatted_mode = !is_preformatted_mode;
   
    if(set_links_to_paragraphs == false) {
      attr = get_paragraph_attr(&line_str, &link, &protocol, &offset_begin);
    } 
    else {
      link = strdup(paragraphs[i]);
      make_domain_bold = true;
    }
    
    line_str += offset_begin;
     
    // it's a link to non gemini page
    if(protocol != null && protocol != GEMINI) {
      // if it's not a gemini protocol we need to append an information to the end of the link
      // so realloc, add to the end, and set line_str to our reallocated string + offset
      const char *protocol_str = protocols_strings[protocol];
      paragraphs[i] = realloc(paragraphs[i], strlen(paragraphs[i]) + strlen(protocol_str) + 1);
      strcat(paragraphs[i], protocol_str);
      line_str = paragraphs[i] + offset_begin;
    }
    
    // loop over all lines in a paragraph
    do {
      // if at the end of the line we have a splited word
      // then move the whole word to the next line 
      int word_offset = 0;
      //int line_len = utf8_to_bytes(line_str, page_x - offset_x);
      int line_len = utf8_to_bytes(line_str, utf8_max_chars_in_width(line_str, page_x - offset_x));
      
      //if(utf8_max_chars_in_bytes(line_str, strlen(line_str)) <= page_x - offset_x)
      if(utf8_strwidth(line_str) <= page_x - offset_x)
        is_last_line = true;

      // check if word at the end of the line needs to be moved to the next line
      char *line_p = line_str, *last_found_space = NULL;
      if(line_str[line_len] != '\0') {  
        while(line_p != line_str + line_len) {
          line_p += utf8_to_bytes(line_p, 1);
          if(iswspace(get_wchar(line_p)))
            last_found_space = line_p;
        }

        if(last_found_space != NULL) {
          // word offset in *raw bytes*
          word_offset = (line_str + line_len) - last_found_space;
          // if we have one long line then let it be split, otherwise move 
          if(utf8_strnwidth(last_found_space, utf8_max_chars_in_bytes(last_found_space, word_offset)) > (page_x - offset_x) / 2) {
            word_offset = 0;
          }
        }
      }
     
      line_len -= word_offset; 
      
      if(*line_str != '\0' && iswspace(get_wchar(line_str)) && is_preformatted_mode == false) {
        line_str++;
        line_len--;
      }
    
      num_lines++;
      lines = realloc(lines, num_lines * sizeof(struct screen_line*));

      char *text = malloc(line_len + 1);
      memcpy(text, line_str, line_len);
      text[line_len] = '\0';

      struct screen_line *line = (struct screen_line*) calloc(1, 1 * sizeof(struct screen_line));
      
      // it is used for bookmarks_dialog, to know when a domain should be bolded
      if(make_domain_bold) {
        line->attr = A_BOLD;
        if(strchr(text, '/'))
          make_domain_bold = false;
      }
      else {
        line->attr = attr;
      }
      line->text = text;
      line->link = link;
      lines[num_lines - 1] = line;
      
      line_str += line_len;
    } while(!is_last_line);
  }

  page->lines_num = num_lines;
  page->selected_link_index = -1;

  return lines;
}


// ########## PRINTING ##########

static void print_current_mode() {
  werase(mode_win);
  wprintw(mode_win, "%s|%c", 
    (is_offline) ? "off" : "on",
    (current_mode == LINKS_MODE) ? 'L' : 'S'
  );
  wrefresh(mode_win);
}

static void print_is_bookmarked(bool is_bookmarked) {
  werase(isbookmarked_win);
  if(is_bookmarked)
    wprintw(isbookmarked_win, "%s", "★");
  else
    wprintw(isbookmarked_win, "%s", "☆");
}

static void info_bar_print(const char *format, ...) {
  va_list argptr;
  va_start(argptr, format);
  vsnprintf(info_message, sizeof(info_message), format, argptr);
  va_end(argptr);

  werase(info_bar_win);
  wprintw(info_bar_win, "%s", info_message);
  wrefresh(info_bar_win);
}

static void info_bar_clear(void) {
  info_message[0] = 0;
  werase(info_bar_win);
}

static void printline(WINDOW *win, struct screen_line *line, int x, int y) {
  // scrollok scrolls automatically to the next line when 
  // len == max_x so disable it temporarily
  scrollok(win, false);
  wattron(win, line->attr);
  
  if(line->text != NULL)
    mvwprintw(win, y, x, "%s", line->text);
  
  wattroff(win, line->attr);
  scrollok(win, true); 
}

static void print_page(
    struct page_t *page, 
    WINDOW *win, 
    int first_line_index, 
    int page_y,
    println_func_def print_func
    ) {
  scrollok(win, false);
  // TODO if we resized the window too much and the text on down is now on up
  // then go up to make the text be on down
  int index = first_line_index;
  struct screen_line *line;
  if(!print_func)
    print_func = &printline;
  // print all we can
  for(int i = 0; i < page_y; i++){
    line = page->lines[index];
    print_func(win, line, offset_x, i);
    index++;
    if(index >= page->lines_num)
      break;
  }

  page->last_line_index = index;
  page->first_line_index = first_line_index;
  
  scrollok(win, true);
}


static void print_bookmark_line(WINDOW *win, struct screen_line *line, int x, int y) {
  scrollok(win, false);
  wattron(win, line->attr);

  char *domain = NULL;
  char deleted_delimiter = '\0';

  if(line->attr == A_BOLD) {
    domain = strchr(line->text, '/');
    if(domain) {
      deleted_delimiter = *domain;
      *domain = '\0';
    }
  }
 
  if(line->text != NULL) {
    wattron(win, line->attr);
    mvwprintw(win, y, x, "%s", line->text);
    wattroff(win, line->attr);

    if(domain) {
      *domain = deleted_delimiter;
      mvwprintw(win, y, x + domain - line->text, "%s", domain);
    }
  }

  wattroff(win, line->attr);
  scrollok(win, true); 
}

static void scrolldown(
    struct page_t *page, 
    WINDOW *win,
    int page_y, 
    println_func_def print_func
  ) {
  
  if(page->last_line_index >= page->lines_num || page->lines == NULL)
    return;
  
  wscrl(win, 1);

  struct screen_line *line = page->lines[page->last_line_index];
  if(!print_func) print_func = &printline;  
  print_func(win, line, offset_x, page_y - 1);

  page->last_line_index++;
  page->first_line_index++;
}


static void scrollup(
    struct page_t *page, 
    WINDOW *win,
    println_func_def print_func
  ) {
  
  if(page->first_line_index == 0 || page->lines == NULL)
    return;

  wscrl(win, -1);
  
  page->first_line_index--;
  page->last_line_index--;

  struct screen_line *line = page->lines[page->first_line_index];
  if(!print_func) print_func = &printline;  
  print_func(win, line, offset_x, 0);
}


// this function is bulk, i know, but just freeing and initializing everythink again
// has a huge memory and performance impact.
// we need to resize and move windows manually i guess, or make some sort of functions to do what
static void resize_screen(struct page_t *page, struct response *resp) {

  endwin();
  refresh();
  getmaxyx(stdscr, max_y, max_x);

  if(max_y <= 5 || max_x <= 5) {
    fprintf(stderr, "%s", "Too small screen size\n");
    exit(EXIT_FAILURE);
  }  

  set_main_win_x(max_x);
  set_main_win_y(max_y);
  
  // info_bar win
  wresize(info_bar_win, 1, max_x - 6);
  mvwin(info_bar_win, max_y - 1, 0);
  wclear(info_bar_win);
  if(info_message[0])
    wprintw(info_bar_win, "%s", info_message);

  // mode win
  mvwin(mode_win, max_y - 1, max_x - 5);
  print_current_mode();

  // search_bar win
  wresize(search_bar_win, 1, max_x - 2);
  
  // bookmarked win
  mvwin(isbookmarked_win, 0, max_x - 1);
  print_is_bookmarked(page->is_bookmarked);
  
  // main win
  wresize(main_win, main_win_y, max_x);
  draw_borders();
  wclear(main_win);

  // search win
  form_driver(search_form, REQ_VALIDATION);
  char search_str[1024];
  bool is_search_field_str = false;
  if(field_buffer(search_field[1], 0) != NULL){
    strcpy(search_str, trim_whitespaces(field_buffer(search_field[1], 0)));
    is_search_field_str = true;
  }
  
  // form
  // unfortunately there's no way to just resize a form, we need to recreate it
  unpost_form(search_form);
  free_form(search_form);
  free_field(search_field[1]);
  
  init_search_form(true);
  if(is_search_field_str)
    set_field_buffer(search_field[1], 0, search_str);

  // main win
  // set selected link to index -1, to start from the beginning of the page 
  page->selected_link_index = -1;
  // if there was a response, then print it
  if(page->lines != NULL)
    free_lines(page);

  if(!is_offline) { 
    if(resp && resp->body) {
      int paragraphs_num = 0;
      char **paragraphs = string_to_paragraphs(resp->body, &paragraphs_num);
      page->lines = paragraphs_to_lines(
          page, 
          paragraphs, 
          paragraphs_num, 
          main_win_x, 
          false
      );
      
      free_paragraphs(paragraphs, paragraphs_num);
      print_page(page, main_win, 0, main_win_y, NULL);
    }
  }
  // TODO
  else {}

  // dialog panel
  wclear(dialog_win);
  wclear(dialog_subwin);
  refresh_windows();
  
  int d_win_pos_y = max_y / 8.0;
  int d_win_pos_x = max_x / 8.0;
  
  dialog_win_x = max_x * (6.0 / 8.0);
  dialog_win_y = max_y * (6.0 / 8.0);
  
  dialog_subwin_x = dialog_win_x - 2 - 1;
  dialog_subwin_y = dialog_win_y - 2 - 2;

  wresize(dialog_win, dialog_win_y, dialog_win_x);
  wresize(dialog_subwin, dialog_subwin_y, dialog_win_x - 2);
  wresize(dialog_title_win, 2, dialog_win_x - 2);

  mvwin(dialog_win, d_win_pos_y, d_win_pos_x);
  mvwin(dialog_subwin, d_win_pos_y + 3, d_win_pos_x + 1);
  mvwin(dialog_title_win, 1, 1);

  replace_panel(dialog_panel, dialog_win);
  
  box(dialog_win, 0, 0);
  mvwhline(dialog_title_win, 1, 0, 0, dialog_subwin_x + 2);
  
  if(bookmarks.lines) {
    free_lines(&bookmarks);
    bookmarks.lines = paragraphs_to_lines(
      &bookmarks, 
      bookmarks_links, 
      num_bookmarks_links, 
      dialog_subwin_x, 
      true
    );
  }
  
  switch(current_dialog_type){
    case BOOKMARKS:
      mvwprintw(dialog_title_win, 0, dialog_subwin_x / 2 - 4, "%s", "Bookmarks");
      if(bookmarks.lines)
        print_page(&bookmarks, dialog_subwin, 0, dialog_subwin_y, &print_bookmark_line);
      draw_scrollbar(dialog_subwin, &bookmarks, dialog_subwin_y, dialog_win_x - 2 - offset_x);
      break;
    case INFO:
      mvwprintw(dialog_title_win, 0, dialog_subwin_x / 2 - 2, "%s", "Info");
      if(dialog_message)
        wprintw(dialog_subwin, "%s", dialog_message);
      break;
    case OFFLINE: 
      mvwprintw(dialog_title_win, 0, dialog_subwin_x / 2 - 3, "%s", "Offline");
      if(offline.lines)
        print_page(&offline, dialog_subwin, 0, dialog_subwin_y, NULL);
      draw_scrollbar(dialog_subwin, &offline, dialog_subwin_y, dialog_win_x - 2 - offset_x);
      break;
  }
  
  update_panels();
  doupdate();
}

static void reprint_line(
    struct page_t *page, 
    WINDOW *win, 
    int index, 
    int offset,
    println_func_def print_func
    ) {

  wmove(win, offset, 0);
  wclrtoeol(win);
  if(!print_func) print_func = &printline;
  print_func(win, page->lines[index], offset_x, offset);
}


static void pagedown(
    struct page_t *page, 
    WINDOW *win, 
    int page_y,
    println_func_def print_func
    ) {
  if(page->lines_num > page_y) {
    int start_line = 0;
    if(page->last_line_index + page_y > page->lines_num)
      start_line = page->lines_num - page_y;
    else
      start_line = page->last_line_index;
  
    werase(win);
    print_page(page, win, start_line, page_y, print_func);
  }
}


static void pageup(
    struct page_t *page, 
    WINDOW *win, 
    int page_y,
    println_func_def print_func
    ) {
  if(page->lines_num > page_y) {
    int start_line_index = 0;
    if(page->first_line_index - page_y < 0)
      start_line_index = 0;
    else
      start_line_index = page->first_line_index - page_y;

    werase(win);
    print_page(page, win, start_line_index, page_y, print_func);
  }
}


static void nextlink(
    struct page_t *page, 
    WINDOW *win, 
    int page_y,
    println_func_def print_func
    ) {
  bool is_pagedown = false;
start:
  if(page->last_line_index > page->lines_num)
    return;
  if(page->lines == NULL)
    return;

  int link_index = page->selected_link_index;
  if(link_index == -1) { 
    link_index = page->first_line_index - 1;
  }
  else if(link_index < page->first_line_index || link_index >= page->last_line_index){
    page->lines[link_index]->attr ^= A_STANDOUT; 
    link_index = page->first_line_index - 1;
    page->selected_link_index = -1;
  }

  int offset = link_index - page->first_line_index;
    
  // if the next link is on the current page, then just highlight it 
  for(int i = 1; i < page->last_line_index - link_index; i++) {
    if(page->lines[link_index + i]->link != NULL) {
      // unhighlight the old selected link
      if(page->selected_link_index != -1) {
        page->lines[link_index]->attr ^= A_STANDOUT;
        reprint_line(page, win, link_index, offset, print_func);
      }
      // highlight the selected link
      page->lines[link_index + i]->attr ^= A_STANDOUT;
      page->selected_link_index = link_index + i;
      reprint_line(page, win, link_index + i, offset + i, print_func);
      return;
    }
  }

  // if there's no link on the page, then go page down and find a link in a new page
  if(is_pagedown) return;
  is_pagedown = true;
  pagedown(page, win, page_y, print_func);
  goto start;
}


static void prevlink(
    struct page_t *page, 
    WINDOW *win, 
    int page_y,
    println_func_def print_func
    ) {
  bool is_pageup = false;

start:
  if(page->lines == NULL)
    return;
  if(page->first_line_index < 0)
    return;

  int link_index = page->selected_link_index;
  if(link_index == -1) {
    link_index = page->last_line_index;
  }
  else if(link_index < page->first_line_index || link_index >= page->last_line_index){
    page->lines[link_index]->attr ^= A_STANDOUT;
    link_index = page->last_line_index;
    page->selected_link_index = -1;
  }

  int offset = link_index - page->first_line_index;
  // if the next link is on the current page, then just highlight it 
  for(int i = 1; i <= link_index - page->first_line_index; i++) {
    if(link_index - i < 0)
      return;
    
    if(page->lines[link_index - i]->link != NULL) {      
      // unhighlight the old selected link
      if(page->selected_link_index != -1) {
        page->lines[link_index]->attr ^= A_STANDOUT;
        reprint_line(page, win, link_index, offset, print_func);
      }
      // highlight the selected link
      page->lines[link_index - i]->attr ^= A_STANDOUT;
      page->selected_link_index = link_index - i;
      reprint_line(page, win, link_index - i, offset - i, print_func);
      return;
    }
  }
  
  // if there's no link on the page, then go page up and find a link in a new page
  if(is_pageup) return;
  is_pageup = true;
  pageup(page, win, page_y, print_func);
  goto start;
}



// ########## DIALOG ##########

static void show_dialog(enum dialog_types dialog_type) {
  current_dialog_type = dialog_type;
  wclear(dialog_win);
  wclear(dialog_subwin);
  wclear(dialog_title_win);
  
  wattron(dialog_win,       COLOR_PAIR(H3_COLOR));
  wattron(dialog_title_win, COLOR_PAIR(H3_COLOR));
  
  box(dialog_win, 0, 0);
  mvwhline(dialog_title_win, 1, 0, 0, dialog_win_x - 1);

  switch(current_dialog_type){
    case BOOKMARKS:
      mvwprintw(dialog_title_win, 0, dialog_subwin_x / 2 - 4, "%s", "Bookmarks");
      break;
    case INFO:
      mvwprintw(dialog_title_win, 0, dialog_subwin_x / 2 - 2, "%s", "Info");
      break;
    case OFFLINE:
      mvwprintw(dialog_title_win, 0, dialog_subwin_x / 2 - 3, "%s", "Offline");
      break;
  }
  
  refresh_windows();
  show_panel(dialog_panel);
  is_dialog_hidden = false;
}

static void hide_dialog() {
  refresh_windows();
  hide_panel(dialog_panel);
  update_panels();
  doupdate();
  touchwin(main_win);
  is_dialog_hidden = true;
  current_focus = MAIN_WINDOW;
}

static void print_to_dialog(const char *format, ...) {
  if(dialog_message) {
    free(dialog_message);
    dialog_message = NULL;
  }

  va_list args;
  va_start(args, format);
  int len = vsnprintf(NULL, 0, format, args);
  va_start(args, format);

  dialog_message = malloc(len + 1);
  vsprintf(dialog_message, format, args);
  va_end(args);

  wprintw(dialog_subwin, "%s", dialog_message);
  update_panels();
  doupdate();
}

static char dialog_ask(struct page_t *page, struct response *resp, const char *options) {
  int c;
loop:
  c = getch();
  if(c == KEY_RESIZE) {
    resize_screen(page, resp); 
    goto loop;
  }
  
  for(size_t i = 0; i < strlen(options); i++) {
    if(tolower((unsigned char)c) == options[i])
      return options[i];
  }
  if(options[0] == '\0') return '.';
  
  goto loop;
}

// ########## LINK HANDLE ##########
static char* handle_link_click(char *base_url, char *link, struct page_t *page, struct response *resp) {
  char *new_url = strdup(base_url);

  if(!link)
    goto nullret;

  if(get_protocol(link) == GEMINI) {
      if(link[0] == '/')
        link += 2;

      free(new_url);
      return strdup(link);
  } 
  // ":" is a reserved character, so if it's occurs in uri, then, it probably shouldn't be a relative link
  // however FIXME i need to crawl more geminispace
  else if(strstr(link, ":")) {
      show_dialog(INFO);
      print_to_dialog("Open %s? [y/n]", link);
        
      char selected_opt = dialog_ask(page, resp, yes_no_options);
      if(selected_opt == 'y')
       open_link(link); 

      hide_dialog();
      goto nullret;
  }
  // relative link
  else {
    assert(new_url);
    int url_length = strlen(new_url);
    char *p = new_url;

    // cut the gemini scheme from the base url
    if(m_strncmp(new_url, "gemini://") == 0) p += 9;
    // if link has no directory then add it
    if(strchr(p, '/') == NULL) {
      url_length += 1;
      new_url = realloc(new_url, url_length + 1);
      new_url[url_length - 1] = '/';
      new_url[url_length]     = '\0';
      
      p = new_url;
      if(m_strncmp(new_url, "gemini://") == 0) p += 9;
    }

    // if there's an absolute path
    if(m_strncmp(link, "/") == 0) {
      link++;
      char *chr;
      if((chr = strchr(p, '/')) != NULL) {
        ++chr;
        *chr = '\0';
      }
    }
    // if we need to go two or more directories up, then path travel
    else if(m_strncmp(link, "..") == 0 || m_strncmp(link, "./") == 0) {
      char *chr;
      
      if(link[1] == '/') {
        if(link[2] == '.')
          link += 2;
        else
          link += 1;
        
        if((chr = strrchr(p, '/')) != NULL)
          *chr = '\0';
      }

      // at first, clear the current directory
      if(m_strncmp(link, "..") == 0) {
        if((chr = strrchr(p, '/')) != NULL)
          *(chr) = '\0';
      }
      // then go back one directory up, as long as there'is ".."
      while(m_strncmp(link, "..") == 0) {
        if((chr = strrchr(p, '/')) != NULL)
          *(chr) = '\0';
        link += 2;
        if(m_strncmp(link, "/") == 0) 
          link++;
      }
      // we need to concentate '/' to the path, so check if we
      // didnt go too far, and adjust
      if(*(link - 1) == '/')
        link--;
    }
    // if we need to go one directory up
    else if(m_strncmp(link, ".") == 0) {
      char *chr;
      if((chr = strrchr(p, '/')) != NULL) {
        *(chr) = '\0';
      }
      link++;
    }
    // just concatenate it to the current path
    else {
      char *chr;
      if((chr = strrchr(p, '/')) != NULL) {
        ++chr;
        if(*chr != '\0')
          *chr = '\0';
      }
    }
    
    new_url = realloc(new_url, url_length + strlen(link) + 1);
    strcat(new_url, link);
    return new_url;
  }

nullret:
  free(new_url);
  return NULL;
}


// ########## OFFLINE ##########
static char** load_dirs(char *relative_path, int *n_dirs_arg) {  
  DIR *dp;
  struct dirent *ep;
  
  char path[PATH_MAX + 1];
  char *pwd = getenv("PWD");

  strcpy(path, pwd);
  strcat(path, "/");
  strcat(path, SAVED_DIR);
  strcat(path, relative_path);
//  fprintf(stderr, "%s", path);
  struct stat st;
  if(stat(path, &st) == -1)
    mkdir(path, 0700);

  dp = opendir(path);
  char **dirs = NULL;
  int n_dirs = 0;
  if(dp != NULL) {
//    fprintf(stderr,"%s", offline_path);
    while((ep = readdir(dp)) != NULL) {
//      if(relative_path[0] != 0);
//      else if(strcmp(ep->d_name, ".") == 0) continue
      // TODO
      if(strcmp(ep->d_name, ".") == 0)
        continue;
      n_dirs++;
      dirs = realloc(dirs, n_dirs * sizeof(char*));
      if(ep->d_type == DT_DIR) {
        dirs[n_dirs - 1] = malloc(strlen(ep->d_name) + 2);
        strcpy(dirs[n_dirs - 1], ep->d_name); 
        strcat(dirs[n_dirs - 1], "/");
      } else {
        dirs[n_dirs - 1] = strdup(ep->d_name);
      }
    
    }

    (void) closedir(dp);
  }
  else {
    perror("Couldn't open the directory");
    return NULL;
  }
  *n_dirs_arg = n_dirs;
  return dirs;
}

// ########## REQUEST ##########
static int request_gem_page(char *gemini_url, struct gemini_tls *gem_tls, struct page_t *page, struct response **resp) {
  
  bool was_redirected = false;
func_start:
  
  if(!gemini_url)
    return 0;
 
  info_bar_print("Connecting..."); 
  refresh_windows();          

  struct response *new_resp = calloc(1, sizeof(struct response));
  if(new_resp == NULL)
    MALLOC_ERROR;
  
  char fingerprint[100];  
  *fingerprint = '\0';

  int conn_ret = tls_connect(gem_tls, gemini_url, new_resp, fingerprint);
  if(conn_ret == 0) {
    tls_reset(gem_tls);
    info_bar_print(new_resp->error_message);
    goto err;
  }

  if(new_resp->cert_result == TOFU_FINGERPRINT_MISMATCH) {
    show_dialog(INFO);
    print_to_dialog("Fingerprint mistmatch! Do you want to trust it anyway? [y/n]");    
    int selected_opt = dialog_ask(page, *resp, yes_no_options);
    hide_dialog();
  
    if(selected_opt == 'n') {
      tls_reset(gem_tls);
      // info_bar_print("Didn't established connection, because of mistmatched fingerprint.");
      info_bar_print("Fingerprint mistmatch!");
      goto err;
    }
    else {
      assert(*fingerprint);
      tofu_change_cert(gem_tls_get_known_hosts(gem_tls), gem_tls_get_cur_hostname(gem_tls), fingerprint);
      new_resp->cert_result = TOFU_OK;
    }
  }

  tls_read(gem_tls, new_resp);
  tls_reset(gem_tls);

  if(new_resp->error_message != NULL) {
    info_bar_print(new_resp->error_message);
    goto err;
  }
  check_response(new_resp);
  if(new_resp->error_message != NULL) {
    info_bar_print(new_resp->error_message);
    goto err;
  }
  

  assert(new_resp->body);

  switch(new_resp->status_code) {
    case CODE_SENSITIVE_INPUT:
      field_opts_off(search_field[1], O_PUBLIC);
      // fall through
    case CODE_INPUT: 
      show_dialog(INFO);
      assert(new_resp->meta);
      print_to_dialog("%s", new_resp->meta); 

      curs_set(1);
      refresh();
      set_field_buffer(search_field[0], 0, "input:");
      form_driver(search_form, REQ_CLR_FIELD);
      // to test gemini://geminispace.info/
      info_bar_print("Input required!");
      int c;
input_loop:
      wrefresh(search_bar_win);
      c = getch();
      switch(c) {
        case KEY_DOWN:
        case KEY_UP:
          hide_dialog();
          curs_set(0);
          set_field_buffer(search_field[0], 0, "url:");
          set_field_buffer(search_field[1], 0, page->url);
          info_bar_clear();
          goto err;

        case KEY_LEFT:
          form_driver(search_form, REQ_PREV_CHAR); goto input_loop;
        case KEY_RIGHT:
          form_driver(search_form, REQ_NEXT_CHAR); goto input_loop;
        case KEY_BACKSPACE: case 127:
          form_driver(search_form, REQ_DEL_PREV);  goto input_loop;
        case KEY_DC:
          form_driver(search_form, REQ_DEL_CHAR);  goto input_loop;
        case 'W' - 64:
          form_driver(search_form, REQ_DEL_LINE);  goto input_loop;
        case 10:
        case KEY_ENTER:
          form_driver(search_form, REQ_VALIDATION);
          char *query = strdup(trim_whitespaces(field_buffer(search_field[1], 0)));
          wmove(main_win, 0, 0);
          form_driver(search_form, REQ_PREV_FIELD);
         
          int res = get_valid_query(&query);
          if(!res) {
            info_bar_print("Invalid input!");
            free(query);
            goto input_loop;
          }

          // add a query
          int gemini_url_len = strlen(gemini_url), query_len = strlen(query);
          char *new_link = calloc(1, gemini_url_len + 1 + query_len + 1);
          
          memcpy(new_link, gemini_url, gemini_url_len + 1);
          new_link[gemini_url_len] = '?';
          memcpy(new_link + gemini_url_len + 1, query, query_len + 1);
          
          gemini_url = new_link;
          was_redirected = true;
          
          free_resp(new_resp);
          free(query);
          hide_dialog();
          curs_set(0);
          set_field_buffer(search_field[0], 0, "url:");
          goto func_start;

        case KEY_RESIZE:
          resize_screen(page, *resp);
          goto input_loop;

        default:
          form_driver(search_form, c); 
          goto input_loop;
      }
   
      // if it was sensitive input, then set the public input again
      field_opts_on(search_field[1], O_PUBLIC);
      break;
    
    case CODE_SUCCESS:;
      char *mime_type = new_resp->meta;
    
      if(m_strncmp(mime_type, "text/gemini") == 0 || m_strncmp(mime_type, "text/plain") == 0 ||
         m_strncmp(new_resp->body + 3, "\r\n") == 0) {
        // if there's a specified charset, and it's not utf8, then don't show it, but go and ask for download instead
//        INFO_LOG("mime_type %s\n", mime_type);
        char *charset;
        if((charset = strstr(mime_type, "charset=")) != NULL && 
            strncasecmp(charset + 8, "utf-8", 5) != 0 && 
            strncasecmp(charset + 8, "utf8", 4) != 0) {}
        else {
          break;
        }
      }
      // if the mime_type is something else than a gempage, then let's save it, with the filename of the requested resource   
  
      char *filename = strrchr(gemini_url, '/');
      if(filename == NULL || strlen(filename) == 1) {
        info_bar_print("Should be a file, not a directory?");
        goto err;
      }
      else {
        filename++;
      }
      
      int header_offset = 0;
      // skip the header
      while(new_resp->body[header_offset++] != '\n'); 

      char default_app[NAME_MAX + 1];
      char selected_opt;

      show_dialog(INFO);
      if(get_default_app(mime_type, default_app)) {
        print_to_dialog("If you want to open %s [o], if save [s], if nothing [n]", filename);
        const char options[] = {'o', 's', 'n'};
        
        selected_opt = dialog_ask(page, *resp, options);
        if(selected_opt == 'o') {
          if(open_file(
              new_resp->body, 
              filename, 
              default_app, 
              new_resp->body_size, 
              header_offset
           ))
            info_bar_print("Opened the file");
          else
            info_bar_print(strerror(errno));
        }
        else if(selected_opt == 's') {
          char save_path[PATH_MAX + 1];
          if(!save_file(
                save_path, 
                new_resp->body, 
                filename, 
                new_resp->body_size, 
                header_offset
           ))
            info_bar_print("Can't save the file");
          else
            info_bar_print("Successfully saved to: %s", save_path);
        }
        else
          info_bar_print("Didn't open the file");
      }
      else {
        print_to_dialog("Not known mimetype %s\nDo you want to save %s? [y/n]", mime_type, filename);
        
        selected_opt = dialog_ask(page, *resp, yes_no_options);
        if(selected_opt == 'y') {
          char save_path[PATH_MAX + 1];
          if(!save_file(
                save_path, 
                new_resp->body, 
                filename, 
                new_resp->body_size, 
                header_offset
           )) {
            info_bar_print("Can't save the file");
          }
          else {
            info_bar_print("Successfully saved to: %s", save_path);
          }
        }
        else
          info_bar_print("File not saved");
      }

      hide_dialog();
      goto err;
      break;

    case CODE_REDIRECT_PERMANENT:
    case CODE_REDIRECT_TEMPORARY:
      werase(info_bar_win);
      assert(new_resp->meta);
 
      char *new_link = handle_link_click(gemini_url, new_resp->meta, page, *resp);
      
      if(was_redirected)
        free(gemini_url);
      
      show_dialog(INFO);
      if(new_link) { 
        print_to_dialog("Do you want to redirect to: \n%s? [y/n]", new_link);
      }
      else {
        print_to_dialog("%s", "Can't redirect to a new url.");
        goto err;
      }
    
      info_bar_print("Redirecting..."); 
      refresh_windows();          
loop:;
      int ch = getch();
      if(ch == 'y' || ch == 'Y') {
        free_resp(new_resp);
        gemini_url = new_link;
        was_redirected = true;

        hide_dialog();
        goto func_start;
      }
      else if(ch == 'n' || ch == 'N') {
        if(new_link)
          free(new_link);
        // update search bar
        form_driver(search_form, REQ_CLR_FIELD);
        set_field_buffer(search_field[1], 0, page->url);
        
        info_bar_print("Didn't redirected"); 
        hide_dialog();
        goto err;
      }
      else if(ch == KEY_RESIZE) {
        resize_screen(page, *resp);
        goto loop;
      }
      else {
        goto loop;
      }

      break;

    case CODE_TEMPORARY_FAILURE:
      info_bar_print("40 Temporary failure!"); 
      goto err_and_show_meta;
    case CODE_SERVER_UNAVAILABLE:
      info_bar_print("41 Server unavailable!"); 
      goto err_and_show_meta;
    case CODE_CGI_ERROR:
      info_bar_print("42 CGI error!"); 
      goto err_and_show_meta;
    case CODE_PROXY_ERROR:
      info_bar_print("43 Proxy error!"); 
      goto err_and_show_meta;
    case CODE_SLOW_DOWN:
      info_bar_print("44 Slow down!"); 
      goto err_and_show_meta;
    case CODE_PERMANENT_FAILURE:
      info_bar_print("50 Permanent failure!"); 
      goto err_and_show_meta;
    case CODE_NOT_FOUND:
      info_bar_print("51 Gemsite not found!"); 
      goto err_and_show_meta;
    case CODE_GONE:
      info_bar_print("52 Gemsite is gone!"); 
      goto err_and_show_meta;
    case CODE_PROXY_REQUEST_REFUSED:
      info_bar_print("53 Refused request!"); 
      goto err_and_show_meta;
    case CODE_BAD_REQUEST:
      info_bar_print("59 Bad request!"); 
      goto err_and_show_meta;
    case CODE_CLIENT_CERTIFICATE_REQUIRED: 
      info_bar_print("60 Client cert required!"); 
      goto err_and_show_meta;
    case CODE_CERTIFICATE_NOT_AUTHORISED:
      info_bar_print("61 Cert not authorised!"); 
      goto err_and_show_meta;
    case CODE_CERTIFICATE_NOT_VALID:
      info_bar_print("62 Cert not valid!"); 
      goto err_and_show_meta;
    default: 
      info_bar_print("Invalide response code!"); 
      goto err_and_show_meta;
  }


  if(page->url && page->url != gemini_url) {
    free(page->url);
    page->url = NULL;
  }

  if(was_redirected)
    page->url = gemini_url;
  else
    page->url = strdup(gemini_url);


  if(*resp != NULL)
    free_resp(*resp);
  
  if(page->lines)
    free_lines(page);
  
  *resp = new_resp;

  int paragraphs_num = 0;
  char **paragraphs = string_to_paragraphs((*resp)->body, &paragraphs_num);

  page->lines = paragraphs_to_lines(
      page, 
      paragraphs, 
      paragraphs_num, 
      main_win_x, 
      false
  );

  free_paragraphs(paragraphs, paragraphs_num);

  // print the response
  werase(main_win);
  print_page(page, main_win, 0, main_win_y, NULL);
  
  // update search bar
  form_driver(search_form, REQ_CLR_FIELD);
  set_field_buffer(search_field[1], 0, page->url);

  // bookmarking
  page->is_bookmarked = false;
  char *g_p = page->url;
  if(m_strncmp(g_p, "gemini://") == 0) g_p += 9;

  if(!strchr(g_p, '/')) {
    int len = strlen(page->url);
    page->url = realloc(page->url, len + 2);
    page->url[len] = '/';
    page->url[len + 1] = '\0';
    
    g_p = page->url;
    if(m_strncmp(g_p, "gemini://") == 0) g_p += 9;
  }

  if(bookmarks_links && is_bookmark_saved(bookmarks_links, num_bookmarks_links, g_p) != -1)
      page->is_bookmarked = true;
  
  print_is_bookmarked(page->is_bookmarked);
  refresh();

  switch((*resp)->cert_result) {
    case TOFU_OK:
      if((*resp)->was_resumpted) 
        info_bar_print("Valid fingerprint! (session resumpted)");
      else 
        info_bar_print("Valid fingerprint!");
      
      break;
    case TOFU_NEW_HOSTNAME:
      if((*resp)->was_resumpted) 
        info_bar_print("New hostname! (session resumpted)");
      else 
        info_bar_print("New hostname!");
      break;
    default:
      assert(0);
  }

  return 1;

err_and_show_meta:
  if(new_resp->meta) {
    show_dialog(INFO);
    print_to_dialog("%s [press anything to continue]", new_resp->meta);    
    dialog_ask(page, *resp, "");
    hide_dialog();
  }

err:
  field_opts_on(search_field[1], O_PUBLIC);
  free_resp(new_resp);
  return 0;
}

//static void load_offline_dirs(void) {
//  int n_dirs = 0;
//  char **dir_paragraphs = load_dirs(offline_path, &n_dirs);
//  offline_dirs.lines = paragraphs_to_lines(
//    &offline_dirs,
//    dir_paragraphs, 
//    n_dirs, 
//    main_win_x,
//    true
//  );
//  free_paragraphs(dir_paragraphs, n_dirs);
//  offline.selected_link_index = -1;
//}
static inline void print_help(void) {
    puts("\
A gemini ncurses client.\n\
Usage:\n\
  KEY 	ACTION\n\
  arrows up/down 	go down or up on the page\n\
  / 	search\n\
  q 	change to link-mode/scroll-mode\n\
  enter 	go to a link\n\
  B 	go to the defined main gemsite (antenna)\n\
  P 	show bookmarks dialog\n\
  S 	save the gemsite\n\
  C 	show url of the selected link\n\
  A 	bookmark current gemsite\n\
  PgUp/PgDn 	go page up or page down\n\
  mouse scroll 	scroll\n\
  \n\
You can find data at $XDG_DATA_HOME or $HOME/.local/share/gemcurses\
");
}

int main(int argc, char **argv) {
  if(argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
    print_help();
    return EXIT_SUCCESS;
  }

  // set encoding for emojis
  // however some emojis may not work
  setlocale(LC_CTYPE, "en_US.utf8");
  // when server closes a pipe
  signal(SIGPIPE, SIG_IGN);
  // redirect stderr to log.txt in data dir
  {
    char log_path[PATH_MAX + 1];
    get_file_path_in_data_dir("log.txt", log_path, sizeof(log_path));
    freopen(log_path, "a", stderr);
  }

  init_windows();
  init_search_form(false);
  draw_borders();
  print_current_mode();
  print_is_bookmarked(false);
  refresh_windows();
  init_dialog_panel();

  struct gemini_tls *gem_tls = init_tls(0);
  if(gem_tls == NULL) 
    exit(EXIT_FAILURE);

  struct response *resp = NULL;
  struct page_t *gem_page = calloc(1, sizeof(struct page_t));
  
  bookmarks_links = load_bookmarks(&num_bookmarks_links);
  if(bookmarks_links != NULL) {
    bookmarks.lines = paragraphs_to_lines(
        &bookmarks, 
        bookmarks_links, 
        num_bookmarks_links, 
        dialog_subwin_x, 
        true
     );
  }
  // TODO
// load_offline_dirs();
  // mouse support
  MEVENT event;
  mousemask(BUTTON5_PRESSED | BUTTON4_PRESSED, NULL);
  
  // for fuzzing  
//  int flags = fcntl(0, F_GETFL, 0);
//  fcntl(0, F_SETFL, flags | O_NONBLOCK);

  int ch = 0;
  while(ch != KEY_F(1)) {
    if(!is_offline)
      draw_scrollbar(main_win, gem_page, main_win_y, max_x - offset_x);
    else
      draw_scrollbar(main_win, &offline, main_win_y, max_x - offset_x);

    refresh_windows();

    ch = getch();
    // for fuzzing
//    if (ch == -1)
//      return 0;

    if(current_focus == MAIN_WINDOW){
      switch(ch) {      
        case '/':
          if(!is_offline) {
            current_focus = SEARCH_FORM;
            form_driver(search_form, REQ_PREV_FIELD);
            form_driver(search_form, REQ_END_LINE);
            curs_set(1);
          }
          break;
        
        case KEY_DOWN:
          if(current_mode == SCROLL_MODE) {
            if(!is_offline)
              for(int i = 0; i < scrolling_velocity; i++)
                scrolldown(gem_page, main_win, main_win_y, NULL);
            else
              scrolldown(&offline, main_win, main_win_y, NULL);
          }
          else
            if(!is_offline)
              nextlink(gem_page, main_win, main_win_y, NULL);
            else
              nextlink(&offline, main_win, main_win_y, NULL);
          break;
        
        case KEY_UP:
          if(current_mode == SCROLL_MODE) {
            if(!is_offline)
              for(int i = 0; i < scrolling_velocity; i++)
                scrollup(gem_page, main_win, NULL);
            else
              scrollup(&offline, main_win, NULL);
          }
          else
            if(!is_offline)
              prevlink(gem_page, main_win, main_win_y, NULL);
            else
              prevlink(&offline, main_win, main_win_y, NULL);
    
          break;

        case KEY_NPAGE:
          if(!is_offline)
            pagedown(gem_page, main_win, main_win_y, NULL);
          else    
            pagedown(&offline, main_win, main_win_y, NULL);
          break;
        case KEY_PPAGE:
          if(!is_offline)
            pageup(gem_page, main_win, main_win_y, NULL);
          else    
            pageup(&offline, main_win, main_win_y, NULL);
          break;

        case 'R':;
          if(!is_offline) {
            if(gem_page != NULL && gem_page->url != NULL) {
              char *url = gem_page->url;
              int res = request_gem_page(
                  url, 
                  gem_tls, 
                  gem_page, 
                  &resp 
              );
              free(url);
              if(res) {
                curs_set(0);
                form_driver(search_form, REQ_CLR_FIELD);
                set_field_buffer(search_field[1], 0, gem_page->url);
                refresh();
              } 
              info_bar_print("Refreshed!");
            }
          }
        break;

        case 'B':;
          if(!is_offline) {
            int res = request_gem_page(
                MAIN_GEM_SITE, 
                gem_tls, 
                gem_page, 
                &resp 
            );
            if(res) {
              curs_set(0);
              form_driver(search_form, REQ_CLR_FIELD);
              set_field_buffer(search_field[1], 0, gem_page->url);
              refresh();
            }
          }
          break;

        // preview the link
        case 'C':;
          if(!is_offline) {
            if(gem_page->lines && gem_page->selected_link_index != -1) {
              show_dialog(INFO);
              print_to_dialog("%s", gem_page->lines[gem_page->selected_link_index]->link);
              dialog_ask(gem_page, resp, "");
              hide_dialog();
            }
          }
          else {
            if(offline.lines && offline.selected_link_index != -1) {
              show_dialog(INFO);
              print_to_dialog("%s", offline.lines[offline.selected_link_index]->link);
              dialog_ask(gem_page, resp, "");
              hide_dialog();
            }

          }
          break;

        case 'P':;
          if(!is_offline) {
            if(num_bookmarks_links == 0) break;
            if(bookmarks.lines == NULL) {
              if(bookmarks_links == NULL) {
                info_bar_print("No bookmarks saved!");
                break;
              }

              bookmarks.lines = paragraphs_to_lines(
                  &bookmarks, 
                  bookmarks_links, 
                  num_bookmarks_links, 
                  dialog_subwin_x, 
                  true
               );
            }
            
            show_dialog(BOOKMARKS);
            print_page(&bookmarks, dialog_subwin, bookmarks.first_line_index, dialog_subwin_y, &print_bookmark_line);
            current_focus = BOOKMARKS_DIALOG;
            goto refresh_bookmarks;
          }
          else {
            show_dialog(OFFLINE);
            if(offline_dirs.lines[0])
              print_page(&offline_dirs, dialog_subwin, 0, dialog_subwin_y, NULL);
            current_focus = BOOKMARKS_DIALOG;
            wrefresh(dialog_win);
          }
          break;        
        
        // jesus christ i'm sorry for this spaghetti code, i'll refactor it someday i swear
        case 'A':;
          if(!gem_page->url) break;

          char *url = gem_page->url;
          if(m_strncmp(url, "gemini://") == 0) url += 9;

          int bm_link_index = is_bookmark_saved(
              bookmarks_links, 
              num_bookmarks_links, 
              url
           );

          // if it's already saved then delete it
          if(bm_link_index != -1) {
            gem_page->is_bookmarked = false;
            print_is_bookmarked(false);
          
            // de-highlight the selected link
            if(bookmarks.selected_link_index != -1) {
              bookmarks.lines[bookmarks.selected_link_index]->attr ^= A_STANDOUT;
              bookmarks.selected_link_index = -1;
            }
            // free link in bookmarks 
            struct screen_line *line;
            int first_index = -1, num_indexes = 0;
            char *link_to_free = NULL;
            for(int i = 0; i < bookmarks.lines_num; i++) {
              line = bookmarks.lines[i];

              if(strcmp(line->link, url) == 0) {
                free(line->text);
                // free the link only *once* because it's all the same in all lines
                if(first_index == -1) {
                  first_index = i;
                  link_to_free = line->link;
                }
                num_indexes++;
                free(line);
              }
            }
            if(link_to_free)
              free(link_to_free);

            int last_index = first_index + num_indexes;
            memmove(
              &bookmarks.lines[first_index], 
              &bookmarks.lines[last_index], 
              sizeof(struct screen_line*) *(bookmarks.lines_num - last_index)
            );
            
            bookmarks.lines_num -= num_indexes;
            bookmarks.lines = realloc(
                bookmarks.lines, 
                sizeof(struct screen_line*) * bookmarks.lines_num
            );
         

            if(bookmarks.lines_num != 0 && bookmarks.last_line_index > bookmarks.lines_num - 1) {
              if(bookmarks.first_line_index > (bookmarks.last_line_index - bookmarks.lines_num - 1))
                bookmarks.first_line_index -= (bookmarks.last_line_index - (bookmarks.lines_num - 1));
              else
                bookmarks.first_line_index = 0;
              bookmarks.last_line_index = bookmarks.lines_num - 1;
            }
            

            delete_bookmark(url);

            // free link in bookmarks_links
            free(bookmarks_links[bm_link_index]);
            if(bm_link_index != num_bookmarks_links - 1) {
                memmove(
                  &bookmarks_links[bm_link_index], 
                  &bookmarks_links[bm_link_index + 1], 
                  sizeof(struct screen_line*) *(num_bookmarks_links - bm_link_index - 1)
              );
            }
               
            num_bookmarks_links--;
            
            bookmarks_links = realloc(bookmarks_links, sizeof(char*) * num_bookmarks_links);
            break;
          }

          else { 
            num_bookmarks_links++;
            bookmarks_links = realloc(bookmarks_links, sizeof(char*) * num_bookmarks_links);
            bookmarks_links[num_bookmarks_links - 1] = strdup(url);
            
            add_bookmark(url);

            int prev_lines_num = bookmarks.lines_num;
            int link_lines_num  = 0, new_lines_num   = 0;
            int old_index = bookmarks.selected_link_index;
            
            struct screen_line **lines = paragraphs_to_lines(
                &bookmarks, 
                &url,
                1, 
                dialog_subwin_x, 
                true
            );

            link_lines_num = bookmarks.lines_num;
            new_lines_num = prev_lines_num + link_lines_num;
            
            if(prev_lines_num == 0) {
              bookmarks.lines = lines;
            }
            else {
              bookmarks.lines = realloc(bookmarks.lines, sizeof(char*) * new_lines_num);
              for(int i = 0; i < link_lines_num; i++)
                bookmarks.lines[prev_lines_num + i] = lines[i];
            
              free(lines);
            }
            
            bookmarks.lines_num = new_lines_num;
            bookmarks.selected_link_index = old_index;
            gem_page->is_bookmarked = true;
            print_is_bookmarked(true);
          }
          break;

        case 'Q':
        case 'q':
          if(current_mode == SCROLL_MODE)
            current_mode = LINKS_MODE;
          else
            current_mode = SCROLL_MODE;
    
          print_current_mode();
          break;
       
//        case 'O':
//        case 'o':
//          is_offline = !is_offline; 
//          print_current_mode();
//          if(is_offline) {
//            show_dialog(OFFLINE);
//            // load_offline_dirs();
//            if(offline_dirs.lines[0])
//              print_page(&offline_dirs, dialog_subwin, 0, dialog_subwin_y, NULL);
//            current_focus = BOOKMARKS_DIALOG;
//            goto refresh_bookmarks;
//          }
//          else {
//            wclear(main_win); 
//            wrefresh(main_win);
//            if(gem_page->lines)
//              print_page(gem_page, main_win, 0, main_win_y, NULL);
//          } 
//          break;

        case 'S':
        case 's':
          if(!gem_page->url || resp == NULL) break;
          show_dialog(INFO);
          print_to_dialog("%s", "Do you want to save the gemsite? [y/n]");
          char selected_opt = dialog_ask(gem_page, resp, yes_no_options);
          char save_path[PATH_MAX + 1];
          if(selected_opt == 'y') {
            if(save_gemsite(save_path, sizeof(save_path), gem_page->url, resp))
              info_bar_print("Successfully saved to: %s", save_path);
            else
              info_bar_print("Couldn't save the gemsite");
          }
  
          hide_dialog(INFO);
          break;

        // enter
        case 10:
          if(current_mode == LINKS_MODE) {
            if(!resp) break;
            if(!is_offline) {
              char *url = NULL;
              if(
                 gem_page->selected_link_index == -1 || 
                 gem_page->selected_link_index > gem_page->lines_num
                ) break;
              
              char *link = gem_page->lines[gem_page->selected_link_index]->link;
              if((url = handle_link_click(gem_page->url, link, gem_page, resp)) == NULL) 
                break;

              int res = request_gem_page(
                  url, 
                  gem_tls, 
                  gem_page, 
                  &resp
              );

              free(url);

              if(res) {
                curs_set(0);
                form_driver(search_form, REQ_CLR_FIELD);
                set_field_buffer(search_field[1], 0, gem_page->url);
                refresh();
              }
            }
          }
          break;

        case KEY_MOUSE:
        // "For example, in xterm, wheel/scrolling mice send
        // position reports as a sequence of presses of buttons  4  or  5  without
        // matching button-releases."
        // https://invisible-island.net/ncurses/man/curs_mouse.3x.html#h3-Mouse-events
        
        if(getmouse(&event) == OK){ 
          if(event.bstate & BUTTON5_PRESSED)
            for(int i = 0; i < scrolling_velocity; i++)
              scrolldown(gem_page, main_win, main_win_y, NULL);
          else if (event.bstate & BUTTON4_PRESSED)
            for(int i = 0; i < scrolling_velocity; i++)
              scrollup(gem_page, main_win, NULL);
        }
        break;
      }
    }

    else if(current_focus == SEARCH_FORM) {
      switch(ch) {
        case KEY_DOWN:
        case KEY_UP:
          current_focus = MAIN_WINDOW;
          curs_set(0);
          if(gem_page->url)
            set_field_buffer(search_field[1], 0, gem_page->url);
          form_driver(search_form, REQ_PREV_FIELD);
          form_driver(search_form, REQ_END_LINE);
          break;
      
        case KEY_LEFT:
          form_driver(search_form, REQ_PREV_CHAR);
          break;

        case KEY_RIGHT:
          form_driver(search_form, REQ_NEXT_CHAR);
          break;

        case KEY_BACKSPACE:
        case 127:
          form_driver(search_form, REQ_DEL_PREV);
          break;

        // Delete the char under the cursor
        case KEY_DC:
          form_driver(search_form, REQ_DEL_CHAR);
          break;
        
        // https://en.wikipedia.org/wiki/Control_character#How_control_characters_map_to_keyboards
        // ctrl + w
        case 'W' - 64:
          form_driver(search_form, REQ_VALIDATION);

          char *search_str = field_buffer(search_field[1], 0);
          if(!search_str || !*search_str) break;
          
          char search_buf[1024];
          strncpy(search_buf, search_str, sizeof(search_buf));
          trim_whitespaces(search_buf);

          char *slash = NULL;
loop:
          if(*search_buf && (slash = strrchr(search_buf, '/')) != NULL) {
            if(search_buf[strlen(search_buf) - 1] == '/') {
              *slash = '\0';
              goto loop;
            }
            else
              slash[1] = '\0';
          }
          else
            search_buf[0] = '\0';
          if(!*search_buf)
            form_driver(search_form, REQ_CLR_FIELD);
          else
            set_field_buffer(search_field[1], 0, search_buf);

          form_driver(search_form, REQ_PREV_FIELD);
          form_driver(search_form, REQ_END_LINE);
          break;


        case 10: //Enter 
          /*
          https://tldp.org/HOWTO/NCURSES-Programming-HOWTO/forms.html

          To guarantee that right status is returned, call field_status() either
          (1) in the field's exit validation check routine,
          (2) from the field's or form's initialization or termination hooks, or
          (3) just after a REQ_VALIDATION request has been processed by the forms driver
          */
          form_driver(search_form, REQ_VALIDATION);
         
          char *url = trim_whitespaces(field_buffer(search_field[1], 0));
          wmove(main_win, 0, 0);
          form_driver(search_form, REQ_PREV_FIELD);

          // go back to main window
          current_focus = MAIN_WINDOW;
          curs_set(0);
          form_driver(search_form, REQ_PREV_FIELD);
          form_driver(search_form, REQ_END_LINE);

          request_gem_page(
              url, 
              gem_tls, 
              gem_page, 
              &resp
          );
          
          break;

        default:
          form_driver(search_form, ch);
          break;
      }
    }
    else if(current_focus == BOOKMARKS_DIALOG) {
      switch(ch) {
        case KEY_DOWN:
          if(current_mode == SCROLL_MODE) {
            if(!is_offline)
              for(int i = 0; i < scrolling_velocity; i++)
                scrolldown(&bookmarks, dialog_subwin, dialog_subwin_y, &print_bookmark_line);
            else
              for(int i = 0; i < scrolling_velocity; i++)
                scrolldown(&offline_dirs, dialog_subwin, dialog_subwin_y, NULL);
    
          }
          else
            if(!is_offline)
              nextlink(&bookmarks, dialog_subwin, dialog_subwin_y, &print_bookmark_line);
            else 
              nextlink(&offline_dirs, dialog_subwin, dialog_subwin_y, NULL);
        break;
        case KEY_UP:
          if(current_mode == SCROLL_MODE) {
            if(!is_offline)
              for(int i = 0; i < scrolling_velocity; i++)
                scrollup(&bookmarks, dialog_subwin, &print_bookmark_line);
            else
              scrollup(&offline_dirs, dialog_subwin, NULL);
          }
          else
            if(!is_offline)
              prevlink(&bookmarks, dialog_subwin, dialog_subwin_y, &print_bookmark_line);
            else  
              prevlink(&offline_dirs, dialog_subwin, dialog_subwin_y, NULL);
          break;
        case KEY_NPAGE:
          if(!is_offline)
            pagedown(&bookmarks, dialog_subwin, dialog_subwin_y, &print_bookmark_line);
          else
            pagedown(&offline_dirs, dialog_subwin, dialog_subwin_y, NULL);
          break;
        case KEY_PPAGE:
          if(!is_offline)
            pageup(&bookmarks, dialog_subwin, dialog_subwin_y, &print_bookmark_line);
          else
            pageup(&offline_dirs, dialog_subwin, dialog_subwin_y, NULL);
          break;
        case KEY_LEFT:
        case KEY_RIGHT:
          current_focus = MAIN_WINDOW;
          hide_dialog();
          break;
        case KEY_MOUSE:
        if(getmouse(&event) == OK){ 
          if(event.bstate & BUTTON5_PRESSED)
            for(int i = 0; i < scrolling_velocity; i++)
              scrolldown(&bookmarks, dialog_subwin, dialog_subwin_y, &print_bookmark_line);
          else if (event.bstate & BUTTON4_PRESSED)
            for(int i = 0; i < scrolling_velocity; i++)
              scrollup(&bookmarks, dialog_subwin, &print_bookmark_line);
        }
        break;
        
        case 'Q':
        case 'q':
          if(current_mode == SCROLL_MODE)
            current_mode = LINKS_MODE;
          else
            current_mode = SCROLL_MODE;
          print_current_mode();
          break;
        case 10:
          if(!is_offline) {
            if(bookmarks.selected_link_index == -1) break;
            
            char *url = bookmarks.lines[bookmarks.selected_link_index]->link;
            if(!url) break;
      
            int res = request_gem_page(
                url, 
                gem_tls, 
                gem_page, 
                &resp
            );

            if(res) {
              hide_dialog();
              current_focus = MAIN_WINDOW;
            }
          }
          else {
            if(offline_dirs.selected_link_index == -1) break;
              int n_dirs = 0;
              char tmp_path[PATH_MAX + 1] = "";
              strcpy(tmp_path, SAVED_DIR);
              strcat(tmp_path, offline_path);
//              if(strcmp(offline_dirs.lines[offline_dirs.selected_link_index]->text, "../") == 0) 
              strcat(tmp_path, offline_dirs.lines[offline_dirs.selected_link_index]->text);
              struct stat statbuf;
              stat(tmp_path, &statbuf);
              bool is_dir = S_ISDIR(statbuf.st_mode);

              if(is_dir) {
                strcpy(tmp_path, offline_path);
                strcat(tmp_path, offline_dirs.lines[offline_dirs.selected_link_index]->text);
                char **dir_paragraphs = load_dirs(tmp_path, &n_dirs);
                if(offline_dirs.lines) 
                  free_lines(&offline_dirs);
                
                offline_dirs.lines = paragraphs_to_lines(
                  &offline_dirs,
                  dir_paragraphs, 
                  n_dirs, 
                  main_win_x, 
                  true
                );
                free_paragraphs(dir_paragraphs, n_dirs);
                wclear(dialog_subwin);
                print_page(&offline_dirs, dialog_subwin, 0, dialog_subwin_y, NULL);
                wrefresh(dialog_win);
                strcpy(offline_path, tmp_path);
              }
              else {
                
                strcpy(tmp_path, SAVED_DIR);
                strcat(tmp_path, offline_path);
                strcat(tmp_path, offline_dirs.lines[offline_dirs.selected_link_index]->text);
                
                fprintf(stderr, "%s", tmp_path);
                FILE *f = fopen(tmp_path, "rb");
                if(!f) {
                  fprintf(stderr, "cant open the file");
                  break;
                  // TODO do something with it
                }
                
                fseek(f, 0, SEEK_END);
                int fsize = ftell(f);
                rewind(f);

                char *buffer = (char*) malloc(sizeof(char) * fsize + 1);
                if(buffer == NULL) {
                  fprintf(stderr, "%s", "cant alloc memory for buffer\n"); 
                  exit(EXIT_FAILURE);
                 }

                int result = fread(buffer, 1, fsize, f);
                buffer[fsize] = '\0';
                if(result != fsize) {
                  fprintf(stderr, "%s", "reading error\n"); 
                  exit(EXIT_FAILURE);
                }
                fclose(f);
          
                int n_paragraphs = 0;
                char **paragraphs = string_to_paragraphs(buffer, &n_paragraphs);
                offline.lines = paragraphs_to_lines(
                  &offline, 
                  paragraphs, 
                  n_paragraphs, 
                  main_win_x, 
                  false
                );
      
                free_paragraphs(paragraphs, n_paragraphs);
                wclear(main_win);
                print_page(&offline, main_win, 0, main_win_y, NULL);
                hide_dialog();
                current_focus = MAIN_WINDOW;
              }
          }

          break;
      }
refresh_bookmarks:    
      draw_scrollbar(dialog_subwin, &bookmarks, dialog_subwin_y, dialog_win_x - 2 - 1);
      update_panels();
      wrefresh(dialog_subwin);
    }
    
    if(ch == KEY_RESIZE){
      resize_screen(gem_page, resp);
    }
  } 
  free_lines(&bookmarks);
  if(gem_page->url)
    free(gem_page->url);
  free_lines(gem_page);
  free(gem_page);
  tls_free(gem_tls);
  free_resp(resp);
  free_windows();
}
