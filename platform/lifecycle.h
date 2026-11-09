#ifndef REAKTOR_LIFECYCLE_H
#define REAKTOR_LIFECYCLE_H

#include <SDL3/SDL.h>

void reaktor_process_start(int console, const char *title);
void reaktor_session_watch(SDL_Window *win);
void reaktor_process_stopped(void);
void reaktor_process_end(void);

void reaktor_signal_watch(void);
void reaktor_signal_unwatch(void);
int  reaktor_signal_reason(int *again);

void reaktor_console_quit(void);
void reaktor_hard_quit(int code);
void reaktor_session_end(void);

#endif
