/* src/login.c
 * Minimal login that uses your libacl API (acl.h)
 *
 * Behavior:
 *  - reads /conf/users.conf via acl_parse_file()
 *  - looks up Users.user["<name>"].passwd_hash, uid, gid, home, shell
 *  - accepts plaintext or crypt-style hash ($id$...)
 *  - on success: initgroups(), setgid(), setuid(), chdir(home), exec shell
 *
 * Build: see commands below.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "acl.h"
#include "sha256.h"

/* Read password from tty with no echo; returns 0 on success. */
static int get_password_noecho(char *buf, size_t n) {
    struct termios oldt, newt;
    if (tcgetattr(STDIN_FILENO, &oldt) != 0) return -1;
    newt = oldt;
    newt.c_lflag &= ~(ECHO);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &newt) != 0) return -1;
    if (!fgets(buf, n, stdin)) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt); return -1; }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
    size_t L = strlen(buf);
    if (L && buf[L-1] == '\n') buf[L-1] = '\0';
    return 0;
}

static int verify_password(const char *entered, const char *stored_hash) {
    if (!stored_hash || stored_hash[0] == '\0') return 0;

    /* sha256$<salt-hex>$<digest-hex> */
    if (strncmp(stored_hash, "sha256$", 7) == 0) {
        const char *p = stored_hash + 7;
        const char *d = strchr(p, '$');
        if (!d) return 0;
        size_t salt_hex_len = (size_t)(d - p);
        const char *hex = d + 1;
        if (strlen(hex) != 64) return 0; /* must be 32-byte digest hex */

        /* copy salt hex into a temporary NUL-terminated buffer */
        if (salt_hex_len == 0 || salt_hex_len > 128) return 0; /* defensive */
        char salthex_buf[129];
        if (salt_hex_len >= sizeof(salthex_buf)) return 0;
        memcpy(salthex_buf, p, salt_hex_len);
        salthex_buf[salt_hex_len] = '\0';

        uint8_t saltbin[64];
        int rc = hex_to_bin(salthex_buf, saltbin, sizeof(saltbin));
        if (rc <= 0) return 0;
        size_t saltlen = (size_t)rc;

        /* compute sha256(salt || password) */
        size_t plen = strlen(entered);
        uint8_t *concat = malloc(saltlen + plen);
        if (!concat) return 0;
        memcpy(concat, saltbin, saltlen);
        memcpy(concat + saltlen, entered, plen);
        uint8_t digest[32];
        sha256(concat, saltlen + plen, digest);
        free(concat);

        char digest_hex[65];
        sha256_to_hex(digest, digest_hex);

        /* constant-time compare of hex strings (64 bytes) */
        return ct_memcmp(digest_hex, hex, 64);
    }

    /* fallback: crypt-style or plaintext */
    if (stored_hash[0] == '$') {
        return 0; /* crypt not available in this build; keep safe */
    }

    return strcmp(entered, stored_hash) == 0;
}

/* build a path like Users.user["name"].field */
static void build_user_path(char *buf, size_t n, const char *user, const char *field) {
    snprintf(buf, n, "Users.user[\"%s\"].%s", user, field);
}

int main(int argc, char **argv) {
    const char *conf = "/conf/users.conf"; /* path inside the running system */
    char *err = NULL;
    printf("\x1b[2J\x1b[H"); // clear screen

    AclBlock *root = acl_parse_file(conf);
    if (!root) {
        fprintf(stderr, "login: failed to parse %s\n", conf);
        return 1;
    }
    if (!acl_resolve_all(root)) {
        fprintf(stderr, "login: failed to resolve config references\n");
        acl_free(root);
        return 1;
    }

    /* prompt for username */
    char username[128];
    printf("login: ");
    if (!fgets(username, sizeof(username), stdin)) { acl_free(root); return 1; }
    size_t ln = strlen(username); if (ln && username[ln-1]=='\n') username[ln-1]=0;
    if (username[0] == '\0') { acl_free(root); return 1; }

    /* lookup passwd_hash */
    char path[256];
    build_user_path(path, sizeof(path), username, "passwd_hash");
    char *stored_hash = NULL;
    if (!acl_get_string(root, path, &stored_hash) || !stored_hash) {
        fprintf(stderr, "login: user not found\n");
        acl_free(root);
        return 1;
    }

    /* prompt password */
    printf("Password: ");
    char pass[256];
    if (get_password_noecho(pass, sizeof(pass)) != 0) {
        fprintf(stderr, "login: failed to read password\n");
        acl_free(root);
        return 1;
    }
    printf("\n");

    if (!verify_password(pass, stored_hash)) {
        fprintf(stderr, "login: authentication failed\n");
        acl_free(root);
        return 1;
    }

    /* read uid/gid/home/shell */
    long uid = -1, gid = -1;
    build_user_path(path, sizeof(path), username, "uid");
    acl_get_int(root, path, (long *)&uid); /* header uses long* but example uses int; adapt if necessary */
    build_user_path(path, sizeof(path), username, "gid");
    acl_get_int(root, path, (long *)&gid);
    build_user_path(path, sizeof(path), username, "home");
    char *home = NULL; acl_get_string(root, path, &home);
    if (!home) home = strdup("/");

    build_user_path(path, sizeof(path), username, "shell");
    char *shell = NULL; acl_get_string(root, path, &shell);
    if (!shell) shell = strdup("/bin/sh");

    /* set up groups/ids and drop privileges */
    if (initgroups(username, (gid_t)gid) < 0) {
        perror("initgroups");
    }
    if (setgid((gid_t)gid) < 0) perror("setgid");
    if (setuid((uid_t)uid) < 0) perror("setuid");

    if (chdir(home) < 0 || setenv("HOME", home, 1)) {
        /* not fatal; just warn */
        fprintf(stderr, "login: chdir(%s): %s\n", home, strerror(errno));
    }

    /* exec the user's shell */
    char *const args[] = { shell, NULL };
    execv(shell, args);
    /* exec failed */
    fprintf(stderr, "login: execv(%s): %s\n", shell, strerror(errno));
    acl_free(root);
    return 1;
}
