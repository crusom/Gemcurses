#ifndef WCWIDTH_H
#define WCWIDTH_H

int mk_wcwidth(wchar_t ucs);
int mk_wcswidth(const wchar_t *pwcs, size_t n);
int mk_wcwidth_cjk(wchar_t ucs);

#endif
