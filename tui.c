#include <ncurses.h>
#include <form.h>
#include <panel.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <signal.h>
#include <math.h>
#include <stdarg.h>

#include "tls.h"
#include "util.h"

#define MAIN_GEM_SITE "warmedal.se/~antenna/"
#define set_page_x(max_x) page_x = max_x - 2
#define set_page_y(max_y) page_y = max_y - search_bar_height - info_bar_height - 1 - 1

WINDOW *main_win = NULL, *search_bar_win = NULL, *info_bar_win = NULL, *mode_win = NULL;
PANEL  *dialog_panel = NULL; WINDOW *dialog_win = NULL, *dialog_subwin = NULL;
FIELD  *search_field[3];
FORM   *search_form;
const int search_bar_height = 1;
const int info_bar_height = 1;
const int scrolling_velocity = 2;
const int offset_x = 1;

int max_x, max_y;
int page_x, page_y;
const char *info_message;
char *dialog_message = NULL;

enum element {SEARCH_FORM, MAIN_WINDOW};
enum element current_focus = MAIN_WINDOW;

enum mode {SCROLL_MODE, LINKS_MODE};
enum mode current_mode = SCROLL_MODE;

enum color {LINK_COLOR = 1, H1_COLOR = 2, H2_COLOR = 3, H3_COLOR = 4, QUOTE_COLOR = 5, 
            DIALOG_COLOR = 6};

const char yes_no_options[] = {'y', 'n'};

bool is_dialog_hidden = true;

struct screen_line {
  char *text;
  char *link;
  int attr;
};

struct gemini_site {
  char *url;
  struct screen_line **lines;
  int lines_num;
  int first_line_index, last_line_index, selected_link_index;
};

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
  if(wnoutrefresh(search_bar_win) == ERR) goto err;

  doupdate();
  return;

err: 
  fprintf(stderr, "%s", "Cant refresh window\n");
  exit(EXIT_FAILURE);
}

// ########## INITIALIZATION ##########

static inline void init_colors() {
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
  dialog_win = newwin(lines, cols, max_y / 8, max_x / 8);
  dialog_subwin = derwin(dialog_win, lines - 2, cols - 2, 1, 1);
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
  set_page_x(max_x);
  set_page_y(max_y);
  
  main_win = newwin(page_y, max_x, search_bar_height + 1, 0);
  scrollok(main_win, true);
  keypad(main_win, true);
  
  search_bar_win = newwin(search_bar_height, max_x, 0, 0);
  keypad(search_bar_win, true);
  
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
  search_field[1] = new_field(search_bar_height, max_x - 6, 0, 6, 0, 0);
  set_field_back(search_field[1], A_UNDERLINE);
  set_field_opts(search_field[1], O_VISIBLE | O_PUBLIC | O_ACTIVE | O_EDIT);
  // 1024 is max url length
  set_max_field(search_field[1], 1024);
  field_opts_off(search_field[1], O_STATIC);

  search_form = new_form(search_field);
  set_form_win(search_form, search_bar_win);
  set_form_sub(search_form, derwin(search_bar_win, search_bar_height, max_x, 0, 0));
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
}

