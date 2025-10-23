#define _GNU_SOURCE
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include "log.h"

#define init_module(module_image, len, param_values) syscall(__NR_init_module, module_image, len, param_values)
#define finit_module(fd, param_values, flags) syscall(__NR_finit_module, fd, param_values, flags)

/* global for the nftw callback */
static char found_path[PATH_MAX];
static const char *wanted_name;

static int find_module_cb(const char *fpath, const struct stat *sb,
                          int typeflag, struct FTW *ftwbuf) {
    (void)sb;
    (void)ftwbuf;
    if (typeflag != FTW_F)
        return 0;

    /* Compare basename */
    const char *base = strrchr(fpath, '/');
    base = base ? base + 1 : fpath;
    if (strcmp(base, wanted_name) == 0) {
        strncpy(found_path, fpath, sizeof(found_path) - 1);
        found_path[sizeof(found_path) - 1] = '\0';
        return 1; /* stop nftw */
    }
    return 0;
}

int insmod(char* module) {
    int fd;
    size_t image_size;
    struct stat st;
    void *image;

    /* Build module filename we’re looking for: module.ko if not already ends with .ko */
    char modname[NAME_MAX];
    if (strlen(module) > sizeof(modname) - 4) {
        log_error("Module name too long\n");
        return EXIT_FAILURE;
    }
    if (strstr(module, ".ko") == NULL)
        snprintf(modname, sizeof(modname), "%s.ko", module);
    else
        snprintf(modname, sizeof(modname), "%s", module);

    wanted_name = modname;
    found_path[0] = '\0';

    /* Walk /core/lib/modules/6.18.0-rc2-atlas+ */
    if (nftw("/core/lib/modules/6.18.0-rc2-atlas+", find_module_cb, 16, FTW_PHYS) == -1 && found_path[0] == '\0') {
        log_perror("nftw");
        return EXIT_FAILURE;
    }
    if (found_path[0] == '\0') {
        log_error("Module %s not found under /core/lib/modules/6.18.0-rc2-atlas+\n", modname);
        return EXIT_FAILURE;
    }
    log_debug("Found module %s at %s\n", module, found_path);
    log_info("Loading module %s\n", module);

    fd = open(found_path, O_RDONLY);
    if (fd < 0) {
        log_perror("open");
        return EXIT_FAILURE;
    }
    if (fstat(fd, &st) != 0) {
        log_perror("fstat");
        close(fd);
        return EXIT_FAILURE;
    }
    image_size = st.st_size;
    image = malloc(image_size);
    if (!image) {
        log_perror("malloc");
        close(fd);
        return EXIT_FAILURE;
    }
    if (read(fd, image, image_size) != (ssize_t)image_size) {
        log_perror("read");
        free(image);
        close(fd);
        return EXIT_FAILURE;
    }
    close(fd);
    if (init_module(image, image_size, "") != 0) {
        log_perror("init_module");
        free(image);
        return EXIT_FAILURE;
    }
    free(image);
    return EXIT_SUCCESS;
}
