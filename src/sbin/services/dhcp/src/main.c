#define _GNU_SOURCE
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <net/route.h>
#include <sys/un.h>
#include <sys/types.h>
#include <poll.h>

#include "log.h"

#define CONTROL_SOCKET_PATH "/run/dhcpd.sock"

static int setup_control_socket(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        log_error("control socket(): %s\n\r", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONTROL_SOCKET_PATH, sizeof(addr.sun_path)-1);

    unlink(CONTROL_SOCKET_PATH); /* remove stale */

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_perror("bind");
        close(fd);
        return -1;
    }
    if (listen(fd, 5) < 0) {
        log_perror("listen");
        close(fd);
        return -1;
    }
    return fd;
}

/* bring up loopback */
static void configure_lo(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { log_error("socket failed: %s\n\r\r", strerror(errno)); exit(1); }

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
    log_info("loopback configured\n\r\r");
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

/* DHCP */
#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5

#define DHCP_OPTION_MSGTYPE    53
#define DHCP_OPTION_SERVERID   54
#define DHCP_OPTION_REQUESTED  50
#define DHCP_OPTION_NETMASK    1
#define DHCP_OPTION_ROUTER     3
#define DHCP_OPTION_DNS        6
#define DHCP_OPTION_END        255
#define DHCP_OPTION_PARAM_REQ  55

/* A minimal BOOTP/DHCP message (packed to avoid padding) */
struct dhcp_msg {
    uint8_t op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint8_t options[312];
} __attribute__((packed));

/* get MAC address of interface */
static int if_get_hwaddr(const char *ifname, uint8_t *mac_out) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) { close(fd); return -1; }
    memcpy(mac_out, ifr.ifr_hwaddr.sa_data, 6);
    close(fd);
    return 0;
}

/* bring interface up (no IP) */
static int bring_iface_up(const char *ifname) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) { close(fd); return -1; }
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) { close(fd); return -1; }

    close(fd);
    return 0;
}

/* helper: add option to buffer, return new pointer */
static uint8_t *opt_add(uint8_t *p, uint8_t code, uint8_t len, const void *data) {
    *p++ = code;
    *p++ = len;
    if (len && data) {
        memcpy(p, data, len);
        p += len;
    }
    return p;
}

/* build a DHCPDISCOVER message */
static void build_discover(struct dhcp_msg *m, uint32_t xid, const uint8_t *mac) {
    memset(m, 0, sizeof(*m));
    m->op = 1; /* BOOTREQUEST */
    m->htype = 1; /* Ethernet */
    m->hlen = 6;
    m->xid = htonl(xid);
    m->flags = htons(0x8000); /* broadcast */
    memcpy(m->chaddr, mac, 6);

    /* magic cookie */
    uint8_t *p = m->options;
    p[0] = 99; p[1] = 130; p[2] = 83; p[3] = 99;
    p += 4;

    /* dhcp message type */
    p = opt_add(p, DHCP_OPTION_MSGTYPE, 1, (uint8_t[]){DHCPDISCOVER});

    /* parameter request list (request netmask, router, dns) */
    uint8_t prl[] = { DHCP_OPTION_NETMASK, DHCP_OPTION_ROUTER, DHCP_OPTION_DNS };
    p = opt_add(p, DHCP_OPTION_PARAM_REQ, sizeof(prl), prl);

    /* end */
    *p++ = DHCP_OPTION_END;
}

/* build DHCPREQUEST message */
static void build_request(struct dhcp_msg *m, uint32_t xid, const uint8_t *mac,
                          uint32_t requested_ip, uint32_t server_id) {
    memset(m, 0, sizeof(*m));
    m->op = 1;
    m->htype = 1;
    m->hlen = 6;
    m->xid = htonl(xid);
    m->flags = htons(0x8000);
    memcpy(m->chaddr, mac, 6);

    uint8_t *p = m->options;
    p[0] = 99; p[1] = 130; p[2] = 83; p[3] = 99;
    p += 4;

    p = opt_add(p, DHCP_OPTION_MSGTYPE, 1, (uint8_t[]){DHCPREQUEST});
    p = opt_add(p, DHCP_OPTION_REQUESTED, 4, &requested_ip); /* network order expected */
    p = opt_add(p, DHCP_OPTION_SERVERID, 4, &server_id);
    uint8_t prl[] = { DHCP_OPTION_NETMASK, DHCP_OPTION_ROUTER, DHCP_OPTION_DNS };
    p = opt_add(p, DHCP_OPTION_PARAM_REQ, sizeof(prl), prl);
    *p++ = DHCP_OPTION_END;
}

