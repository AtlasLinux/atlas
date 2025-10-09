/* mkpasswd-sha256.c
   Usage: mkpasswd-sha256 <password>
   Writes: sha256$<salt-hex>$<digest-hex> to stdout
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include "sha256.h"

static void to_hex(const uint8_t *bin, size_t n, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[i*2]   = hex[(bin[i] >> 4) & 0xF];
        out[i*2+1] = hex[bin[i] & 0xF];
    }
    out[n*2] = 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <password>\n", argv[0]);
        return 2;
    }
    const char *pw = argv[1];

    uint8_t salt[16];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) { perror("open /dev/urandom"); return 3; }
    if (read(fd, salt, sizeof(salt)) != (ssize_t)sizeof(salt)) { perror("read salt"); close(fd); return 4; }
    close(fd);

    uint8_t *concat = malloc(sizeof(salt) + strlen(pw));
    memcpy(concat, salt, sizeof(salt));
    memcpy(concat + sizeof(salt), pw, strlen(pw));
    uint8_t digest[32];
    sha256(concat, sizeof(salt) + strlen(pw), digest);
    free(concat);

    char salthex[sizeof(salt)*2 + 1], dighex[65];
    to_hex(salt, sizeof(salt), salthex);
    sha256_to_hex(digest, dighex);

    printf("sha256$%s$%s\n", salthex, dighex);
    return 0;
}
