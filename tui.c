#include <ncurses.h>
#include <form.h>
#include <panel.h>
#include <string.h>
#include <locale.h>

#include "tls.h"
#include "gemini_structs.h"
#include "bookmarks.h"
#include "util.h"

#define MAIN_GEM_SITE "warmedal.se/~antenna/"
#define set_main_win_x(max_x) main_win_x = max_x - offset_x - 1
#define set_main_win_y(max_y) main_win_y = max_y - search_bar_height - info_bar_height - 1 - 1

typedef void (*println_func_def) (WINDOW *, struct screen_line*, int x, int y);

WINDOW *main_win, *search_bar_win, *info_bar_win, *mode_win, *isbookmarked_win;
PANEL  *dialog_panel; WINDOW *dialog_win, *dialog_subwin, *dialog_title_win;
FIELD  *search_field[3];
FORM   *search_form;

const int search_bar_height = 1;
const int info_bar_height = 1;
const int scrolling_velocity = 2;
const int offset_x = 1;

int max_x, max_y;
int main_win_x, main_win_y;
int dialog_subwin_x, dialog_subwin_y;
const char *info_message;
char *dialog_message = NULL;

char **bookmarks_links = NULL;
int num_bookmarks_links = 0;

enum element {SEARCH_FORM, MAIN_WINDOW, BOOKMARKS_DIALOG};
enum element current_focus = MAIN_WINDOW;

enum mode {SCROLL_MODE, LINKS_MODE};
enum mode current_mode = SCROLL_MODE;

enum color {LINK_COLOR = 1, H1_COLOR = 2, H2_COLOR = 3, H3_COLOR = 4, QUOTE_COLOR = 5, DIALOG_COLOR = 6};

enum protocols {HTTPS, HTTP, GOPHER, MAIL, FINGER, SPARTAN, GEMINI, null};
enum dialog_types {BOOKMARKS, INFO} current_dialog_type;

const char *protocols_strings[] = {
  [HTTPS] = " [https]",
  [HTTP] = " [http]",
  [GOPHER] = " [gopher]",
  [MAIL] = " [mail]",
  [FINGER] = " [finger]",
  [SPARTAN] = " [spartan]",
};

const char yes_no_options[] = {'y', 'n'};
bool is_dialog_hidden = true;
struct gemini_site bookmarks;

struct gemini_history {
  struct {
    struct gemini_site **gem_sites;
    int index;
    int size;
  } content_cache;
  
  struct {
    char **urls;
    int size;
  } url_history;
};

static void refresh_windows() {
  if(wnoutrefresh(info_bar_win) == ERR)   goto err;
  if(wnoutrefresh(mode_win) == ERR)       goto err;
  if(wnoutrefresh(main_win) == ERR)       goto err;
  if(wnoutrefresh(isbookmarked_win) == ERR) goto err;
  if(wnoutrefresh(search_bar_win) == ERR) goto err;

  doupdate();
  return;

err: 
  fprintf(stderr, "%s", "Cant refresh window\n");
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

  init_pair(DIALOG_COLOR, COLOR_RED, COLOR_BLACK);
  
}

static void init_dialog_panel() {
  int lines = max_y * (6.0 / 8.0);
  int cols  = max_x * (6.0 / 8.0);
  dialog_subwin_x = cols - 2;
  dialog_subwin_y = lines - 2 - 2;

  dialog_win       = newwin(lines, cols, max_y / 8, max_x / 8);
  dialog_subwin    = derwin(dialog_win, dialog_subwin_y, dialog_subwin_x, 1 + 2, 1);
  dialog_title_win = derwin(dialog_win, 2, dialog_subwin_x, 1, 1);

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
  
  info_bar_win = newwin(info_bar_height, max_x - 2, max_y - 1, 0);  
  mode_win = newwin(1, 1, max_y - 1, max_x - 1);

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

static void free_lines(struct gemini_site *gem_site) {
  
  for(int i = 0; i < gem_site->lines_num; i++) {
    if(gem_site->lines[i]) {
      free(gem_site->lines[i]->text);
      
      if(gem_site->lines[i]->link != NULL) {
        char *p = gem_site->lines[i]->link;
        int j = 0;
        free(p);

        // the  next line may have the same link pointer so take care of it
        while(i + j < gem_site->lines_num) {
          if(gem_site->lines[i + j]->link == p) {
            gem_site->lines[i + j]->link = NULL;
            j++;
          }
          else {
            break;
          }
        }
      }

      free(gem_site->lines[i]);
     }
  }
  free(gem_site->lines);
  gem_site->lines = NULL;
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
    free(resp);
  }
  resp = NULL;
}

// ########## DRAWING ##########

static void draw_borders() {
  mvhline(search_bar_height, 0, 0, max_x);
  mvhline(max_y - 2, 0, 0, max_x);
  mvvline(max_y - 1, max_x - 2, 0, 1);
  mvvline(0, max_x - 2, 0, 1);
}

