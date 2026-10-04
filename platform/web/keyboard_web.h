#ifndef REAKTOR_KEYBOARD_WEB_H
#define REAKTOR_KEYBOARD_WEB_H

void reaktor_web_keys_init(unsigned event_type);
void reaktor_web_keys_wants(int wants);

enum {
    REAKTOR_WEB_KEY_BACKSPACE = -1, REAKTOR_WEB_KEY_ENTER = -2,
    REAKTOR_WEB_KEY_LEFT = -3, REAKTOR_WEB_KEY_RIGHT = -4,
    REAKTOR_WEB_KEY_UP = -5, REAKTOR_WEB_KEY_DOWN = -6,
    REAKTOR_WEB_KEY_HOME = -7, REAKTOR_WEB_KEY_END = -8,
    REAKTOR_WEB_KEY_DELETE = -9
};

#endif
