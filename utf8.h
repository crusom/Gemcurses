#ifndef UTF8_H
#define UTF8_H
#include <stddef.h>

wchar_t get_wchar(const char *s);
int utf8_get_char_width(const char *s);
int utf8_to_bytes(char *s, int n);
int utf8_strwidth(char *s);
int utf8_strnwidth(char *s, int n);
int utf8_max_chars_in_width(char *s, int n);
int utf8_max_chars_in_bytes(const char *s, int n);

#endif