static void draw_scrollbar(WINDOW *win, struct gemini_site *gem_site, int page_y, int page_x) {
  if(gem_site->lines_num <= 0) return;

  float y;
  float scrollbar_height = (float)((page_y) * (page_y)) / (float)gem_site->lines_num;
  if(scrollbar_height < 1.0)
    scrollbar_height = 1.0;

  if(gem_site->first_line_index == 0) {
    y = 0;
  } 
  else if(gem_site->last_line_index == gem_site->lines_num) {
    y = (float)(page_y + 1 - scrollbar_height);
  }
  else {
    y = (float)(gem_site->first_line_index + 1) / (float)gem_site->lines_num;
    y = page_y * y;
    y = (int)(y + 0.5);
  }

  mvwvline(win, 0, page_x, ' ', page_y);
  mvwvline(win, y, page_x, 0, scrollbar_height);
}

// ########## STRINGS ##########

static char *trim_whitespaces(char *str) {
  char *end;

  while(isspace(*str))
    str++;

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


static int strlen_utf8(char *s) {
  int len = 0, i = 0;
  char *p = s;
  while(*p != '\0') {
    int j = 1;
    if(p[i] & 0x80) {
      while((p[i] << j) & 0x80) j++;
    }
    p += j;
    len++;
  }
  return len;
}

static int utf8_to_bytes(char *s, int n) {
  int len = 0, i = 0, all_bytes = 0;
  char *p = s;
  while(*p && len != n) {
    int j = 1;
    if(p[i] & 0x80) {
      while((p[i] << j) & 0x80) j++;
    }
    p += j;
    all_bytes += j;
    len++;
  }
  return all_bytes;
}

static int bytes_to_utf8(char *s, int n) {
  int len = 0, i = 0;
  char *p = s;
  while(*p && len != n) {
    int j = 1;
    if(p[i] & 0x80) {
      while((p[i] << j) & 0x80) j++;
    }
    p += j;
    len++;
  }
  return len;
}

static enum protocols get_protocol(char *str) {
  
  enum protocols tmp_protocol = null;

  if(m_strncmp(str, "gemini://") == 0 || m_strncmp(str, "//") == 0)
    tmp_protocol = GEMINI;
  else if(m_strncmp(str, "https://") == 0)
    tmp_protocol = HTTPS;
  else if(m_strncmp(str, "http://") == 0)
    tmp_protocol = HTTP;
  else if(m_strncmp(str, "gopher://") == 0)
    tmp_protocol = GOPHER;
  else if(m_strncmp(str, "mailto:") == 0)
    tmp_protocol = MAIL;
  else if(m_strncmp(str, "finger://") == 0)
    tmp_protocol = FINGER;
  else if(m_strncmp(str, "spartan://") == 0)
    tmp_protocol = SPARTAN;

  return tmp_protocol;
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
      char *tmp_para = *paragraph + 2;
      
      // skip the first whitespace
      while(*tmp_para != '\0' && isspace(*tmp_para)) {
        tmp_para++;
        offset++;
      }

      *protocol = get_protocol(tmp_para);

      // go throught the URL
      char *link_start = tmp_para;
      while(*tmp_para != '\0' && !isspace(*tmp_para)) {
        tmp_para++;
        offset++;
      }
      
      // copy the URL
      if(link_start != tmp_para) {
        char *link_p = (char*) calloc(tmp_para - link_start + 1, sizeof(char));
        memcpy(link_p, link_start, tmp_para - link_start);
        link_p[tmp_para - link_start] = '\0';
        *link = link_p;
      }
      
      // skip whitespace after url
      while(*tmp_para != '\0' && isspace(*tmp_para)) {
        tmp_para++;
        offset++;
      }
     
      // if there is no '<USER_FRIENDLY LINK NAME>' then show the plain URL
      if(*tmp_para == '\0') {
        *protocol = null;
        offset = link_start - *paragraph;
      }
      else {
        // i found out that for some reasons, some links have
        // a '\r' symbol at the end?
        // so we need to check this
        while(*tmp_para && *tmp_para != '\r')
          tmp_para++;
        *tmp_para = '\0';
      }
    }
  }
  
  *p_offset = offset;

  return attr;
}


