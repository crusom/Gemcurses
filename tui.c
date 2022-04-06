#include <ncurses.h>
#include <form.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>

#include <tls.h>

#define MAIN_GEM_SITE "warmedal.se/~antenna/"
#define set_page_x(max_x) page_x = max_x - 2
#define set_page_y(max_y) page_y = max_y - search_bar_height - info_bar_height - 1 - 1

/*
  KNOWN BUGS:
  - probably a lot, tell me if u find any,
*/

/*
  TODO:
  - links mode enhance,
  - add some cool scroll bars, 
  - quote paragraphs support,
  - panels,
  - handling different response codes,
  - colors,
*/

WINDOW *main_win = NULL, *search_bar_win = NULL, *info_bar_win = NULL, *mode_win = NULL;

FIELD *search_field[3];
FORM  *search_form;
const int search_bar_height = 1;
const int info_bar_height = 1;
const int scrolling_velocity = 1;

int max_x, max_y;
int page_x, page_y;
const char *info_message;

enum element {SEARCH_FORM, MAIN_WINDOW};
enum element current_focus = MAIN_WINDOW;

enum mode {SCROLL_MODE, LINKS_MODE};
enum mode current_mode = SCROLL_MODE;

struct screen_line {
  char *text;
  char *link;
  int attr;
};


struct gemini_site {
  struct screen_line **lines;
  int lines_num;
  int first_line_index, last_line_index, selected_link_index;
  char *url;
};


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
}

// TODO
void init_colors() {
  start_color();
}

int init_windows() {
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
  
  return 1;
}

static void create_form(bool resize) {
  // search_field[0] is a static field so we need to create it only once 
  if(!resize) {
    // coords are relative to the window
    // [0]
    search_field[0] = new_field(search_bar_height, 5, 0, 0, 0, 0);
    field_opts_off(search_field[0], O_EDIT);
    set_field_opts(search_field[0], O_VISIBLE | O_PUBLIC | O_AUTOSKIP);
    // [2]
    search_field[2] = NULL;
    set_field_buffer(search_field[0], 0, "url:");
  }
  // [1]
  search_field[1] = new_field(search_bar_height, max_x - 5, 0, 5, 0, 0);
  set_field_back(search_field[1], A_UNDERLINE);
  set_field_opts(search_field[1], O_VISIBLE | O_PUBLIC | O_ACTIVE | O_EDIT);
  set_max_field(search_field[1], 1024);
  field_opts_off(search_field[1], O_STATIC);

  search_form = new_form(search_field);
  set_form_win(search_form, search_bar_win);
  set_form_sub(search_form, derwin(search_bar_win, search_bar_height, max_x, 0, 0));
  post_form(search_form);
}


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


static void draw_borders() {
  mvhline(search_bar_height, 0, 0, max_x);
  mvhline(max_y - 2, 0, 0, max_x);
  mvvline(max_y - 1, max_x - 2, 0, 1);
}


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