/* parse DHCP options and extract useful values (server_id, routers, dns) */
static void parse_options(const uint8_t *opts, size_t optslen,
                          uint32_t *out_server_id,
                          uint32_t *out_netmask,
                          uint32_t *out_router,
                          uint32_t **out_dns, size_t *out_dns_cnt) {
    *out_server_id = 0;
    *out_netmask = 0;
    *out_router = 0;
    *out_dns = NULL;
    *out_dns_cnt = 0;

    /* options should start with cookie at opts[0..3] */
    if (optslen < 4) return;
    if (!(opts[0]==99 && opts[1]==130 && opts[2]==83 && opts[3]==99)) return;

    size_t i = 4;
    while (i < optslen) {
        uint8_t code = opts[i++];
        if (code == DHCP_OPTION_END) break;
        if (code == 0) continue; /* pad */
        if (i >= optslen) break;
        uint8_t len = opts[i++];
        if (i + len > optslen) break;

        switch (code) {
        case DHCP_OPTION_MSGTYPE:
            /* handled elsewhere */
            break;
        case DHCP_OPTION_SERVERID:
            if (len == 4) {
                uint32_t v;
                memcpy(&v, &opts[i], 4);
                *out_server_id = v;
            }
            break;
        case DHCP_OPTION_NETMASK:
            if (len == 4) {
                uint32_t v; memcpy(&v, &opts[i], 4); *out_netmask = v;
            }
            break;
        case DHCP_OPTION_ROUTER:
            if (len >= 4) {
                uint32_t v; memcpy(&v, &opts[i], 4); *out_router = v;
            }
            break;
        case DHCP_OPTION_DNS:
            if (len >= 4) {
                size_t cnt = len / 4;
                uint32_t *arr = malloc(cnt * sizeof(uint32_t));
                if (arr) {
                    for (size_t j = 0; j < cnt; ++j) {
                        memcpy(&arr[j], &opts[i + j*4], 4);
                    }
                    *out_dns = arr;
                    *out_dns_cnt = cnt;
                }
            }
            break;
        default:
            /* ignore */
            break;
        }

        i += len;
    }
}

/* utility: read DHCP message type from options (returns 0 if none) */
static int dhcp_msgtype_from_options(const uint8_t *opts, size_t optslen) {
    if (optslen < 4) return 0;
    if (!(opts[0]==99 && opts[1]==130 && opts[2]==83 && opts[3]==99)) return 0;
    size_t i = 4;
    while (i < optslen) {
        uint8_t code = opts[i++];
        if (code == DHCP_OPTION_END) break;
        if (code == 0) continue;
        if (i >= optslen) break;
        uint8_t len = opts[i++];
        if (i + len > optslen) break;
        if (code == DHCP_OPTION_MSGTYPE && len == 1) {
            return opts[i];
        }
        i += len;
    }
    return 0;
}

/* write /etc/resolv.conf with DNS servers (array in network order) */
static void write_resolv(uint32_t *dns_arr, size_t dns_cnt) {
    if (dns_arr == NULL || dns_cnt == 0) return;
    int fd = open("/etc/resolv.conf", O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) { log_warn("could not open /etc/resolv.conf: %s\n\r", strerror(errno)); return; }

    char line[64];
    for (size_t i = 0; i < dns_cnt; ++i) {
        struct in_addr a;
        a.s_addr = dns_arr[i];
        const char *s = inet_ntoa(a);
        if (!s) continue;
        int n = snprintf(line, sizeof(line), "nameserver %s\n\r", s);
        if (n > 0) write(fd, line, (size_t)n);
    }
    close(fd);
}