static struct screen_line** paragraphs_to_lines(
    struct gemini_site *gem_site, 
    char **paragraphs, 
    int paragraphs_num, 
    int page_x, 
    // this flag is used in bookmarks
    int *lines_num_arg,
    bool set_links_to_paragraphs
) {
 
  struct screen_line **lines = (struct screen_line**) calloc(1, 1 * sizeof(struct screen_line *));
  int num_lines = 0;
  
  for(int i = 0; i < paragraphs_num; i++) {
  
    enum protocols protocol = null; 
    char *line_str = paragraphs[i], *link = NULL;
    bool is_last_line = false;
    int offset = 0;
    int attr = A_NORMAL;

    // this flag is used with set_links_to_paragraphs
    // if we're printing bookmarks, then we need to know which part (domain) should be bolded
    // so what we do, is we give attr A_BOLD to everything, that's before the slash mark '/' in a url
    bool make_domain_bold = false;
    
    if(!set_links_to_paragraphs) {
      attr = get_paragraph_attr(&line_str, &link, &protocol, &offset);
    } 
    else {
      link = strdup(paragraphs[i]);
      make_domain_bold = true;
    }
    
    line_str += offset;
     
    if(protocol != null && protocol != GEMINI) {
      // if it's not a gemini protocol we need to append an information to the end of the link
      // so realloc, add to the end, and set line_str to our reallocated string + offset
      const char *protocol_str = protocols_strings[protocol];
      paragraphs[i] = realloc(paragraphs[i], strlen(paragraphs[i]) + strlen(protocol_str) + 1);
      strcat(paragraphs[i], protocol_str);
      line_str = paragraphs[i] + offset;
    }
    
    // loop over all lines in a paragraph
    do {
      // if at the end of the line we have a splited word
      // then move the whole word to the next line 
      int word_offset = 0;

      int utf8_len = strlen_utf8(line_str);
      if(utf8_len <= page_x - offset_x)
        is_last_line = true;

      int line_len = utf8_to_bytes(line_str, page_x - offset_x);

      // check if word at the end of the line needs to be moved to the next line
      char *tmp_line = line_str, *last_found_space = NULL;
      if(*(line_str + line_len) != '\0') {  
        while(tmp_line != line_str + line_len) {
          tmp_line += utf8_to_bytes(tmp_line, 1);
          if(isspace(*tmp_line)) 
            last_found_space = tmp_line;
        }
        if(last_found_space != NULL) {
          int tmp = bytes_to_utf8(last_found_space, (line_str + line_len) - last_found_space);
          // word offset should be in raw, non-unicode bytes
          word_offset = (line_str + line_len) - last_found_space;
          // if we have one long line then let it be split
          // otherwise move 
          if(tmp > (page_x - offset_x) / 2) {
            word_offset = 0;
          }
        }
      }
     
      line_len -= word_offset;

      // TODO do this only if there is no ''' paragraph
      if(*line_str != '\0' && isspace(*line_str)) {
        line_str++;
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
      else
        line->attr = attr;

      line->text = text;
      line->link = link;
      lines[num_lines - 1] = line;
      
      line_str += line_len;
    } while(!is_last_line);
  }

  *lines_num_arg = num_lines;
  gem_site->selected_link_index = -1;

  return lines;
}


// ########## PRINTING ##########

static void print_current_mode() {
  werase(mode_win);
  if(current_mode == LINKS_MODE)
    wprintw(mode_win, "%c", 'L');
  else
    wprintw(mode_win, "%c", 'S');
}

static void print_is_bookmarked(bool is_bookmarked) {
  werase(isbookmarked_win);
  if(is_bookmarked)
    wprintw(isbookmarked_win, "%s", "★");
  else
    wprintw(isbookmarked_win, "%s", "☆");
}

static void info_bar_print(const char *str) {
  // const strings have static duration so its valid
  info_message = str;
  werase(info_bar_win);
  wprintw(info_bar_win, "%s", info_message);
  wrefresh(info_bar_win);
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

static void print_gemini_site(
    struct gemini_site *gem_site, 
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
    line = gem_site->lines[index];
    print_func(win, line, offset_x, i);
    index++;
    if(index >= gem_site->lines_num)
      break;
  }

  gem_site->last_line_index = index;
  gem_site->first_line_index = first_line_index;
  
  scrollok(win, true);
}


static void print_bookmark_line(WINDOW *win, struct screen_line *line, int x, int y) {
  scrollok(dialog_subwin, false);
  wattron(dialog_subwin, line->attr);

  char *domain = NULL;
  char deleted_char = '\0';

  if(line->attr == A_BOLD) {
    domain = strchr(line->text, '/');
    if(domain) {
      deleted_char = *domain;
      *domain = '\0';
    }
  }
 
  if(line->text != NULL) {
    wattron(dialog_subwin, line->attr);
    mvwprintw(dialog_subwin, y, offset_x, "%s", line->text);
    wattroff(dialog_subwin, line->attr);

    if(domain) {
      *domain = deleted_char;
      mvwprintw(dialog_subwin, y, offset_x + domain - line->text, "%s", domain);
    }
  }

  wattroff(dialog_subwin, line->attr);
  scrollok(dialog_subwin, true); 
}

static void scrolldown(
    struct gemini_site *gem_site, 
    WINDOW *win,
    int page_y, 
    println_func_def print_func
  ) {
  
  if(gem_site->last_line_index >= gem_site->lines_num || gem_site->lines == NULL)
    return;
  
  wscrl(win, 1);

  struct screen_line *line = gem_site->lines[gem_site->last_line_index];
  if(!print_func) print_func = &printline;  
  print_func(win, line, offset_x, page_y - 1);

  gem_site->last_line_index++;
  gem_site->first_line_index++;
}


static void scrollup(
    struct gemini_site *gem_site, 
    WINDOW *win,
    println_func_def print_func
  ) {
  
  if(gem_site->first_line_index == 0 || gem_site->lines == NULL)
    return;

  wscrl(win, -1);
  
  gem_site->first_line_index--;
  gem_site->last_line_index--;

  struct screen_line *line = gem_site->lines[gem_site->first_line_index];
  if(!print_func) print_func = &printline;  
  print_func(win, line, offset_x, 0);
}

static void resize_screen(struct gemini_site *gem_site, struct response *resp) {

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
  wresize(info_bar_win, 1, max_x - 2);
  mvwin(info_bar_win, max_y - 1, 0);
  wclear(info_bar_win);
  if(info_message)
    wprintw(info_bar_win, "%s", info_message);

  // mode win
  mvwin(mode_win, max_y - 1, max_x - 1);
  print_current_mode();

  // search_bar win
  wresize(search_bar_win, 1, max_x - 2);
  
  // bookmarked win
  mvwin(isbookmarked_win, 0, max_x - 1);
  print_is_bookmarked(gem_site->is_bookmarked);
  
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
  gem_site->selected_link_index = -1;
  // if there was a response, then print it
  if(gem_site->lines != NULL)
    free_lines(gem_site);
  
  if(resp && resp->body) {
    int paragraphs_num = 0;
    char **paragraphs = string_to_paragraphs(resp->body, &paragraphs_num);
    gem_site->lines = paragraphs_to_lines(
        gem_site, 
        paragraphs, 
        paragraphs_num, 
        main_win_x, 
        &gem_site->lines_num, 
        false
    );
    
    free_paragraphs(paragraphs, paragraphs_num);
    print_gemini_site(gem_site, main_win, 0, main_win_y, NULL);
  }

  // dialog panel
  wclear(dialog_win);
  wclear(dialog_subwin);
  refresh_windows();
  
  int d_win_lines = max_y * (6.0 / 8.0);
  int d_win_cols  = max_x * (6.0 / 8.0);
  int d_win_pos_y = max_y / 8.0;
  int d_win_pos_x = max_x / 8.0;
  dialog_subwin_x = d_win_cols - 2;
  dialog_subwin_y = d_win_lines - 2 - 2;
 
  wresize(dialog_win, d_win_lines, d_win_cols);
  wresize(dialog_subwin, dialog_subwin_y, dialog_subwin_x);
  wresize(dialog_title_win, 2, dialog_subwin_x);

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
      &bookmarks.lines_num,
      true
    );
  }
  
  switch(current_dialog_type){
    case BOOKMARKS:
      mvwprintw(dialog_title_win, 0, dialog_subwin_x / 2 - 4, "%s", "Bookmarks");
      if(bookmarks.lines)
        print_gemini_site(&bookmarks, dialog_subwin, 0, dialog_subwin_y, &print_bookmark_line);
      break;
    case INFO:
      mvwprintw(dialog_title_win, 0, dialog_subwin_x / 2 - 2, "%s", "Info");
      if(dialog_message)
        wprintw(dialog_subwin, "%s", dialog_message);
      break;
  }

  update_panels();
  doupdate();
}

