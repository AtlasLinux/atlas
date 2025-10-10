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

int main(int argc, char **argv) {
    const char *params;
    int fd, use_finit;
    size_t image_size;
    struct stat st;
    void *image;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s [module] <use_finit=0>", argv[0]);
        return EXIT_FAILURE;
    }
    params = (argc >= 3) ? argv[2] : "";
    use_finit = (argc >= 4) ? (argv[3][0] != '0') : 0;

    /* Build module filename we’re looking for: argv[1].ko if not already ends with .ko */
    char modname[NAME_MAX];
    if (strlen(argv[1]) > sizeof(modname) - 4) {
        fprintf(stderr, "Module name too long\n");
        return EXIT_FAILURE;
    }
    if (strstr(argv[1], ".ko") == NULL)
        snprintf(modname, sizeof(modname), "%s.ko", argv[1]);
    else
        snprintf(modname, sizeof(modname), "%s", argv[1]);

    wanted_name = modname;
    found_path[0] = '\0';

    /* Walk /core/lib/modules/6.16.0-atlas+ */
    if (nftw("/core/lib/modules/6.16.0-atlas+", find_module_cb, 16, FTW_PHYS) == -1 && found_path[0] == '\0') {
        perror("nftw");
        return EXIT_FAILURE;
    }
    if (found_path[0] == '\0') {
        fprintf(stderr, "Module %s not found under /core/lib/modules/6.16.0-atlas+\n\r", modname);
        return EXIT_FAILURE;
    }

    fd = open(found_path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    if (use_finit) {
        if (finit_module(fd, params, 0) != 0) {
            perror("finit_module");
            close(fd);
            return EXIT_FAILURE;
        }
        close(fd);
    } else {
        if (fstat(fd, &st) != 0) {
            perror("fstat");
            close(fd);
            return EXIT_FAILURE;
        }
        image_size = st.st_size;
        image = malloc(image_size);
        if (!image) {
            perror("malloc");
            close(fd);
            return EXIT_FAILURE;
        }
        if (read(fd, image, image_size) != (ssize_t)image_size) {
            perror("read");
            free(image);
            close(fd);
            return EXIT_FAILURE;
        }
        close(fd);
        if (init_module(image, image_size, params) != 0) {
            perror("init_module");
            free(image);
            return EXIT_FAILURE;
        }
        free(image);
    }
    return EXIT_SUCCESS;
}