static char **string_to_paragraphs(char *str, int *paragraphs_num) {
  
  char *str_tmp = str;
  int i = 0, num_newlines = 0;
  // count new paragraphs to know how many gemini_paragraph to allocate
  while(str_tmp[i] != '\0') {
    if(str_tmp[i] == '\n') {
      num_newlines++;
    }
    i++;
  }
  
  char **paragraphs = (char **) calloc(1, num_newlines * sizeof(char*));

  char *str_start = str;
  int index_line = 0;
  // fill 'paragraphs'
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


static int get_paragraph_attr(char **paragraph, char **link) {
  
  int attr = A_NORMAL;
  int offset = 0;
  if(strlen(*paragraph) > 0) {

    if(strncmp(*paragraph, "###", 3) == 0) {
      attr |= A_BOLD | A_ITALIC;
      offset = 3;
    }
    else if(strncmp(*paragraph, "##", 2) == 0) {
      attr |= A_BOLD | A_ITALIC;
      offset = 2;
    }
    else if(strncmp(*paragraph, "#", 1) == 0) {
      attr |= A_BOLD;
      offset = 1;
    }
    else if(strncmp(*paragraph, ">", 1) == 0) {
      attr |= A_ITALIC;
      offset = 1;
    }
    else if(strncmp(*paragraph, "=>", 2) == 0) {
      
      // Lines beginning with the two characters "=>" are link lines, which have the following syntax:
      // =>[<whitespace>]<URL>[<whitespace><USER-FRIENDLY LINK NAME>]

      attr |= A_UNDERLINE;
      offset = 2;
      char *tmp_para = *paragraph + 2;
      
      // skip the first whitespace
      while(*tmp_para != '\0' && isspace(*tmp_para)) {
        tmp_para++;
        offset++;
      }

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
      if(*tmp_para == '\0')
        offset = link_start - *paragraph;
    }
  }
  
  *paragraph += offset;

  return attr;
}


static struct screen_line** string_to_lines(struct gemini_site *gem_site, char *str_body) {
 
  int paragraphs_num = 0;
  char **paragraphs = string_to_paragraphs(str_body, &paragraphs_num);
  struct screen_line **lines = (struct screen_line**) calloc(1, 1 * sizeof(struct screen_line *));
  int num_lines = 0;

  for(int i = 0; i < paragraphs_num; i++) {
  
    char *line_str = paragraphs[i], *link = NULL; 
    int line_len;
    bool is_last_line = true;
    
    int attr = get_paragraph_attr(&line_str, &link);
    do {
    // if at the end of the line we have a splited word
    // then move the whole word to the next line 
      int word_offset = 0;
      line_len = strlen(line_str);

      if(line_len <= page_x)
        is_last_line = false;
      else
        line_len = page_x;
      

      // check if word at the end of the line needs to be moved to the next line
      char *tmp_line = line_str + line_len;
      if(line_len == page_x && *(line_str + line_len) != '\0') {  
        while(!isspace(*tmp_line) && tmp_line != line_str){
          word_offset++;
          tmp_line--;
        }
        // if we have one long line then let it be split
        // otherwise move 
        if(word_offset > page_x / 2) {
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
    } while(is_last_line);
  }

  gem_site->lines_num = num_lines;
  
  for(int i = 0; i < paragraphs_num; i++)
    free(paragraphs[i]);
  free(paragraphs);

  return lines;
}


static void free_lines(struct gemini_site *gem_site) {
  
  for(int i = 0; i < gem_site->lines_num; i++) {
    if(gem_site->lines[i]) {
      
      free(gem_site->lines[i]->text);
      
      if(gem_site->lines[i]->link != NULL) {
        char *p = gem_site->lines[i]->link;
        int j = 0;
        free(p);

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


static inline void scrolldown(struct gemini_site *gem_site) {
  if(gem_site->last_line_index >= gem_site->lines_num || gem_site->lines == NULL)
    return;
  
  wscrl(main_win, 1);

  struct screen_line *line = gem_site->lines[gem_site->last_line_index];
  printline(line, 0, max_y - 5);

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
  printline(line, 0, 0);
}


static void print_gemini_site(struct gemini_site *gem_site, int first_line_index) {
  scrollok(main_win, false);
  // TODO 
  // if we resized the window too much and the text on down is now on up
  // then go up to make the text be on down
  int index = first_line_index;
  struct screen_line *line;
  // print all we can
  for(int i = 0; i < page_y; i++){
    line = gem_site->lines[index];
    printline(line, 0, i);
    index++;
    if(index >= gem_site->lines_num)
      break;
  }

  gem_site->last_line_index = index;
  gem_site->first_line_index = first_line_index;
  
  scrollok(main_win, true);
}


static inline void edit_line_attr(struct gemini_site *gem_site, int index, int offset, int attr) {
    gem_site->lines[index]->attr = attr;
    wmove(main_win, offset, 0);
    wclrtoeol(main_win);
    printline(gem_site->lines[index], 0, offset);
}


static void nextlink(struct gemini_site *gem_site) {
  if(gem_site->last_line_index > gem_site->lines_num)
    return;
  if(gem_site->lines == NULL)
    return;

  // TODO 

  int link_index = gem_site->selected_link_index;
  if(link_index == 0) { 
      link_index = gem_site->first_line_index - 1;
  }
  else if(link_index < gem_site->first_line_index || link_index >= gem_site->last_line_index){
    if(gem_site->first_line_index > 0) {
      gem_site->lines[link_index]->attr = A_NORMAL | A_UNDERLINE; 
      link_index = gem_site->first_line_index - 1;
      gem_site->selected_link_index = 0;
    }
  }

  int offset = link_index - gem_site->first_line_index;
 

  // if the next link is on the current page, then just highlight it 
  for(int i = 1; i < gem_site->last_line_index - link_index; i++) {
    if(gem_site->lines[link_index + i]->link != NULL) {
      // unhighlight the old selected link
      if(gem_site->selected_link_index != 0)
        edit_line_attr(gem_site, link_index, offset, A_NORMAL | A_UNDERLINE);
      
      // highlight the selected link
      gem_site->selected_link_index = link_index + i;
      edit_line_attr(gem_site, link_index + i, offset + i, A_STANDOUT);
      return;
    }
  }


  // if there's no link on the page, then go page down
  if(gem_site->lines_num > page_y) {
    int start_line = 0;
    if(gem_site->last_line_index + page_y > gem_site->lines_num)
      start_line = gem_site->lines_num - page_y;
    else
      start_line = gem_site->last_line_index - 1;
    
    werase(main_win);
    print_gemini_site(gem_site, start_line);
  }
}


static void prevlink(struct gemini_site *gem_site) {
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
      gem_site->lines[link_index]->attr = A_NORMAL | A_UNDERLINE;
      link_index = gem_site->last_line_index - 1;
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
      if(gem_site->selected_link_index != 0)
        edit_line_attr(gem_site, link_index, offset, A_NORMAL | A_UNDERLINE);
      // highlight the selected link
      gem_site->selected_link_index = link_index - i;
      edit_line_attr(gem_site, link_index - i, offset - i, A_STANDOUT);
      return;
    }
  }
  
  // if there's no link on the page, then go page up
  if(gem_site->lines_num > page_y) {
    int start_line_index = 0;
    if(gem_site->first_line_index - page_y < 0)
      start_line_index = 0;
    else
      start_line_index = gem_site->first_line_index - page_y + 1;

    werase(main_win);
    print_gemini_site(gem_site, start_line_index);
  }
}


static int request_gem_site(char *gemini_url, struct gemini_tls *gem_tls, struct gemini_site *gem_site, struct response **resp) {

    info_bar_print("Connecting..."); 
    refresh_windows();        
    
    struct response *new_resp = tls_request(gem_tls, gemini_url);

    if(new_resp == NULL)
      return 0;

    if(new_resp->error_message != NULL) {
      info_bar_print(new_resp->error_message);
      free_resp(new_resp);
      return 0;
    }
    
    werase(main_win);
    if(new_resp->body == NULL || new_resp->body[0] == '\0') {
      info_bar_print("Empty body");
      free_resp(new_resp);
      return 0;
    }

    if(gem_site->lines)
      free_lines(gem_site);

    if(*resp != NULL)
      free_resp(*resp); 
      
    *resp = new_resp;

    gem_site->lines = string_to_lines(gem_site, (*resp)->body);
    gem_site->selected_link_index = 0;
    print_gemini_site(gem_site, 0);

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
  char *search_str = strdup(trim_whitespaces(field_buffer(search_field[1], 0)));

  unpost_form(search_form);
  free_form(search_form);
  free_field(search_field[1]);
 
  // unfortunately there's no way to just resize a form, we need to recreate it
  create_form(true);
  set_field_buffer(search_field[1], 0, search_str);
  free(search_str);

  gem_site->selected_link_index = 0;

  // if there was a response, then print it
  if(resp == NULL) 
    return;

  if(gem_site->lines != NULL)
    free_lines(gem_site);

  if(resp->body)
    gem_site->lines = string_to_lines(gem_site, resp->body);
  else
    return;

  if(resp != NULL) 
    print_gemini_site(gem_site, 0);
}


int main() {
  // set encoding for emojis
  // however some emojis may not work
  setlocale(LC_ALL, "en_US.utf8");
  init_windows();
  create_form(false);
  draw_borders();
  print_current_mode();
  refresh_windows();

  struct gemini_tls *gem_tls = init_tls(0);
  if(gem_tls == NULL) return -1;
  
  struct response *resp = NULL;
  struct gemini_site *gem_site = calloc(1, sizeof(struct gemini_site));

  // mouse support
  MEVENT event;
  mousemask(BUTTON5_PRESSED | BUTTON4_PRESSED, NULL);

  int ch = 0;
  while(ch != KEY_F(1)) {
   
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

        case 'B':
          if(gem_site->url) {
            free(gem_site->url);
            gem_site->url = NULL;
          }
          
          gem_site->url = strdup(MAIN_GEM_SITE);
          request_gem_site(gem_site->url, gem_tls, gem_site, &resp);
          curs_set(0);
          form_driver(search_form, REQ_CLR_FIELD);
          set_field_buffer(search_field[1], 0, gem_site->url);
          refresh();
          break;

        case 'Q':
        case 'q':
          if(current_mode == SCROLL_MODE)
            current_mode = LINKS_MODE;
          else
            current_mode = SCROLL_MODE;
    
          print_current_mode();

          break;
        // enter
        case 10:
          if(current_mode == LINKS_MODE) {
            if(gem_site->selected_link_index == 0 || gem_site->selected_link_index > gem_site->lines_num) 
              break;

            char *link = gem_site->lines[gem_site->selected_link_index]->link;
            
            if(gem_site->selected_link_index && link) {
              if(strncmp(link, "gemini://", 9) == 0 || strncmp(link, "//", 2) == 0) {
                if(gem_site->url)
                  free(gem_site->url);

                if(link[0] == '/') link += 2;
                gem_site->url = strdup(link);
              }
   
              else if(strncmp(link, "gopher://", 9) == 0) {
                info_bar_print("Gopher links not supported");
                break;
              }
              
              else if(strncmp(link, "https://", 8) == 0 || strncmp(link, "http://", 7) == 0) {
//                info_bar_print("Web links not supported");
                wclear(info_bar_win);
                wprintw(info_bar_win, "%s", link);
                break;
              }
              
              else {
                assert(gem_site->url);
                int url_length = strlen(gem_site->url);
                char *p = gem_site->url, *chr;

                // cut the gemini scheme from the url
                if(strncmp(gem_site->url, "gemini://", 9) == 0) p += 9;
                // if there's an absolute path
                if(strncmp(link, "/", 1) == 0 || strncmp(link, "./", 2) == 0) {
                
                  if(link[0] == '.') link++;
                  
                  if((chr = strchr(p, '/')) != NULL) {
                    url_length = chr - gem_site->url;
                    gem_site->url[url_length] = '\0';
                  }
                }

                // if we need to go one directory up
                else if(strncmp(link, ".", 1) == 0) {
                  char *pageup;
                  if((pageup = strrchr(p, '/')) != NULL) {
                    *(pageup) = '\0';
                  }
                  // if we need to go two directories up
                  if(strncmp(link, "..", 2) == 0) {
                    if((pageup = strrchr(p, '/')) != NULL) {
                      *(pageup) = '\0';
                      link++;
                    }
                  }
                  
                  link++;
                }
                // just concatenate it to the current path
                else {
                  char *last;
                  if((last = strrchr(p, '/')) != NULL) {
                    url_length = last - gem_site->url + 1;
                    gem_site->url[url_length] = '\0';
                  }
                }

                gem_site->url = realloc(gem_site->url, url_length + strlen(link) + 1);
                strcat(gem_site->url, link);
              }

              int res = request_gem_site(
                  gem_site->url, 
                  gem_tls, 
                  gem_site, 
                  &resp
              );
              
              werase(info_bar_win);
              wprintw(info_bar_win, "%s", gem_site->url);
              if(res) {
                curs_set(0);
                form_driver(search_form, REQ_CLR_FIELD);
                set_field_buffer(search_field[1], 0, gem_site->url);
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
              scrolldown(gem_site);
          else if (event.bstate & BUTTON4_PRESSED)
            for(int i = 0; i < scrolling_velocity; i++)
              scrollup(gem_site);
        }
        break;
      }
    }

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
          char *old_url = gem_site->url;
          
          form_driver(search_form, REQ_VALIDATION);
          gem_site->url = strdup(trim_whitespaces(field_buffer(search_field[1], 0)));
          wmove(main_win, 0, 0);
          form_driver(search_form, REQ_PREV_FIELD);

          // go back to main window
          current_focus = MAIN_WINDOW;
          curs_set(0);
          form_driver(search_form, REQ_PREV_FIELD);
          form_driver(search_form, REQ_END_LINE);
         
          int res = request_gem_site(gem_site->url, gem_tls, gem_site, &resp);
          if(!res) {
            free(gem_site->url);
            gem_site->url = old_url;
          }
          else {
            free(old_url);
          }

          break;

        default:
          curs_set(1);
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
  if(gem_site->lines != NULL)
    free_lines(gem_site);
}
