#ifndef WIN32_KEY_H
#define WIN32_KEY_H

/* At most two UTF-16 units, each with a key-down and key-up record. */
#define WIN32_KEY_SEQUENCE_MAX 128

int win32_key_encode(key_code, char *, size_t);
int win32_key_encode_record(const KEY_EVENT_RECORD *, char *, size_t);
int win32_key_decode(const char *, size_t, KEY_EVENT_RECORD *, size_t *);
key_code win32_key_code(const KEY_EVENT_RECORD *);

#endif
