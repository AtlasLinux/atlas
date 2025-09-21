#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include "log.h"

#define delete_module(name, flags) syscall(__NR_delete_module, name, flags)

int main(int argc, char **argv) {
    log_init("/dev/null", LOG_INFO);
    if (argc != 2) {
        log_error("Usage: %s [module]", argv[0]);
        return EXIT_FAILURE;
    }
    if (delete_module(argv[1], O_NONBLOCK) != 0) {
        log_perror("delete_module");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}