static void reprint_line(
    struct gemini_site *gem_site, 
    WINDOW *win, 
    int index, 
    int offset,
    println_func_def print_func
    ) {

  wmove(win, offset, 0);
  wclrtoeol(win);
  if(!print_func) print_func = &printline;
  print_func(win, gem_site->lines[index], offset_x, offset);
}


static void pagedown(
    struct gemini_site *gem_site, 
    WINDOW *win, 
    int page_y,
    println_func_def print_func
    ) {
  if(gem_site->lines_num > page_y) {
    int start_line = 0;
    if(gem_site->last_line_index + page_y > gem_site->lines_num)
      start_line = gem_site->lines_num - page_y;
    else
      start_line = gem_site->last_line_index;
  
    werase(win);
    print_gemini_site(gem_site, win, start_line, page_y, print_func);
  }
}


static void pageup(
    struct gemini_site *gem_site, 
    WINDOW *win, 
    int page_y,
    println_func_def print_func
    ) {
  if(gem_site->lines_num > page_y) {
    int start_line_index = 0;
    if(gem_site->first_line_index - page_y < 0)
      start_line_index = 0;
    else
      start_line_index = gem_site->first_line_index - page_y;

    werase(win);
    print_gemini_site(gem_site, win, start_line_index, page_y, print_func);
  }
}


static void nextlink(
    struct gemini_site *gem_site, 
    WINDOW *win, 
    int page_y,
    println_func_def print_func
    ) {
  bool is_pagedown = false;
start:
  if(gem_site->last_line_index > gem_site->lines_num)
    return;
  if(gem_site->lines == NULL)
    return;

  int link_index = gem_site->selected_link_index;
  if(link_index == -1) { 
    link_index = gem_site->first_line_index - 1;
  }
  else if(link_index < gem_site->first_line_index || link_index >= gem_site->last_line_index){
    gem_site->lines[link_index]->attr ^= A_STANDOUT; 
    link_index = gem_site->first_line_index - 1;
    gem_site->selected_link_index = -1;
  }

  int offset = link_index - gem_site->first_line_index;
    
  // if the next link is on the current page, then just highlight it 
  for(int i = 1; i < gem_site->last_line_index - link_index; i++) {
    if(gem_site->lines[link_index + i]->link != NULL) {
      // unhighlight the old selected link
      if(gem_site->selected_link_index != -1) {
        gem_site->lines[link_index]->attr ^= A_STANDOUT;
        reprint_line(gem_site, win, link_index, offset, print_func);
      }
      // highlight the selected link
      gem_site->lines[link_index + i]->attr ^= A_STANDOUT;
      gem_site->selected_link_index = link_index + i;
      reprint_line(gem_site, win, link_index + i, offset + i, print_func);
      return;
    }
  }

  // if there's no link on the page, then go page down and find a link in a new page
  if(is_pagedown) return;
  is_pagedown = true;
  pagedown(gem_site, win, page_y, print_func);
  goto start;
}


