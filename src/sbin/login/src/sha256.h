#ifndef SHA256_H
#define SHA256_H
#include <stddef.h>
#include <stdint.h>

/* compute SHA-256: digest must be 32 bytes */
void sha256(const void *data, size_t len, uint8_t digest[32]);

/* helper: hex encode 32-byte digest to 64-char string + NUL */
void sha256_to_hex(const uint8_t digest[32], char out_hex[65]);

/* helper: hex string -> bytes (returns bytes written or -1 on error) */
int hex_to_bin(const char *hex, uint8_t *out, size_t outlen);

/* constant-time memcmp (returns 1 if equal, 0 if not) */
int ct_memcmp(const void *a, const void *b, size_t len);

#endif
