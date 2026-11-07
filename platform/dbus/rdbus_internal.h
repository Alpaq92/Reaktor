#ifndef REAKTOR_RDBUS_INTERNAL_H
#define REAKTOR_RDBUS_INTERNAL_H

#include "rdbus.h"

/* How many bytes the message starting at b takes in all, once its first 16
   are in: 0 while fewer are, -1 for a header no valid message has. A frame
   of a type above RDBUS_MESSAGE_SIGNAL has a size, but rdbus_msg_parse
   answers NULL for it; the specification says to skip such messages. */
long rdbus__frame_size(const uint8_t *b, size_t have);

/* Sets e, when given, to name and a formatted message. */
void rdbus__error_set(rdbus_error *e, const char *name, const char *fmt, ...);

/* Whether the message's NO_REPLY_EXPECTED flag is set. */
int  rdbus__msg_no_reply(const rdbus_msg *m);

#endif
