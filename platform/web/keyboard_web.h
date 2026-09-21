#ifndef REAKTOR_KEYBOARD_WEB_H
#define REAKTOR_KEYBOARD_WEB_H

void reaktor_web_keys_init(void (*woke)(void));
void reaktor_web_keys_wants(int wants);
int  reaktor_web_keys_holds(void);

/* The next typed rune, or 0. */
int  reaktor_web_keys_take(void);

#endif
