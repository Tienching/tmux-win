#ifndef WIN32_KEY_H
#define WIN32_KEY_H

/* At most two UTF-16 units, each with a key-down and key-up record. */
#define WIN32_KEY_SEQUENCE_MAX 128

int win32_key_encode(key_code, char *, size_t);

#endif