/* perform DHCP DISCOVER -> OFFER -> REQUEST -> ACK sequence on ifname */
static int do_dhcp(const char *ifname) {
    uint8_t mac[6];
    if (if_get_hwaddr(ifname, mac) < 0) {
        log_error("failed to get MAC for %s: %s\n\r", ifname, strerror(errno));
        return -1;
    }

    /* bring interface up (link) so we can send */
    if (bring_iface_up(ifname) < 0) {
        log_warn("could not bring %s up: %s\n\r", ifname, strerror(errno));
        /* continue anyway, some drivers bring it up later */
    }

    /* create socket bound to port 68 */
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        log_error("socket: %s\n\r", strerror(errno));
        return -1;
    }

    /* allow broadcast */
    int on = 1;
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) < 0)
        log_warn("setsockopt(SO_BROADCAST) failed: %s\n\r", strerror(errno));

    /* bind to port 68 */
    struct sockaddr_in bind_addr = {0};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(68);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        log_error("bind(68): %s\n\r", strerror(errno));
        close(s);
        return -1;
    }

    /* bind socket to interface so replies come on that interface */
    if (setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, ifname, strlen(ifname)) < 0) {
        /* not fatal on all kernels, just warn */
        log_warn("SO_BINDTODEVICE failed for %s: %s\n\r", ifname, strerror(errno));
    }

    /* destination (broadcast) */
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(67);
    dst.sin_addr.s_addr = INADDR_BROADCAST;

    /* transaction id */
    srand((unsigned)time(NULL) ^ getpid());
    uint32_t xid = ((uint32_t)rand() << 16) ^ rand();

    struct dhcp_msg msg;
    build_discover(&msg, xid, mac);

    fd_set rfds;
    struct timeval tv;
    int tries = 0;
    const int max_tries = 4;
    ssize_t sent;

    /* send discover and wait for offer(s) */
    uint32_t offered_ip = 0;
    uint32_t server_id = 0;
    while (tries < max_tries && offered_ip == 0) {
        tries++;
        sent = sendto(s, &msg, sizeof(msg), 0, (struct sockaddr*)&dst, sizeof(dst));
        if (sent < 0) {
            log_warn("sendto DISCOVER failed: %s\n\r", strerror(errno));
        } else {
            log_info("DHCPDISCOVER sent (try %d)\n\r", tries);
        }

        /* wait up to 3 seconds for an answer, then retry */
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        int rv = select(s+1, &rfds, NULL, NULL, &tv);
        if (rv < 0) {
            log_warn("select error: %s\n\r", strerror(errno));
            continue;
        } else if (rv == 0) {
            log_debug("no DHCP offer yet (timeout)\n\r");
            continue;
        } else {
            /* read packet(s), accept first matching xid OFFER */
            struct dhcp_msg reply;
            ssize_t n = recv(s, &reply, sizeof(reply), 0);
            if (n <= 0) {
                log_warn("recv failed: %s\n\r", strerror(errno));
                continue;
            }
            if (ntohl(reply.xid) != xid) {
                log_debug("ignoring reply with xid %u (want %u)\n\r", ntohl(reply.xid), xid);
                continue;
            }
            int msgtype = dhcp_msgtype_from_options(reply.options, sizeof(reply.options));
            if (msgtype != DHCPOFFER) {
                log_debug("received non-OFFER DHCP message type %d\n\r", msgtype);
                continue;
            }
            offered_ip = reply.yiaddr; /* network order */
            /* parse options to get server id */
            uint32_t netmask=0, router=0;
            uint32_t *dns_arr = NULL; size_t dns_cnt = 0;
            parse_options(reply.options, sizeof(reply.options), &server_id, &netmask, &router, &dns_arr, &dns_cnt);
            if (server_id == 0) {
                log_warn("OFFER missing server identifier; ignoring\n\r");
                offered_ip = 0; /* ignore */
                free(dns_arr);
                continue;
            }
            /* we have an offer */
            char ipstr[INET_ADDRSTRLEN];
            struct in_addr ina; ina.s_addr = offered_ip;
            inet_ntop(AF_INET, &ina, ipstr, sizeof(ipstr));
            log_info("DHCPOFFER from server %s offered %s\n\r", inet_ntoa(*(struct in_addr *)&server_id), ipstr);

            /* keep dns_arr/dns_cnt/router/netmask for later */
            if (dns_arr) {
                /* store them in heap-local variables by reusing parse vars below after request */
                /* we'll free/handle them after ACK */
            }
            /* break to send REQUEST */
            /* For simplicity we will reuse server_id variable and offered_ip */
            break;
        }
    }

    if (offered_ip == 0) {
        log_error("no DHCPOFFER received\n\r");
        close(s);
        return -1;
    }

    /* build REQUEST (use same xid for simplicity) */
    struct dhcp_msg req;
    build_request(&req, xid, mac, offered_ip, server_id);

    /* send REQUEST and wait for ACK (with retries) */
    int got_ack = 0;
    tries = 0;
    uint32_t netmask = 0, router = 0;
    uint32_t *dns_arr = NULL; size_t dns_cnt = 0;

    while (tries < max_tries && !got_ack) {
        tries++;
        if (sendto(s, &req, sizeof(req), 0, (struct sockaddr*)&dst, sizeof(dst)) < 0) {
            log_warn("sendto REQUEST failed: %s\n\r", strerror(errno));
        } else log_info("DHCPREQUEST sent (try %d)\n\r", tries);

        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        int rv = select(s+1, &rfds, NULL, NULL, &tv);
        if (rv <= 0) { log_debug("no ACK yet\n\r"); continue; }

        struct dhcp_msg reply;
        ssize_t n = recv(s, &reply, sizeof(reply), 0);
        if (n <= 0) { log_warn("recv failed: %s\n\r", strerror(errno)); continue; }
        if (ntohl(reply.xid) != xid) {
            log_debug("ignoring reply with xid %u\n\r", ntohl(reply.xid));
            continue;
        }
        int mtype = dhcp_msgtype_from_options(reply.options, sizeof(reply.options));
        if (mtype == DHCPACK) {
            got_ack = 1;
            /* parse out options */
            uint32_t server_id2 = 0;
            parse_options(reply.options, sizeof(reply.options), &server_id2, &netmask, &router, &dns_arr, &dns_cnt);
            if (reply.yiaddr == 0) {
                log_warn("ACK without yiaddr??\n\r");
                got_ack = 0;
                continue;
            }
            char ipstr[INET_ADDRSTRLEN], gwstr[INET_ADDRSTRLEN];
            struct in_addr ina; ina.s_addr = reply.yiaddr;
            inet_ntop(AF_INET, &ina, ipstr, sizeof(ipstr));
            log_info("DHCPACK: leased %s\n\r", ipstr);
            if (router) {
                struct in_addr r; r.s_addr = router;
                inet_ntop(AF_INET, &r, gwstr, sizeof(gwstr));
                log_info("DHCPACK: router %s\n\r", gwstr);
            } else {
                log_info("DHCPACK: no router option\n\r");
            }

            /* now apply IP and route */
            /* convert yiaddr to string (note reply.yiaddr is in network order) */
            char ipbuf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &reply.yiaddr, ipbuf, sizeof(ipbuf));
            if (set_ip_on_iface(ifname, ipbuf) == 0) {
                log_info("set_ip_on_iface %s -> %s\n\r", ifname, ipbuf);
            } else {
                log_error("set_ip_on_iface failed for %s -> %s\n\r", ifname, ipbuf);
            }

            if (router) {
                struct in_addr r; r.s_addr = router;
                char gwbuf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &r, gwbuf, sizeof(gwbuf));
                add_default_route(gwbuf, ifname);
            }

            if (dns_arr && dns_cnt) {
                write_resolv(dns_arr, dns_cnt);
            }

            free(dns_arr);
            break;
        } else {
            log_debug("received DHCP message type %d while awaiting ACK\n\r", mtype);
            continue;
        }
    }

    if (!got_ack) {
        log_error("didn't receive DHCPACK\n\r");
        close(s);
        return -1;
    }

    close(s);
    return 0;
}

