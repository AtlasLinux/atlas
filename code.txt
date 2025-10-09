#define _GNU_SOURCE
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/route.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <stdarg.h>
#include <linux/fb.h>
#include <sys/mman.h>

#include "log.h"
#include "insmod.c"
#include "acl.h"

void load_modules(const char* modules[]);

/* safe mkdir -p wrapper (unchanged) */
static int ensure_dir(const char *path, mode_t mode) {
    log_debug("enter ensure_dir(path='%s', mode=%o)\n", path, mode);
    struct stat st;
    if (stat(path, &st) == 0) {
        log_warn("ensure_dir: %s already exists\n", path);
        log_debug("exit ensure_dir => 0\n");
        return 0;
    }

    log_info("ensure_dir: %s does not exist, attempting mkdir\n", path);
    if (mkdir(path, mode) == 0) {
        log_info("ensure_dir: created %s\n", path);
        log_debug("exit ensure_dir => 0\n");
        return 0;
    }

    if (errno == ENOENT) {
        log_warn("ensure_dir: parent missing for %s, creating parents\n", path);

        char tmp[256];
        strncpy(tmp, path, sizeof(tmp)-1);
        tmp[sizeof(tmp)-1] = '\0';

        for (char *p = tmp + 1; *p; ++p) {
            if (*p == '/') {
                *p = '\0';
                log_debug("ensure_dir: mkdir parent %s\n", tmp);
                if (mkdir(tmp, 0755) == 0) {
                    log_info("ensure_dir: created parent %s\n", tmp);
                } else if (errno != EEXIST) {
                    log_warn("ensure_dir: failed to create parent %s: %s\n", tmp, strerror(errno));
                } else {
                    log_debug("ensure_dir: parent %s already exists\n", tmp);
                }
                *p = '/';
            }
        }

        if (mkdir(path, mode) == 0) {
            log_info("ensure_dir: successfully created %s after creating parents\n", path);
            log_debug("exit ensure_dir => 0\n");
            return 0;
        } else {
            log_error("ensure_dir: failed to create %s even after parents: %s\n", path, strerror(errno));
        }
    } else {
        log_warn("ensure_dir: mkdir %s failed: %s\n", path, strerror(errno));
    }

    log_debug("exit ensure_dir => -1\n");
    return -1;
}

/* small helpers for reading config (wrappers around your libacl API) */
static int cfg_get_int(AclBlock *cfg, const char *path, int def) {
    if (!cfg) return def;
    long v = 0;
    if (acl_get_int(cfg, path, &v)) return (int)v;
    return def;
}

static char *cfg_get_string_dup(AclBlock *cfg, const char *path, const char *def) {
    if (!cfg) return def ? strdup(def) : NULL;
    char *tmp = NULL;
    if (acl_get_string(cfg, path, &tmp) && tmp) return strdup(tmp);
    return def ? strdup(def) : NULL;
}

/* spawn login (unchanged except small log phrasing) */
static void spawn_login(const char *tty) {
    log_info("spawn_login: starting login on %s\n", tty);
    pid_t pid;
    for (;;) {
        pid = fork();
        if (pid < 0) { log_error("fork failed: %s\n", strerror(errno)); sleep(1); continue; }
        if (pid == 0) {
            log_debug("child process %d starting login on %s\n", getpid(), tty);
            setsid();
            int fd = open(tty, O_RDWR);
            if (fd < 0) { perror("open tty"); _exit(1); }
            ioctl(fd, TIOCSCTTY, 0);
            dup2(fd, STDIN_FILENO); dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO) close(fd);
            char *envp[] = {"PATH=/bin:/sbin","LD_LIBRARY_PATH=/lib",NULL};
            char *argv[] = {"/sbin/login", NULL};
            execve(argv[0], argv, envp);
            perror("execve"); _exit(1);
        }
        log_debug("parent: spawned child %d for %s\n", pid, tty);
        int status; waitpid(pid, &status, 0);
        log_debug("child %d exited with status %d\n", pid, status);
        sleep(1);
    }
}