static void prevlink(
    struct gemini_site *gem_site, 
    WINDOW *win, 
    int page_y,
    println_func_def print_func
    ) {
  bool is_pageup = false;

start:
  if(gem_site->lines == NULL)
    return;
  if(gem_site->first_line_index < 0)
    return;

  int link_index = gem_site->selected_link_index;
  if(link_index == -1) {
    link_index = gem_site->last_line_index;
  }
  else if(link_index < gem_site->first_line_index || link_index >= gem_site->last_line_index){
    gem_site->lines[link_index]->attr ^= A_STANDOUT;
    link_index = gem_site->last_line_index;
    gem_site->selected_link_index = -1;
  }

  int offset = link_index - gem_site->first_line_index;
  // if the next link is on the current page, then just highlight it 
  for(int i = 1; i <= link_index - gem_site->first_line_index; i++) {
    if(link_index - i < 0)
      return;
    
    if(gem_site->lines[link_index - i]->link != NULL) {      
      // unhighlight the old selected link
      if(gem_site->selected_link_index != -1) {
        gem_site->lines[link_index]->attr ^= A_STANDOUT;
        reprint_line(gem_site, win, link_index, offset, print_func);
      }
      // highlight the selected link
      gem_site->lines[link_index - i]->attr ^= A_STANDOUT;
      gem_site->selected_link_index = link_index - i;
      reprint_line(gem_site, win, link_index - i, offset - i, print_func);
      return;
    }
  }
  
  // if there's no link on the page, then go page up and find a link in a new page
  if(is_pageup) return;
  is_pageup = true;
  pageup(gem_site, win, page_y, print_func);
  goto start;
}




// ########## DIALOG ##########

