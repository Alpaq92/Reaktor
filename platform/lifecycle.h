#ifndef REAKTOR_LIFECYCLE_H
#define REAKTOR_LIFECYCLE_H

#include <SDL3/SDL.h>

/* Before SDL_Init. */
void reaktor_process_start(int console, const char *title);
void reaktor_session_watch(SDL_Window *win);
void reaktor_process_stopped(void);
void reaktor_process_end(void);

/* After SDL_Init. A bare quit's reason as reaktor_quit_reason + 1, or 0;
   *again for a second Ctrl+C. */
void reaktor_signal_watch(void);
void reaktor_signal_unwatch(void);
int  reaktor_signal_reason(int *again);

/* The runtime's: the console asks the app, a hard quit leaves with code. */
void reaktor_console_quit(void);
void reaktor_hard_quit(int code);
void reaktor_session_end(void);

#endif