/* setup /dev and devices (unchanged) */
static void setup_dev(void) {
    log_debug("enter setup_dev()\n");
    ensure_dir("/dev", 0755);

    if (mount("devtmpfs","/dev","devtmpfs",MS_NOSUID|MS_NOEXEC|MS_RELATIME,NULL) == 0)
        log_info("mounted devtmpfs on /dev\n");
    else {
        log_warn("devtmpfs mount failed: %s, fallback tmpfs\n", strerror(errno));
        if (mount("tmpfs","/dev","tmpfs",MS_NOSUID|MS_STRICTATIME,"mode=0755")<0)
            log_error("tmpfs mount failed: %s\n", strerror(errno));
        else log_info("mounted tmpfs on /dev\n");

        struct { const char *path; mode_t mode; dev_t dev; } nodes[] = {
            {"/dev/console",0600,makedev(5,1)},
            {"/dev/null",0666,makedev(1,3)},
            {"/dev/zero",0666,makedev(1,5)},
            {"/dev/full",0666,makedev(1,7)},
            {"/dev/random",0666,makedev(1,8)},
            {"/dev/urandom",0666,makedev(1,9)},
            {"/dev/tty",0666,makedev(5,0)},
            {"/dev/ptmx",0666,makedev(5,2)},
            {"/dev/tty0",0600,makedev(4,0)},
            {"/dev/tty1",0620,makedev(4,1)},
            {"/dev/tty2",0620,makedev(4,2)},
            {"/dev/tty3",0620,makedev(4,3)}
        };
        for(size_t i=0;i<sizeof(nodes)/sizeof(nodes[0]);i++){
            struct stat st; if(stat(nodes[i].path,&st)==0) continue;
            if(mknod(nodes[i].path,S_IFCHR|nodes[i].mode,nodes[i].dev)<0)
                log_warn("mknod %s failed: %s\n",nodes[i].path,strerror(errno));
            else { chmod(nodes[i].path,nodes[i].mode); log_debug("created node %s\n",nodes[i].path);}
        }
    }

    ensure_dir("/dev/pts",0755);
    if(mount("devpts","/dev/pts","devpts",0,"mode=0620,ptmxmode=0666")==0)
        log_info("mounted devpts\n");
    else log_warn("devpts mount failed: %s\n", strerror(errno));

    struct stat st; if(stat("/dev/ptmx",&st)!=0){ mknod("/dev/ptmx",S_IFCHR|0666,makedev(5,2)); log_debug("created /dev/ptmx\n");}

    ensure_dir("/dev/shm",01777);
    if(mount("tmpfs","/dev/shm","tmpfs",MS_NOSUID|MS_NODEV,"size=64M,mode=1777")==0)
        log_info("mounted /dev/shm\n");
    else log_warn("/dev/shm mount failed: %s\n", strerror(errno));

    unlink("/dev/fd"); symlink("/proc/self/fd","/dev/fd");
    unlink("/dev/stdin"); unlink("/dev/stdout"); unlink("/dev/stderr");
    symlink("/proc/self/fd/0","/dev/stdin");
    symlink("/proc/self/fd/1","/dev/stdout");
    symlink("/proc/self/fd/2","/dev/stderr");

    log_info("/dev setup complete\n");
    log_debug("exit setup_dev()\n");
}

/* framebuffer setup unchanged */
static int setup_framebuffer(void) {
    log_debug("enter setup_framebuffer()\n");

    int fb = open("/dev/fb0", O_RDWR);
    if (fb < 0) {
        log_warn("cannot open /dev/fb0: %s\n", strerror(errno));
        return -1;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    if (ioctl(fb, FBIOGET_FSCREENINFO, &finfo) < 0) {
        log_warn("FBIOGET_FSCREENINFO failed: %s\n", strerror(errno));
        close(fb);
        return -1;
    }

    if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        log_warn("FBIOGET_VSCREENINFO failed: %s\n", strerror(errno));
        close(fb);
        return -1;
    }

    log_info("Framebuffer: %dx%d, %dbpp, line_length=%d\n",
        vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, finfo.line_length);

    size_t screensize = finfo.smem_len;
    void *fbp = mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (fbp == MAP_FAILED) {
        log_warn("mmap framebuffer failed: %s\n", strerror(errno));
        close(fb);
        return -1;
    }

    // clear the screen
    memset(fbp, 0, screensize);
    log_info("framebuffer cleared\n");

    // re-map if we need to draw later
    munmap(fbp, screensize);

    log_debug("exit setup_framebuffer()\n");
    return 0;
}

/* helper: check if file is executable */
static int is_executable(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    if (st.st_mode & S_IXUSR) return 1;  // owner executable
    return 0;
}

/* load modules from config: Modules.load[0], Modules.load[1], ... */
static void load_modules_from_config(AclBlock *cfg) {
    const int MAX_MODULES = 256;
    char path[256];
    int found = 0;
    for (int i = 0; i < MAX_MODULES; ++i) {
        snprintf(path, sizeof(path), "Modules.load[%d]", i);
        char *mod = NULL;
        if (!acl_get_string(cfg, path, &mod) || !mod) break;
        log_info("config: loading module '%s'\n", mod);
        insmod(mod);
        found = 1;
    }

    if (!found) {
        /* fallback to previous hard-coded list */
        const char *defaults[] = { "e1000", "virtio_dma_buf", "virtio-gpu", NULL };
        log_info("config: no Modules.load[], falling back to defaults\n");
        load_modules(defaults);
    }
}

