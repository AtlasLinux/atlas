#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <net/route.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include "log.h"

/* bring up loopback */
static void configure_lo(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { log_error("socket failed: %s\n\r", strerror(errno)); exit(1); }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "lo", IFNAMSIZ-1);

    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    ioctl(fd, SIOCSIFFLAGS, &ifr);

    struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &addr->sin_addr);
    ioctl(fd, SIOCSIFADDR, &ifr);

    close(fd);
    log_info("loopback configured\n\r");
}

/* pick first non-loopback iface */
static int choose_net_iface(char *buf, size_t bufsz) {
    DIR *d = opendir("/sys/class/net");
    if (!d) return -1;

    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (strcmp(e->d_name,"lo")==0) continue;
        strncpy(buf, e->d_name, bufsz-1);
        buf[bufsz-1] = '\0';
        closedir(d);
        return 0;
    }

    closedir(d);
    return -1;
}

/* assign IP to interface */
static int set_ip_on_iface(const char *ifname, const char *ip) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

    // fetch current flags
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        perror("SIOCGIFFLAGS");
        close(fd);
        return -1;
    }

    // bring interface up
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) {
        perror("SIOCSIFFLAGS");
        close(fd);
        return -1;
    }

    // assign IP
    struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
    addr->sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &addr->sin_addr) != 1) {
        fprintf(stderr, "invalid IP: %s\n\r", ip);
        close(fd);
        return -1;
    }

    if (ioctl(fd, SIOCSIFADDR, &ifr) < 0) {
        perror("SIOCSIFADDR");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/* add default route */
static void add_default_route(const char *gw, const char *dev) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) exit(1);

    struct rtentry route;
    memset(&route, 0, sizeof(route));
    struct sockaddr_in *addr;

    addr = (struct sockaddr_in *)&route.rt_dst;
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;

    addr = (struct sockaddr_in *)&route.rt_gateway;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, gw, &addr->sin_addr);

    addr = (struct sockaddr_in *)&route.rt_genmask;
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;

    route.rt_flags = RTF_UP | RTF_GATEWAY;
    route.rt_dev = (char *)dev;

    ioctl(fd, SIOCADDRT, &route);
    close(fd);
}

int main(void) {
    log_init("/log/services/net.log", 0);
    log_info("net service starting...\n\r");

    configure_lo();

    char ifname[IFNAMSIZ] = {0};
    int waited = 0, max_wait = 10;
    while(waited < max_wait){
        if(choose_net_iface(ifname,sizeof(ifname))==0) break;
        sleep(1); waited++;
    }

    if(ifname[0]){
        set_ip_on_iface(ifname,"10.0.2.15");
        add_default_route("10.0.2.2",ifname);
    }

    int rfd = open("/etc/resolv.conf",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(rfd>=0){
        const char *dns="nameserver 8.8.8.8\n\r";
        write(rfd,dns,strlen(dns));
        close(rfd);
    }

    log_info("net service done\n\r");
    return 0;
}
