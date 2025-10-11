#include "acl.h"

int main(int argc, char** argv) {
    AclBlock* root = acl_parse_file(argv[1]);
    acl_print(root, stdout);
    if (argc == 3) {
        char* buf;
        acl_get_string(root, argv[2], &buf);
        puts(buf);
    }
    return 0;
}