/* launch services directory (configurable) */
static void launch_services_from_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) {
        log_warn("launch_services: opendir(%s): %s\n", dir, strerror(errno));
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char path[PATH_MAX];
        int n = snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if (n < 0 || n >= (int)sizeof(path)) { log_warn("path too long: %s/%s\n", dir, entry->d_name); continue; }
        struct stat st;
        if (stat(path, &st) < 0) { log_warn("stat(%s): %s\n", path, strerror(errno)); continue; }
        if (!S_ISREG(st.st_mode) || !(st.st_mode & S_IXUSR)) continue;
        pid_t pid = fork();
        if (pid < 0) { log_warn("fork failed for %s: %s\n", path, strerror(errno)); continue; }
        if (pid == 0) { execl(path, path, NULL); perror("execl"); _exit(1); }
        /* parent continues; no supervision here (simple) */
    }
    closedir(d);
}

/* Load modules helper for fallback usage (calls insmod for each name) */
void load_modules(const char* modules[]) {
    for (int i = 0; modules[i] != NULL; i++) {
        insmod(modules[i]);
    }
}

/* main init (now config-aware) */
int main(void) {
    /* create logger and console sink first */
    logger = logger_create(LOG_INFO);
    LogSink* console_sink = console_sink_create();
    logger_add_sink(logger, console_sink);
    log_debug("enter main()\n");

    /* try to parse /conf/system.acl (non-fatal) */
    AclBlock *cfg = acl_parse_file("/conf/system.acl");
    if (cfg) {
        if (!acl_resolve_all(cfg)) {
            log_warn("acl: /conf/system.acl parsed but failed to resolve references\n");
        } else {
            log_info("acl: /conf/system.acl loaded\n");
        }
    } else {
        log_info("acl: /conf/system.acl not found or failed to parse, continuing with defaults\n");
    }

    /* Logging path override (optional) */
    char *logpath = cfg_get_string_dup(cfg, "Logging.path", "/log/init.log");
    LogSink* file_sink = file_sink_create(logpath);
    if (file_sink) logger_add_sink(logger, file_sink);
    free(logpath);

    signal(SIGCHLD,SIG_IGN);
    signal(SIGHUP,SIG_IGN);
    log_debug("signals set\n");

    ensure_dir("/proc",0555); ensure_dir("/sys",0555);
    ensure_dir("/dev",0755); ensure_dir("/etc",0755); ensure_dir("/var",0755);

    if(mount("proc","/proc","proc",0,NULL)<0) log_warn("/proc mount failed: %s\n", strerror(errno));
    else log_info("mounted /proc\n");

    if(mount("sysfs","/sys","sysfs",0,NULL)<0) log_warn("/sys mount failed: %s\n", strerror(errno));
    else log_info("mounted /sys\n");

    setup_dev();
    setup_framebuffer();

    ensure_dir("/tmp",01777); chmod("/tmp",01777);
    ensure_dir("/var/tmp",01777); chmod("/var/tmp",01777);
    if(mount("tmpfs","/tmp","tmpfs",MS_NOSUID|MS_NODEV,"size=128M,mode=1777")<0)
        log_warn("/tmp mount failed: %s\n", strerror(errno));
    else log_info("mounted /tmp\n");

    ensure_dir("/var/run",0755);
    if(mount("tmpfs","/var/run","tmpfs",MS_NOSUID|MS_NODEV,"size=16M,mode=0755")<0)
        log_warn("/var/run mount failed: %s\n", strerror(errno));
    else log_info("mounted /var/run\n");

    ensure_dir("/var/cache",0755);

    symlink("/usr/bin","/bin");
    symlink("/usr/lib","/lib");
    symlink("/usr/lib64","/lib64");
    symlink("/var/run", "/run");

    log_info("AtlasLinux init starting...\n");

    /* load modules from config or defaults */
    if (cfg) load_modules_from_config(cfg);
    else load_modules((const char*[]){ "e1000", "virtio_dma_buf", "virtio-gpu", NULL });

    /* launch services: directory may be overridden in config */
    char *svcdir = cfg_get_string_dup(cfg, "Services.dir", "/sbin/services");
    launch_services_from_dir(svcdir);
    free(svcdir);

    /* get how many ttys to spawn from config (default 3) */
    int ttys = cfg_get_int(cfg, "System.spawn_getty_ttys", 3);

    /* spawn login processes on tty1..ttyN */
    for (int i = 1; i <= ttys && i <= 12; ++i) {
        pid_t p = fork();
        if (p == 0) {
            char ttypath[32];
            snprintf(ttypath, sizeof(ttypath), "/dev/tty%d", i);
            spawn_login(ttypath);
            _exit(0);
        }
    }

    log_debug("main: parent entering infinite pause loop\n");
    for(;;) pause();
}
