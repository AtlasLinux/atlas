#ifndef ACL_H
#define ACL_H

#include <stdio.h>

/* Opaque types (mirror internal structures) */
typedef struct AclValue AclValue;
typedef struct AclField AclField;
typedef struct AclBlock AclBlock;
typedef struct AclError AclError;

/* Lifecycle (no-op for now) */
int acl_init(void);
void acl_shutdown(void);

/* Parse from file or in-memory string.
   Returns a heap-allocated AclBlock* (linked list of top-level blocks) on success,
   or NULL on failure (in which case an error may have been printed to stderr). */
AclBlock *acl_parse_file(const char *path);
AclBlock *acl_parse_string(const char *text);

/* Resolve references in-place. Returns 1 on success, 0 on failure. */
int acl_resolve_all(AclBlock *root);

/* Utilities */
void acl_print(AclBlock *root, FILE *out);

/* Free tree returned by parser */
void acl_free(AclBlock *root);

/* Error structure and helpers (placeholder; parser currently prints to stderr) */
struct AclError {
    int code;
    char *message;
    int line;
    int col;
    size_t pos;
};
void acl_error_free(AclError *err);

/* Path lookup that supports numeric array indexing.
   Examples:
     "Modules.load[0]"
     "Network.interface[\"eth0\"].gateway"
     "Network.interface[\"wlan0\"].addresses[2]"

   Returns pointer-owned Value* (pointer into the tree) or NULL if not found.
   Do not free the returned pointer; its lifetime is tied to the acl tree.
*/
AclValue *acl_find_value_by_path(AclBlock *root, const char *path);

/* Typed getters now use array-index aware lookup (same behavior as before) */
int acl_get_int(AclBlock *root, const char *path, long *out);
int acl_get_float(AclBlock *root, const char *path, double *out);
int acl_get_bool(AclBlock *root, const char *path, int *out);
int acl_get_string(AclBlock *root, const char *path, char **out);

#endif