int main(void) {
    log_init("/log/services/dhcp.log", 0);
    log_info("dhcp service starting...\n\r");

    configure_lo();

    char ifname[IFNAMSIZ] = {0};
    int waited = 0, max_wait = 10;
    while (waited < max_wait) {
        if (choose_net_iface(ifname, sizeof(ifname)) == 0) {
            log_info("chosen interface %s\n\r", ifname);
            break;
        }
        sleep(1);
        waited++;
    }

    if (ifname[0] == '\0') {
        log_error("no network interface found within %d seconds\n\r", max_wait);
        return 1;
    }

    int rc = do_dhcp(ifname);
    if (rc == 0) log_info("dhcp succeeded on %s\n\r", ifname);
    else log_error("dhcp failed on %s\n\r", ifname);

    /* start control socket loop */
    int ctl_fd = setup_control_socket();
    if (ctl_fd < 0) return 1;

    log_info("control socket listening at %s\n\r", CONTROL_SOCKET_PATH);

    for (;;) {
        int cfd = accept(ctl_fd, NULL, NULL);
        if (cfd < 0) {
            log_warn("accept: %s\n\r", strerror(errno));
            continue;
        }

        char cmd[128];
        ssize_t n = read(cfd, cmd, sizeof(cmd)-1);
        if (n <= 0) { close(cfd); continue; }
        cmd[n] = 0;

        /* very basic command handler */
        if (strncmp(cmd, "status", 6) == 0) {
            dprintf(cfd, "dhcp %s\n", rc==0 ? "ok" : "failed");
        } else if (strncmp(cmd, "iface", 5) == 0) {
            dprintf(cfd, "iface %s\n", ifname);
        } else {
            dprintf(cfd, "unknown\n");
        }
        close(cfd);
    }

    return 0;
}