#ifndef REAKTOR_RDBUS_H
#define REAKTOR_RDBUS_H

/* A D-Bus client for Linux and the BSDs: EXTERNAL authentication over a Unix
   socket, the message format, and what the AT-SPI bridge, the tray and
   notifications ask of a bus. Ported from minidbus (MIT; see NOTICE.md). */

#include <stddef.h>
#include <stdint.h>

#define RDBUS_TYPE_INVALID     0
#define RDBUS_TYPE_BYTE        'y'
#define RDBUS_TYPE_BOOLEAN     'b'
#define RDBUS_TYPE_INT16       'n'
#define RDBUS_TYPE_UINT16      'q'
#define RDBUS_TYPE_INT32       'i'
#define RDBUS_TYPE_UINT32      'u'
#define RDBUS_TYPE_INT64       'x'
#define RDBUS_TYPE_UINT64      't'
#define RDBUS_TYPE_DOUBLE      'd'
#define RDBUS_TYPE_STRING      's'
#define RDBUS_TYPE_OBJECT_PATH 'o'
#define RDBUS_TYPE_SIGNATURE   'g'
#define RDBUS_TYPE_ARRAY       'a'
#define RDBUS_TYPE_VARIANT     'v'
#define RDBUS_TYPE_STRUCT      'r'
#define RDBUS_TYPE_DICT_ENTRY  'e'

#define RDBUS_MESSAGE_METHOD_CALL   1
#define RDBUS_MESSAGE_METHOD_RETURN 2
#define RDBUS_MESSAGE_ERROR         3
#define RDBUS_MESSAGE_SIGNAL        4

#define RDBUS_SERVICE_DBUS             "org.freedesktop.DBus"
#define RDBUS_PATH_DBUS                "/org/freedesktop/DBus"
#define RDBUS_INTERFACE_DBUS           "org.freedesktop.DBus"
#define RDBUS_INTERFACE_PROPERTIES     "org.freedesktop.DBus.Properties"
#define RDBUS_INTERFACE_INTROSPECTABLE "org.freedesktop.DBus.Introspectable"
#define RDBUS_INTROSPECT_DOCTYPE \
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n" \
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"

#define RDBUS_ERROR_FAILED             "org.freedesktop.DBus.Error.Failed"
#define RDBUS_ERROR_NO_REPLY           "org.freedesktop.DBus.Error.NoReply"
#define RDBUS_ERROR_DISCONNECTED       "org.freedesktop.DBus.Error.Disconnected"
#define RDBUS_ERROR_INVALID_ARGS       "org.freedesktop.DBus.Error.InvalidArgs"
#define RDBUS_ERROR_UNKNOWN_METHOD     "org.freedesktop.DBus.Error.UnknownMethod"
#define RDBUS_ERROR_UNKNOWN_OBJECT     "org.freedesktop.DBus.Error.UnknownObject"
#define RDBUS_ERROR_UNKNOWN_PROPERTY   "org.freedesktop.DBus.Error.UnknownProperty"
#define RDBUS_ERROR_PROPERTY_READ_ONLY "org.freedesktop.DBus.Error.PropertyReadOnly"
#define RDBUS_ERROR_NOT_SUPPORTED      "org.freedesktop.DBus.Error.NotSupported"

#define RDBUS_NAME_FLAG_DO_NOT_QUEUE          4
#define RDBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER 1

#define RDBUS_HANDLED          1
#define RDBUS_NOT_YET_HANDLED  0

typedef uint32_t rdbus_bool;

typedef struct rdbus_conn rdbus_conn;
typedef struct rdbus_msg  rdbus_msg;

typedef struct rdbus_error {
    int  set;
    char name[128];
    char message[256];
} rdbus_error;

/* Walks a message body for reading, or builds one. Its fields are the
   implementation's; callers only declare one. */
typedef struct rdbus_iter {
    rdbus_msg *msg;
    int        mode;
    int        container;
    int        failed;
    int        depth;
    size_t     pos;
    size_t     end;
    size_t     len_at;
    size_t     first;
    char       sig[256];
    size_t     sig_at;
    size_t     sig_len;
} rdbus_iter;

/* A handler answers RDBUS_HANDLED or RDBUS_NOT_YET_HANDLED. It runs on the
   thread that dispatches, with no lock held, and may send. */
typedef int (*rdbus_handler)(rdbus_conn *c, rdbus_msg *m, void *user);

void rdbus_error_init(rdbus_error *e);
int  rdbus_error_is_set(const rdbus_error *e);
void rdbus_error_free(rdbus_error *e);

/* The session bus, shared by every caller: each call takes a reference that
   rdbus_unref gives back. */
rdbus_conn *rdbus_session(rdbus_error *err);
/* A connection of the caller's own to the session bus. */
rdbus_conn *rdbus_session_private(rdbus_error *err);
/* A connection of the caller's own to the bus at a D-Bus address, such as the
   one org.a11y.Bus.GetAddress answers. Both authenticate and say Hello. */
rdbus_conn *rdbus_open(const char *address, rdbus_error *err);
/* Drops one reference; the last closes the socket and frees the connection. */
void        rdbus_unref(rdbus_conn *c);

const char *rdbus_unique_name(rdbus_conn *c);
/* The socket, for a thread that polls it; -1 once disconnected. */
int         rdbus_fd(rdbus_conn *c);
int         rdbus_connected(rdbus_conn *c);

/* Queues the message, assigning its serial, and writes what the socket takes
   now. Any thread may send. Answers 0 for a message that failed to build or a
   closed connection. */
