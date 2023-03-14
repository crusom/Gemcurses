#include <stdlib.h>
//#include <wchar.h>
//#include <wctype.h>
#include "wcwidth.h"

wchar_t get_wchar(const char *s) {
  wchar_t buffer[2];
  //swprintf(buffer, 2, L"%hs", s);  
  mbstowcs(buffer, s, 2);  
  return buffer[0];
}

int utf8_get_char_width(const char *s) {
  wchar_t wc = get_wchar(s);
  // If wc is a NULL or non-spacing wide character, wcwidth() returns 0.
  // If wc is not a printing wide character, wcwidth() returns -1. 
  int ret = mk_wcwidth(wc);
  // for what i understand, not a printing wide character may be for example an Invisible Operator.
  // eg. Invisible Plus "To ensure that 3¼ means 3 plus ¼—in uses where it is not possible to rely on a human 
  // reader to disambiguate the implied intent of juxtaposition—the invisible plus operator is
  // used. In such uses, not having an operator at all would imply multiplication."
  // https://www.unicode.org/versions/Unicode13.0.0/ch22.pdf
  if(ret == -1)
    ret = 0;

  return ret;
}

int utf8_to_bytes(char *s, int n) {
  int len = 0, all_bytes = 0;
  unsigned char *p = (unsigned char*)s;
  while(*p && len != n) {
    int j = 1;
    if(*p & 0x80) {
      while((*p << j) & 0x80) j++;
    }
    p += j;
    all_bytes += j;
    len++;
  }
  return all_bytes;
}

int utf8_strwidth(char *s) {
  int len = 0;
  unsigned char *p = (unsigned char*)s;
  while(*p != '\0') {
    len += utf8_get_char_width((const char*)p);
    int j = 1;
    if(*p & 0x80) {
      while((*p << j) & 0x80) j++;
    }
    p += j;
  }
  return len; 
}



int utf8_strnwidth(char *s, int n) {
  int chars_n = 0;
  int len = 0;
  unsigned char *p = (unsigned char*)s;
  while(*p != '\0' && chars_n != n) {
    len += utf8_get_char_width((const char*)p);
    chars_n++;
    int j = 1;
    if(*p & 0x80) {
      while((*p << j) & 0x80) j++;
    }
    p += j;
  }
  return len;
}

int utf8_max_chars_in_width(char *s, int n) {
  int chars_n = 0;
  int len = 0;
  unsigned char *p = (unsigned char*)s;
  while(*p != '\0') {
    if (len + utf8_get_char_width((const char*)p) > n)
      break;
    
    len += utf8_get_char_width((const char*)p);
    chars_n++;
    int j = 1;
    if(*p & 0x80) {
      while((*p << j) & 0x80) j++;
    }
    p += j;
  }
  return chars_n;
}



int utf8_max_chars_in_bytes(const char *s, int n) {
  int chars_n = 0;
  int len = 0;
  unsigned char *p = (unsigned char*)s;
  while(*p) {
    int j = 1;
    if(*p & 0x80) {
      while((*p << j) & 0x80) j++;
    }
    if(len + j > n)
      break;
    
    p += j;
    len += j;
    chars_n += 1;
  }
  return chars_n;
}