static void draw_scrollbar(struct gemini_site *gem_site) {
  if(gem_site->lines_num <= 0) return;

  float y;
  float scrollbar_height = (float)((page_y) * (page_y)) / (float)gem_site->lines_num;
  if(scrollbar_height < 1.0)
    scrollbar_height = 1.0;

  if(gem_site->first_line_index == 0) {
    y = 0;
  } 
  else if(gem_site->last_line_index == gem_site->lines_num) {
    y = max_y - 3 - scrollbar_height;
  }
  else {
    y = (float)(gem_site->first_line_index + 1) / (float)gem_site->lines_num;
    y = page_y * y;
    y = (int)(y + 0.5);
  }

  mvwvline(main_win, 0, max_x - 1, ' ', max_y - 2);
  mvwvline(main_win, y, max_x - 1, 0, scrollbar_height);
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


static int get_paragraph_attr(char **paragraph, char **link, const char **protocol, int *p_offset) {
  
  int attr = A_NORMAL;
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

      const char *https  = " [https]";
      const char *http   = " [http]";
      const char *gopher = " [gopher]";
      const char *mail   = " [mail]";
      const char *tmp_protocol = NULL;

      if(m_strncmp(tmp_para, "https://") == 0)
        tmp_protocol = https;
      else if(m_strncmp(tmp_para, "http://") == 0)
        tmp_protocol = http;
      else if(m_strncmp(tmp_para, "gopher://") == 0)
        tmp_protocol = gopher;
      else if(m_strncmp(tmp_para, "mailto:") == 0)
        tmp_protocol = mail;

      if(tmp_protocol != NULL)
        *protocol = tmp_protocol;

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
        *protocol = NULL;
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


static struct screen_line** paragraphs_to_lines(struct gemini_site *gem_site, char **paragraphs, int paragraphs_num) {
 
  struct screen_line **lines = (struct screen_line**) calloc(1, 1 * sizeof(struct screen_line *));
  int num_lines = 0;
  
  for(int i = 0; i < paragraphs_num; i++) {
  
    const char *protocol = NULL; 
    char *line_str = paragraphs[i], *link = NULL;
    bool is_last_line = false;
    int line_len, offset;
    
    int attr = get_paragraph_attr(&line_str, &link, &protocol, &offset);
    line_str += offset;

    if(protocol) {
      // if it's not a gemini protocol we need to append an information to the end of the link
      // so realloc, add to the end, and set line_str to our reallocated string + offset
      paragraphs[i] = realloc(paragraphs[i], strlen(paragraphs[i]) + strlen(protocol) + 1);
      strcat(paragraphs[i], protocol);
      line_str = paragraphs[i] + offset;
    }
    
    // loop over all lines in a paragraph
    do {
      // if at the end of the line we have a splited word
      // then move the whole word to the next line 
      int word_offset = 0;
      line_len = strlen(line_str);

      if(line_len <= page_x - offset_x)
        is_last_line = true;
      else
        line_len = page_x - offset_x;
      

      // check if word at the end of the line needs to be moved to the next line
      char *tmp_line = line_str + line_len;
      if(line_len == page_x - offset_x && *(line_str + line_len) != '\0') {  
        while(!isspace(*tmp_line) && tmp_line != line_str){
          word_offset++;
          tmp_line--;
        }
        // if we have one long line then let it be split
        // otherwise move 
        if(word_offset > (page_x - offset_x)/ 2) {
          word_offset = 0;
        }
      }
     
      line_len -= word_offset;

      // TODO do this only if there is no ''' paragraph
      if(*line_str != '\0' && isspace(*line_str)) {
        line_str++;
      }
    
      num_lines++;
      lines = realloc(lines, num_lines * sizeof(struct screen_line*));
      char *str = calloc(1, line_len + 1);
      memcpy(str, line_str, line_len);
      str[line_len] = '\0';
      
      struct screen_line *line = (struct screen_line*) calloc(1, 1 * sizeof(struct screen_line));
      line->text = str;
      line->attr = attr;
      line->link = link;
      lines[num_lines - 1] = line;

      line_str += line_len;
    } while(!is_last_line);
  }

  gem_site->lines_num = num_lines;
  
  for(int i = 0; i < paragraphs_num; i++)
    if(paragraphs[i])
      free(paragraphs[i]);
  free(paragraphs);

  return lines;
}


// ########## PRINTING ##########

static inline void print_current_mode() {
  werase(mode_win);
  if(current_mode == LINKS_MODE)
    wprintw(mode_win, "%c", 'L');
  else
    wprintw(mode_win, "%c", 'S');
}

static inline void info_bar_print(const char *str) {
  // const strings have static duration so its valid
  info_message = str;
  werase(info_bar_win);
  wprintw(info_bar_win, "%s", info_message);
  wrefresh(info_bar_win);
}


static void printline(struct screen_line *line, int x, int y) {
  // scrollok scrolls automatically to the next line when 
  // len == max_x so disable it temporarily
  scrollok(main_win, false);
  wattron(main_win, line->attr);
  
  if(line->text != NULL)
    mvwprintw(main_win, y, x, "%s", line->text);
  
  wattroff(main_win, line->attr);
  scrollok(main_win, true); 
}


static void print_gemini_site(struct gemini_site *gem_site, int first_line_index) {
  scrollok(main_win, false);
  // TODO if we resized the window too much and the text on down is now on up
  // then go up to make the text be on down
  int index = first_line_index;
  struct screen_line *line;
  // print all we can
  for(int i = 0; i < page_y; i++){
    line = gem_site->lines[index];
    printline(line, offset_x, i);
    index++;
    if(index >= gem_site->lines_num)
      break;
  }

  gem_site->last_line_index = index;
  gem_site->first_line_index = first_line_index;
  
  scrollok(main_win, true);
}

static inline void scrolldown(struct gemini_site *gem_site) {
  if(gem_site->last_line_index >= gem_site->lines_num || gem_site->lines == NULL)
    return;
  
  wscrl(main_win, 1);

  struct screen_line *line = gem_site->lines[gem_site->last_line_index];
  printline(line, offset_x, max_y - 5);

  gem_site->last_line_index++;
  gem_site->first_line_index++;
}


static inline void scrollup(struct gemini_site *gem_site) {
  if(gem_site->first_line_index == 0 || gem_site->lines == NULL)
    return;

  wscrl(main_win, -1);
  
  gem_site->first_line_index--;
  gem_site->last_line_index--;

  struct screen_line *line = gem_site->lines[gem_site->first_line_index];
  printline(line, offset_x, 0);
}

static void resize_screen(struct gemini_site *gem_site, struct response *resp) {

  endwin();
  refresh();
  getmaxyx(stdscr, max_y, max_x);

  if(max_y <= 5 || max_x <= 5) {
    fprintf(stderr, "%s", "Too small screen size\n");
    exit(EXIT_FAILURE);
  }  

  set_page_x(max_x);
  set_page_y(max_y);
  
  // info_bar win
  wresize(info_bar_win, 1, max_x - 2);
  mvwin(info_bar_win, max_y - 1, 0);
  wclear(info_bar_win);
  if(info_message)
    wprintw(info_bar_win, "%s", info_message);

  // mode win
  mvwin(mode_win, max_y - 1, max_x - 1);
  print_current_mode();

  // main win
  wresize(main_win, page_y, max_x);
  draw_borders();
  wclear(main_win);

  // search win
  form_driver(search_form, REQ_VALIDATION);
  char search_str[1024];
  memcpy(search_str, trim_whitespaces(field_buffer(search_field[1], 0)), sizeof(search_str));

  // form
  // unfortunately there's no way to just resize a form, we need to recreate it
  unpost_form(search_form);
  free_form(search_form);
  free_field(search_field[1]);
  
  init_search_form(true);
  set_field_buffer(search_field[1], 0, search_str);

  // dialog panel
  wclear(dialog_win);
  wclear(dialog_subwin);
  refresh_windows();
  
  int d_win_lines = max_y * (6.0 / 8.0);
  int d_win_cols  = max_x * (6.0 / 8.0);
  int d_win_pos_y = max_y / 8.0;
  int d_win_pos_x = max_x / 8.0;

  wresize(dialog_win, d_win_lines, d_win_cols);
  wresize(dialog_subwin, d_win_lines - 2, d_win_cols - 2);
  mvwin(dialog_win, d_win_pos_y, d_win_pos_x);
  mvwin(dialog_win, d_win_pos_y + 1, d_win_pos_x + 1);

  replace_panel(dialog_panel, dialog_win);
  if(dialog_message)
    wprintw(dialog_subwin, "%s", dialog_message);
  box(dialog_win, 0, 0);
  update_panels();
  doupdate();

  // set selected link to index 0, to start from the beginning of the page 
  gem_site->selected_link_index = 0;
  // if there was a response, then print it
  if(resp == NULL) 
    return;

  if(gem_site->lines != NULL)
    free_lines(gem_site);

  if(resp->body) {
    int paragraphs_num = 0;
    char **paragraphs = string_to_paragraphs(resp->body, &paragraphs_num);
    gem_site->lines = paragraphs_to_lines(gem_site, paragraphs, paragraphs_num);
  }
  else
    return;

  if(resp != NULL) 
    print_gemini_site(gem_site, 0);
}

static inline void edit_line_attr(struct gemini_site *gem_site, int index, int offset) {
  wmove(main_win, offset, 0);
  wclrtoeol(main_win);
  printline(gem_site->lines[index], offset_x, offset);
}


static inline void pagedown(struct gemini_site *gem_site) {
  if(gem_site->lines_num > page_y) {
    int start_line = 0;
    if(gem_site->last_line_index + page_y > gem_site->lines_num)
      start_line = gem_site->lines_num - page_y;
    else
      start_line = gem_site->last_line_index;
  
    werase(main_win);
    print_gemini_site(gem_site, start_line);
  }
}


static inline void pageup(struct gemini_site *gem_site) {
  if(gem_site->lines_num > page_y) {
    int start_line_index = 0;
    if(gem_site->first_line_index - page_y < 0)
      start_line_index = 0;
    else
      start_line_index = gem_site->first_line_index - page_y;

    werase(main_win);
    print_gemini_site(gem_site, start_line_index);
  }
}


static void nextlink(struct gemini_site *gem_site) {
  bool is_pagedown = false;
start:
  if(gem_site->last_line_index > gem_site->lines_num)
    return;
  if(gem_site->lines == NULL)
    return;

  int link_index = gem_site->selected_link_index;
  if(link_index == 0) { 
      link_index = gem_site->first_line_index - 1;
  }
  else if(link_index < gem_site->first_line_index || link_index >= gem_site->last_line_index){
    if(gem_site->first_line_index > 0) {
      gem_site->lines[link_index]->attr ^= A_STANDOUT; 
      link_index = gem_site->first_line_index - 1;
      gem_site->selected_link_index = 0;
    }
  }

  int offset = link_index - gem_site->first_line_index;
    
  // if the next link is on the current page, then just highlight it 
  for(int i = 1; i < gem_site->last_line_index - link_index; i++) {
    if(gem_site->lines[link_index + i]->link != NULL) {
      // unhighlight the old selected link
      if(gem_site->selected_link_index != 0) {
        gem_site->lines[link_index]->attr ^= A_STANDOUT;
        edit_line_attr(gem_site, link_index, offset);
      }
      // highlight the selected link
      gem_site->lines[link_index + i]->attr ^= A_STANDOUT;
      gem_site->selected_link_index = link_index + i;
      edit_line_attr(gem_site, link_index + i, offset + i);
      return;
    }
  }

  // if there's no link on the page, then go page down and find a link in a new page
  if(is_pagedown) return;
  is_pagedown = true;
  pagedown(gem_site);
  goto start;
}


static void prevlink(struct gemini_site *gem_site) {
  bool is_pageup = false;

start:
  if(gem_site->lines == NULL)
    return;
  if(gem_site->first_line_index < 0)
    return;

  int link_index = gem_site->selected_link_index;
  if(link_index == 0) {
      link_index = gem_site->last_line_index - 1;
  }
  else if(link_index < gem_site->first_line_index || link_index > gem_site->last_line_index){
    if(gem_site->first_line_index > 0) {
      gem_site->lines[link_index]->attr ^= A_STANDOUT;
      link_index = gem_site->last_line_index;
      gem_site->selected_link_index = 0;
    }
  }

  int offset = link_index - gem_site->first_line_index;
  // if the next link is on the current page, then just highlight it 
  for(int i = 1; i <= link_index - gem_site->first_line_index; i++) {
    if(link_index - i <= 0)
      return;
    
    if(gem_site->lines[link_index - i]->link != NULL) {      
      // unhighlight the old selected link
      if(gem_site->selected_link_index != 0) {
        gem_site->lines[link_index]->attr ^= A_STANDOUT;
        edit_line_attr(gem_site, link_index, offset);
      }
      // highlight the selected link
      gem_site->lines[link_index - i]->attr ^= A_STANDOUT;
      gem_site->selected_link_index = link_index - i;
      edit_line_attr(gem_site, link_index - i, offset - i);
      return;
    }
  }
  
  // if there's no link on the page, then go page up and find a link in a new page
  if(is_pageup) return;
  is_pageup = true;
  pageup(gem_site);
  goto start;
}


static char* handle_link_click(char *base_url, char *link) {

  char *new_url = strdup(base_url);

  if(!link)
    goto nullret;
 
  if(m_strncmp(link, "gemini://") == 0 || m_strncmp(link, "//") == 0) {
    if(link[0] == '/')
      link += 2;

    free(new_url);
    return strdup(link);
  }
  else if(m_strncmp(link, "gopher://") == 0) {
    wclear(info_bar_win);
    wprintw(info_bar_win, "%s", link);
    goto nullret;
  }
  else if(m_strncmp(link, "mailto:") == 0) {
    wclear(info_bar_win);
    wprintw(info_bar_win, "%s", link);
    goto nullret;
  }
  else if(m_strncmp(link, "https://") == 0 || m_strncmp(link, "http://") == 0) {
    wclear(info_bar_win);
    wprintw(info_bar_win, "%s", link);
    goto nullret;
  }
  else {
    assert(new_url);
    int url_length = strlen(new_url);
    char *p = new_url;

    // cut the gemini scheme from the url
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
    // if there's a relative path
    else if(m_strncmp(link, "./") == 0) {
      link++;
      char *chr;
      if((chr = strrchr(p, '/')) != NULL) {
        *chr = '\0';
      }
    }

    // if we need to go two or more directories up, then path travel
    else if(m_strncmp(link, "..") == 0) {
      char *chr;
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

nullret:
  free(new_url);
  return NULL;
}


// ########## DIALOG ##########

static void show_dialog() {
  wclear(dialog_win);
  wclear(dialog_subwin);
  wattron(dialog_win, COLOR_PAIR(H3_COLOR));
  box(dialog_win, 0, 0);
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

static inline void print_to_dialog(char *format, char *str) {

  if(dialog_message) {
    free(dialog_message);
    dialog_message = NULL;
  }
  
  if(str != NULL) {
    dialog_message = malloc(strlen(format) + strlen(str) + 1);
    sprintf(dialog_message, format, str);
  }
  else {
    dialog_message = strdup(format);
  }
  
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
  
  for(int i = 0; i < strlen(options); i++) {
    if(c == options[i])
      return options[i];
  }
  
  goto loop;
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
      show_dialog();
      new_resp->body[strlen(new_resp->body) - 2] = '\0';
      print_to_dialog("%s", new_resp->body);

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
      
      int header_offset = 1;
      char *body_p = new_resp->body;
      while(*body_p++ != '\n') header_offset++;
      
      char default_app[NAME_MAX + 1];
      char selected_opt;

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
      
      char *new_link = handle_link_click(gemini_url, redirect_link);
      
      if(was_redirected)
        free(gemini_url);
      
      show_dialog();
      if(new_link) { 
        print_to_dialog("Do you want to redirect to: \n%s? [y/n]", new_link);
      }
      else {
        print_to_dialog("Can't redirect to a new url.", NULL);
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


  if(gem_site->url) {
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

  gem_site->lines = paragraphs_to_lines(gem_site, paragraphs, paragraphs_num);
  gem_site->selected_link_index = 0;
 
  // print the response
  werase(main_win);
  print_gemini_site(gem_site, 0);
  
  // update search bar
  form_driver(search_form, REQ_CLR_FIELD);
  set_field_buffer(search_field[1], 0, gem_site->url);
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
  if(was_redirected)
    free(gemini_url);
  return 0;
}


int main() {

  // when server closes a pipe
  signal(SIGPIPE, SIG_IGN);
  // set encoding for emojis
  // however some emojis may not work
  setlocale(LC_ALL, "en_US.utf8");
  init_windows();
  init_search_form(false);
  draw_borders();
  print_current_mode();
  refresh_windows();
  init_dialog_panel();

  struct gemini_tls *gem_tls = init_tls(0);
  if(gem_tls == NULL) 
    exit(EXIT_FAILURE);

  struct response *resp = NULL;
  struct gemini_site *gem_site = calloc(1, sizeof(struct gemini_site));

  // mouse support
  MEVENT event;
  mousemask(BUTTON5_PRESSED | BUTTON4_PRESSED, NULL);

  int ch = 0;
  while(ch != KEY_F(1)) {
  
    draw_scrollbar(gem_site);
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
              scrolldown(gem_site);
          }
          else
            nextlink(gem_site);
          break;
        
        case KEY_UP:
          if(current_mode == SCROLL_MODE) {
            for(int i = 0; i < scrolling_velocity; i++)
              scrollup(gem_site);
          }
          else
            prevlink(gem_site);
          break;

        case KEY_NPAGE:
          pagedown(gem_site);
          break;
        case KEY_PPAGE:
          pageup(gem_site);
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
        
        case 'Q':
        case 'q':
          if(current_mode == SCROLL_MODE)
            current_mode = LINKS_MODE;
          else
            current_mode = SCROLL_MODE;
    
          print_current_mode();
          break;
        
        case 'S':
        case 's':
          if(!gem_site->url || resp == NULL) break;
          
          print_to_dialog("Do you want to save the gemsite? [y/n]", NULL);
          char selected_opt = dialog_ask(gem_site, resp, yes_no_options);
          if(selected_opt == 'y')
            save_gemsite(gem_site->url, resp);

          break;

        // enter
        case 10:
          if(current_mode == LINKS_MODE) {
            char *url = NULL;
            if(
               gem_site->selected_link_index == 0 || 
               gem_site->selected_link_index > gem_site->lines_num
              ) break;
            
            char *link = gem_site->lines[gem_site->selected_link_index]->link;
            if((url = handle_link_click(gem_site->url, link)) == NULL) 
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
              scrolldown(gem_site);
          else if (event.bstate & BUTTON4_PRESSED)
            for(int i = 0; i < scrolling_velocity; i++)
              scrollup(gem_site);
        }
        break;
      }
    }

    // current_focus == SEARCH_FORM
    else {
      switch(ch) {
        case KEY_DOWN:
        case KEY_UP:
          current_focus = MAIN_WINDOW;
          curs_set(0);
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
         
          char *url = strdup(trim_whitespaces(field_buffer(search_field[1], 0)));
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

          free(url);
          break;

        default:
//          curs_set(1);
          form_driver(search_form, ch);
          break;
      }
    }

    if(ch == KEY_RESIZE){
      resize_screen(gem_site, resp);
    }
  }
  
  tls_free(gem_tls);
  free_resp(resp);
  free_windows();
}
