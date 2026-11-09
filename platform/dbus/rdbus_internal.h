#ifndef REAKTOR_RDBUS_INTERNAL_H
#define REAKTOR_RDBUS_INTERNAL_H

#include "rdbus.h"

long rdbus__frame_size(const uint8_t *b, size_t have);

void rdbus__error_set(rdbus_error *e, const char *name, const char *fmt, ...);

int  rdbus__msg_no_reply(const rdbus_msg *m);

#endif
