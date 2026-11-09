#ifndef REAKTOR_TEXT_PUBLIC_H
#define REAKTOR_TEXT_PUBLIC_H

int  reaktor_text_break(const char *s, int len, int limit);
int  reaktor_text_rtl(const char *s, int len);
void reaktor_text_describe(char *buf, int cap);

#endif