static void show_dialog() {
  wclear(dialog_win);
  wclear(dialog_subwin);
  wclear(dialog_title_win);
  
  wattron(dialog_win,       COLOR_PAIR(H3_COLOR));
  wattron(dialog_title_win, COLOR_PAIR(H3_COLOR));
  
  box(dialog_win, 0, 0);
  mvwhline(dialog_title_win, 1, 0, 0, dialog_subwin_x + 2);

  switch(current_dialog_type){
    case BOOKMARKS:
      mvwprintw(dialog_title_win, 0, dialog_subwin_x / 2 - 4, "%s", "Bookmarks");
      break;
    case INFO:
      mvwprintw(dialog_title_win, 0, dialog_subwin_x / 2 - 2, "%s", "Info");
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

static char dialog_ask(struct gemini_site *gem_site, struct response *resp, const char *options) {
  int c;
loop:
  c = getch();
  if(c == KEY_RESIZE) {
    resize_screen(gem_site, resp); 
    goto loop;
  }
  
  for(size_t i = 0; i < strlen(options); i++) {
    if(tolower((unsigned char)c) == options[i])
      return options[i];
  }
  if(options == "") return '.';
  
  goto loop;
}

// ########## LINK HANDLE ##########
static char* handle_link_click(char *base_url, char *link, struct gemini_site *gem_site, struct response *resp) {
  char *new_url = strdup(base_url);

  if(!link)
    goto nullret;

  enum protocols protocol = get_protocol(link);
  switch(protocol) {
    case GEMINI:
      if(link[0] == '/')
        link += 2;

      free(new_url);
      return strdup(link);
    case GOPHER:
    case MAIL:
    case HTTP:
    case HTTPS:
      current_dialog_type = INFO;
      show_dialog();
      print_to_dialog("Open %s? [y/n]", link);
        
      char selected_opt = dialog_ask(gem_site, resp, yes_no_options);
      if(selected_opt == 'y')
       open_link(link); 

      hide_dialog();
      // wclear(info_bar_win);
      // wprintw(info_bar_win, "%s", link);
      goto nullret;
    
    default: {
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

        while(m_strncmp(link, "..") == 0) {
          for(int i = 0; i < 2; i++) {
            if((chr = strrchr(p, '/')) != NULL)
              *(chr) = '\0';
          }
          link += 2;
          if(m_strncmp(link, "/") == 0) {
            link++;
          }
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

  }

nullret:
  free(new_url);
  return NULL;
}


// ########## REQUEST ##########
static int request_gem_site(char *gemini_url, struct gemini_tls *gem_tls, struct gemini_site *gem_site, struct response **resp) {
  
  bool was_redirected = false;
func_start:
  
  if(!gemini_url)
    return 0;
 
  info_bar_print("Connecting..."); 
  refresh_windows();          

  struct response *new_resp = tls_request(gem_tls, gemini_url);
  if(new_resp == NULL) return 0;

  if(new_resp->error_message != NULL) {
    info_bar_print(new_resp->error_message);
    goto err;
  }

  if(new_resp->body == NULL || new_resp->body[0] == '\0') {
    info_bar_print("Can't connect to the host");
    goto err;
  }

  switch(new_resp->status_code) {
    case CODE_SENSITIVE_INPUT:
      field_opts_off(search_field[1], O_PUBLIC);
    case CODE_INPUT: 
      current_dialog_type = INFO;
      show_dialog();
      
      new_resp->body[strlen(new_resp->body) - 2] = '\0';
      char *b_p = new_resp->body;
      while(*b_p && isdigit(*b_p)) b_p++;
      print_to_dialog("%s", b_p);

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
          set_field_buffer(search_field[1], 0, gem_site->url);
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
          resize_screen(gem_site, *resp);
          goto input_loop;

        default:
          form_driver(search_form, c); 
          goto input_loop;
      }
   
      // if it was sensitive input, then set the public input again
      field_opts_on(search_field[1], O_PUBLIC);
      break;
    
    case CODE_SUCCESS:;
      char *mime_type = get_mime_type(new_resp->body);
      if(!mime_type) {
        info_bar_print("No mimetype in response");
        goto err;
      }

      if(strcmp(mime_type, "text/gemini") == 0 || strcmp(mime_type, "text/plain") == 0) {
        free(mime_type);
        break;
      }
      
      char *filename = strrchr(gemini_url, '/');
      if(filename == NULL || strlen(filename) == 1) {
        info_bar_print("Should be a file, not a directory?");
        free(mime_type);
        goto err;
      }
      else {
        filename++;
      }
      
      int header_offset = 0;
      while(new_resp->body[header_offset] != '\n') 
        header_offset++;
      // one more to omit '\n'
      header_offset++;

      char default_app[NAME_MAX + 1];
      char selected_opt;

      current_dialog_type = INFO;
      show_dialog();
      if(get_default_app(mime_type, default_app)) {
        print_to_dialog("If you want to open %s [o], if save [s], if nothing [n]", filename);
        const char options[] = {'o', 's', 'n'};
        
        selected_opt = dialog_ask(gem_site, *resp, options);
        if(selected_opt == 'o') {
          open_file(
              new_resp->body, 
              filename, 
              default_app, 
              new_resp->body_size, 
              header_offset
           );
          info_bar_print("Opened a file");
        }
        else if(selected_opt == 's') {
          char save_path[PATH_MAX + 1];
          save_file(
              save_path, 
              new_resp->body, 
              filename, 
              new_resp->body_size, 
              header_offset
           );

          info_message = "";
          werase(info_bar_win);
          wprintw(info_bar_win, "Successfully saved to: %s", save_path);
        }
        else {
          info_message = "";
          werase(info_bar_win);
        }
      }
      else {
        print_to_dialog("Not known mimetype. Do you want to save %s? [y/n]", filename);
        
        selected_opt = dialog_ask(gem_site, *resp, yes_no_options);
        if(selected_opt == 'y') {
          char save_path[PATH_MAX + 1];
          save_file(
              save_path, 
              new_resp->body, 
              filename, 
              new_resp->body_size, 
              header_offset
           );

          info_message = "";
          werase(info_bar_win);
          wprintw(info_bar_win, "Successfully saved to: %s", save_path);
        }
        else
          info_bar_print("File not saved");
      }

      hide_dialog();
      free(mime_type);
      goto err;
      break;

    case CODE_REDIRECT_PERMANENT:
    case CODE_REDIRECT_TEMPORARY:
      werase(info_bar_win);
      char *redirect_link = new_resp->body; 
      redirect_link += 3;
      char *p = redirect_link;
      while(*p && *p != '\r')
        p++;
      *p = '\0';
      
      char *new_link = handle_link_click(gemini_url, redirect_link, gem_site, *resp);
      
      if(was_redirected)
        free(gemini_url);
      
      current_dialog_type = INFO;
      show_dialog();
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
        set_field_buffer(search_field[1], 0, gem_site->url);
        
        info_bar_print("Didn't redirected"); 
        hide_dialog();
        goto err;
      }
      else if(ch == KEY_RESIZE) {
        resize_screen(gem_site, *resp);
        goto loop;
      }
      else {
        goto loop;
      }

      break;

    case CODE_TEMPORARY_FAILURE:
      info_bar_print("Temporary failure!"); 
      goto err;
    case CODE_SERVER_UNAVAILABLE:
      info_bar_print("Server unavailable!"); 
      goto err;
    case CODE_CGI_ERROR:
      info_bar_print("CGI error!"); 
      goto err;
    case CODE_PROXY_ERROR:
      info_bar_print("Proxy error!"); 
      goto err;
    case CODE_SLOW_DOWN:
      info_bar_print("Slow down!"); 
      goto err;
    case CODE_PERMANENT_FAILURE:
      info_bar_print("Permanent failure!"); 
      goto err;
    case CODE_NOT_FOUND:
      info_bar_print("Gemsite not found!"); 
      goto err;
    case CODE_GONE:
      info_bar_print("Gemsite is gone!"); 
      goto err;
    case CODE_PROXY_REQUEST_REFUSED:
      info_bar_print("Refused request!"); 
      goto err;
    case CODE_BAD_REQUEST:
      info_bar_print("Bad request!"); 
      goto err; 
    case CODE_CLIENT_CERTIFICATE_REQUIRED: 
      info_bar_print("Client cert required!"); 
      goto err;
    case CODE_CERTIFICATE_NOT_AUTHORISED:
      info_bar_print("Cert not authorised!"); 
      goto err;
    case CODE_CERTIFICATE_NOT_VALID:
      info_bar_print("Cert not valid!"); 
      goto err;
    default: 
      info_bar_print("Invalide response code!"); 
      goto err;
  }


  if(gem_site->url && gem_site->url != gemini_url) {
    free(gem_site->url);
    gem_site->url = NULL;
  }

  if(was_redirected)
    gem_site->url = gemini_url;
  else
    gem_site->url = strdup(gemini_url);


  if(*resp != NULL)
    free_resp(*resp);
  
  if(gem_site->lines)
    free_lines(gem_site);
  
  *resp = new_resp;

  int paragraphs_num = 0;
  char **paragraphs = string_to_paragraphs((*resp)->body, &paragraphs_num);

  gem_site->lines = paragraphs_to_lines(
      gem_site, 
      paragraphs, 
      paragraphs_num, 
      main_win_x, 
      &gem_site->lines_num, 
      false
  );
  free_paragraphs(paragraphs, paragraphs_num);

  // print the response
  werase(main_win);
  print_gemini_site(gem_site, main_win, 0, main_win_y, NULL);
  
  // update search bar
  form_driver(search_form, REQ_CLR_FIELD);
  set_field_buffer(search_field[1], 0, gem_site->url);

  // bookmarking
  gem_site->is_bookmarked = false;
  char *g_p = gem_site->url;
  if(m_strncmp(g_p, "gemini://") == 0) g_p += 9;

  if(!strchr(g_p, '/')) {
    int len = strlen(gem_site->url);
    gem_site->url = realloc(gem_site->url, len + 2);
    gem_site->url[len] = '/';
    gem_site->url[len + 1] = '\0';
    
    g_p = gem_site->url;
    if(m_strncmp(g_p, "gemini://") == 0) g_p += 9;
  }

  if(bookmarks_links && is_bookmark_saved(bookmarks_links, num_bookmarks_links, g_p) != -1)
      gem_site->is_bookmarked = true;
  
  print_is_bookmarked(gem_site->is_bookmarked);
  refresh();

  switch((*resp)->cert_result) {
    case TOFU_OK:
      if((*resp)->was_resumpted) 
        info_bar_print("Valid fingerprint! (session resumpted)");
      else 
        info_bar_print("Valid fingerprint!");
      
      break;
    case TOFU_FINGERPRINT_MISMATCH:
      if((*resp)->was_resumpted) 
        info_bar_print("Fingerprint mistmatch! (session resumpted)");
      else 
        info_bar_print("Fingerprint mistmatch!");
      
      break;
    case TOFU_NEW_HOSTNAME:
      if((*resp)->was_resumpted) 
        info_bar_print("New hostname! (session resumpted)");
      else 
        info_bar_print("New hostname!");
      
      break;
  }

  return 1;

err:
  field_opts_on(search_field[1], O_PUBLIC);
  free_resp(new_resp);
  return 0;
}

int main() {
  // set encoding for emojis
  // however some emojis may not work
  setlocale(LC_ALL, "en_US.utf8");
  // when server closes a pipe
  signal(SIGPIPE, SIG_IGN);
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
  struct gemini_site *gem_site = calloc(1, sizeof(struct gemini_site));
  
  bookmarks_links = load_bookmarks(&num_bookmarks_links);
  if(bookmarks_links != NULL) {
    bookmarks.lines = paragraphs_to_lines(
        &bookmarks, 
        bookmarks_links, 
        num_bookmarks_links, 
        dialog_subwin_x, 
        &bookmarks.lines_num,
        true
     );
  }
  bookmarks.last_line_index = -1;
  // mouse support
  MEVENT event;
  mousemask(BUTTON5_PRESSED | BUTTON4_PRESSED, NULL);

  int ch = 0;
  while(ch != KEY_F(1)) {
  
    draw_scrollbar(main_win, gem_site, main_win_y, max_x - offset_x);
    refresh_windows();

    ch = getch();
    if(current_focus == MAIN_WINDOW){
      switch(ch) {      
        case '/':
          current_focus = SEARCH_FORM;
          form_driver(search_form, REQ_PREV_FIELD);
          form_driver(search_form, REQ_END_LINE);
          curs_set(1);
          break;
        
        case KEY_DOWN:
          if(current_mode == SCROLL_MODE) {
            for(int i = 0; i < scrolling_velocity; i++)
              scrolldown(gem_site, main_win, main_win_y, NULL);
          }
          else
            nextlink(gem_site, main_win, main_win_y, NULL);
          break;
        
        case KEY_UP:
          if(current_mode == SCROLL_MODE) {
            for(int i = 0; i < scrolling_velocity; i++)
              scrollup(gem_site, main_win, NULL);
          }
          else
            prevlink(gem_site, main_win, main_win_y, NULL);
          break;

        case KEY_NPAGE:
          pagedown(gem_site, main_win, main_win_y, NULL);
          break;
        case KEY_PPAGE:
          pageup(gem_site, main_win, main_win_y, NULL);
          break;

        case 'B':;
          int res = request_gem_site(
              MAIN_GEM_SITE, 
              gem_tls, 
              gem_site, 
              &resp 
          );
          if(res) {
            curs_set(0);
            form_driver(search_form, REQ_CLR_FIELD);
            set_field_buffer(search_field[1], 0, gem_site->url);
            refresh();
          }
          break;

        // preview the link
        case 'C':;
          if(gem_site->lines && gem_site->selected_link_index != -1) {
            current_dialog_type = INFO;
            show_dialog();
            print_to_dialog("%s", gem_site->lines[gem_site->selected_link_index]->link);
            dialog_ask(gem_site, resp, "");
            hide_dialog();
          }
          break;

        case 'P':;
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
                &bookmarks.lines_num,
                true
             );
          }
          
          current_dialog_type = BOOKMARKS;
          show_dialog();
          print_gemini_site(&bookmarks, dialog_subwin, bookmarks.first_line_index, dialog_subwin_y, &print_bookmark_line);

          update_panels();
          doupdate();
          current_focus = BOOKMARKS_DIALOG;
          break;        
        
        case 'A':;
          if(!gem_site->url) break;

          char *url = gem_site->url;
          if(m_strncmp(url, "gemini://") == 0) url += 9;

          int bm_link_index = is_bookmark_saved(
              bookmarks_links, 
              num_bookmarks_links, 
              url
           );

          // if it's already saved then delete it
          if(bm_link_index != -1) {
            gem_site->is_bookmarked = false;
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
         

            if(bookmarks.last_line_index > bookmarks.lines_num - 1) {
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

            int link_lines_num;
            int old_index = bookmarks.selected_link_index;
            struct screen_line **lines = paragraphs_to_lines(
                &bookmarks, 
                &url,
                1, 
                dialog_subwin_x, 
                &link_lines_num,
                true
            );  

            int len = link_lines_num + bookmarks.lines_num;
            if(bookmarks.lines_num == 0) {
              bookmarks.lines = lines;
            }
            else {
              bookmarks.lines = realloc(bookmarks.lines, sizeof(char*) * len);
              for(int i = 0; i < link_lines_num; i++)
                bookmarks.lines[bookmarks.lines_num + i] = lines[i];
            
              free(lines);
            }
            
            bookmarks.lines_num += link_lines_num;
            
            bookmarks.selected_link_index = old_index;
            gem_site->is_bookmarked = true;
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
        

        // TODO
        case 'S':
        case 's':
          if(!gem_site->url || resp == NULL) break;
          current_dialog_type = INFO;
          show_dialog();
          print_to_dialog("%s", "Do you want to save the gemsite? [y/n]");
          char selected_opt = dialog_ask(gem_site, resp, yes_no_options);
          if(selected_opt == 'y') {
            save_gemsite(gem_site->url, resp);
          }
          break;

        // enter
        case 10:
          if(current_mode == LINKS_MODE) {
            char *url = NULL;
            if(
               gem_site->selected_link_index == -1 || 
               gem_site->selected_link_index > gem_site->lines_num
              ) break;
            
            char *link = gem_site->lines[gem_site->selected_link_index]->link;
            if((url = handle_link_click(gem_site->url, link, gem_site, resp)) == NULL) 
              break;

            int res = request_gem_site(
                url, 
                gem_tls, 
                gem_site, 
                &resp
            );

            free(url);

            if(res) {
              curs_set(0);
              form_driver(search_form, REQ_CLR_FIELD);
              set_field_buffer(search_field[1], 0, gem_site->url);
              refresh();
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
              scrolldown(gem_site, main_win, main_win_y, NULL);
          else if (event.bstate & BUTTON4_PRESSED)
            for(int i = 0; i < scrolling_velocity; i++)
              scrollup(gem_site, main_win, NULL);
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
          if(gem_site->url)
            set_field_buffer(search_field[1], 0, gem_site->url);
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
          form_driver(search_form, REQ_DEL_LINE);
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

          request_gem_site(
              url, 
              gem_tls, 
              gem_site, 
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
            for(int i = 0; i < scrolling_velocity; i++)
              scrolldown(&bookmarks, dialog_subwin, dialog_subwin_y, &print_bookmark_line);
          }
          else
            nextlink(&bookmarks, dialog_subwin, dialog_subwin_y, &print_bookmark_line);
          break;
        case KEY_UP:
          if(current_mode == SCROLL_MODE) {
            for(int i = 0; i < scrolling_velocity; i++)
              scrollup(&bookmarks, dialog_subwin, &print_bookmark_line);
          }
          else
            prevlink(&bookmarks, dialog_subwin, dialog_subwin_y, &print_bookmark_line);
          break;
        case KEY_NPAGE:
          pagedown(&bookmarks, dialog_subwin, dialog_subwin_y, &print_bookmark_line);
          break;
        case KEY_PPAGE:
          pageup(&bookmarks, dialog_subwin, dialog_subwin_y, &print_bookmark_line);
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
          if(bookmarks.selected_link_index == -1) break;
          char *url = bookmarks.lines[bookmarks.selected_link_index]->link;
          if(!url) break;
          int res = request_gem_site(
              url, 
              gem_tls, 
              gem_site, 
              &resp
          );

          if(res) {
            hide_dialog();
            current_focus = MAIN_WINDOW;
          }

          break;
      }
      update_panels();
      wrefresh(dialog_subwin);
    }
    
    if(ch == KEY_RESIZE){
      resize_screen(gem_site, resp);
    }
  } 
  free_lines(&bookmarks);
  if(gem_site->url)
    free(gem_site->url);
  free_lines(gem_site);
  free(gem_site);
  tls_free(gem_tls);
  free_resp(resp);
  free_windows();
}