int         rdbus_send(rdbus_conn *c, rdbus_msg *m, uint32_t *serial);
/* Writes everything queued, waiting as long as the socket needs. */
int         rdbus_flush(rdbus_conn *c);
/* Sends a method call and waits up to timeout_ms for its reply. Messages that
   arrive meanwhile stay queued for dispatch. An error reply, a timeout or a
   disconnect answers NULL with err set. */
rdbus_msg  *rdbus_call(rdbus_conn *c, rdbus_msg *m, int timeout_ms, rdbus_error *err);

/* Reads what has arrived, waiting up to timeout_ms (0: not at all) for the
   first byte, and writes what is queued. Answers 0 once disconnected. */
int         rdbus_read_write(rdbus_conn *c, int timeout_ms);
/* Hands the oldest queued message to the filters, then to the handler of its
   object path or the nearest fallback above it. A method call nobody handles
   is answered with UnknownMethod. Answers 1 while more remain queued. */
int         rdbus_dispatch(rdbus_conn *c);
/* rdbus_read_write, then every queued message dispatched. Answers 0 once
   disconnected. */
int         rdbus_read_write_dispatch(rdbus_conn *c, int timeout_ms);

int rdbus_add_filter(rdbus_conn *c, rdbus_handler fn, void *user);
int rdbus_register_path(rdbus_conn *c, const char *path, rdbus_handler fn, void *user);
/* Handles the path and every path below it. */
int rdbus_register_fallback(rdbus_conn *c, const char *path, rdbus_handler fn, void *user);

/* org.freedesktop.DBus.RequestName: the reply code, or -1 with err set. */
int rdbus_request_name(rdbus_conn *c, const char *name, unsigned flags, rdbus_error *err);
/* org.freedesktop.DBus.AddMatch, waiting for its reply. */
int rdbus_add_match(rdbus_conn *c, const char *rule, rdbus_error *err);

rdbus_msg *rdbus_msg_call(const char *dest, const char *path, const char *iface,
                          const char *member);
rdbus_msg *rdbus_msg_return(const rdbus_msg *call);
rdbus_msg *rdbus_msg_error(const rdbus_msg *call, const char *name, const char *text);
rdbus_msg *rdbus_msg_signal(const char *path, const char *iface, const char *member);
void       rdbus_msg_free(rdbus_msg *m);

int         rdbus_msg_type(const rdbus_msg *m);
uint32_t    rdbus_msg_serial(const rdbus_msg *m);
uint32_t    rdbus_msg_reply_serial(const rdbus_msg *m);
/* NULL when the header has no such field. */
const char *rdbus_msg_path(const rdbus_msg *m);
const char *rdbus_msg_interface(const rdbus_msg *m);
const char *rdbus_msg_member(const rdbus_msg *m);
const char *rdbus_msg_error_name(const rdbus_msg *m);
const char *rdbus_msg_sender(const rdbus_msg *m);
const char *rdbus_msg_destination(const rdbus_msg *m);
/* The body's signature; "" for an empty body. */
const char *rdbus_msg_signature(const rdbus_msg *m);

int rdbus_msg_is_call(const rdbus_msg *m, const char *iface, const char *member);
int rdbus_msg_is_signal(const rdbus_msg *m, const char *iface, const char *member);
int rdbus_msg_has_member(const rdbus_msg *m, const char *member);
int rdbus_msg_has_signature(const rdbus_msg *m, const char *sig);

/* Reads the leading arguments: pairs of a type and a pointer to where it goes,
   ended by RDBUS_TYPE_INVALID. Strings, object paths and signatures point into
   the message. RDBUS_TYPE_ARRAY takes the element type (a fixed one), a
   pointer to a const pointer to the elements and an int count. Answers 0, with
   err set when given, if the body does not start with those types. */
int rdbus_msg_get_args(rdbus_msg *m, rdbus_error *err, int type, ...);
/* Appends basic arguments, given as for rdbus_iter_append_basic, ended by
   RDBUS_TYPE_INVALID. */
int rdbus_msg_append_args(rdbus_msg *m, int type, ...);

/* Building: value points at the value, as uint8_t, rdbus_bool, int16_t ...
   double, or const char * for a string, object path or signature. */
void rdbus_iter_init_append(rdbus_msg *m, rdbus_iter *it);
int  rdbus_iter_append_basic(rdbus_iter *it, int type, const void *value);
/* An array's sig is its element's signature and a variant's its content's;
   a struct or dict entry takes NULL. */
int  rdbus_iter_open(rdbus_iter *it, int type, const char *sig, rdbus_iter *sub);
int  rdbus_iter_close(rdbus_iter *it, rdbus_iter *sub);
int  rdbus_iter_append_fixed_array(rdbus_iter *it, int type, const void *elems, int n);

/* Reading: 0 for an empty body. */
int  rdbus_iter_init(rdbus_msg *m, rdbus_iter *it);
/* The type at the iterator, RDBUS_TYPE_INVALID past the end. */
int  rdbus_iter_type(const rdbus_iter *it);
void rdbus_iter_get_basic(const rdbus_iter *it, void *value);
void rdbus_iter_recurse(const rdbus_iter *it, rdbus_iter *sub);
int  rdbus_iter_next(rdbus_iter *it);

/* The wire format alone, for tests: a message from its bytes (either byte
   order), or NULL if they are not one valid message. */
rdbus_msg *rdbus_msg_parse(const void *bytes, size_t len);
/* The message's bytes as sent, with its serial; NULL if it failed to build. */
const uint8_t *rdbus_msg_bytes(rdbus_msg *m, uint32_t serial, size_t *len);

#endif
