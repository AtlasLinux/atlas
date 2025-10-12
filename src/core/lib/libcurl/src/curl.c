#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "curl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

struct url_parts {
    int use_ssl;
    char host[256];
    char portstr[8];
    int port;
    char path[1024];
};

struct CURL {
    char url[2048];
    FILE *out;
    int verbose;
    struct url_parts u;
};

/* simple header search */
static void *find_double_crlf(const void *buf, size_t len) {
    const unsigned char *b = buf;
    for (size_t i = 0; i + 3 < len; i++) {
        if (b[i]=='\r' && b[i+1]=='\n' && b[i+2]=='\r' && b[i+3]=='\n')
            return (void*)(b+i);
    }
    return NULL;
}

static void parse_url(const char *url, struct url_parts *u) {
    memset(u, 0, sizeof(*u));
    u->use_ssl = 1; u->port = 443; strcpy(u->portstr,"443");
    const char *p = url;
    if (strncmp(p,"http://",7)==0) { u->use_ssl=0;u->port=80;strcpy(u->portstr,"80"); p+=7; }
    else if (strncmp(p,"https://",8)==0) { u->use_ssl=1;u->port=443;strcpy(u->portstr,"443"); p+=8; }
    const char *slash=strchr(p,'/'); const char *hostend=slash?slash:p+strlen(p);
    const char *colon=memchr(p,':',hostend-p);
    if (colon) {
        size_t hlen=colon-p; if(hlen>=sizeof(u->host)) hlen=sizeof(u->host)-1;
        memcpy(u->host,p,hlen); u->host[hlen]='\0';
        u->port=atoi(colon+1); snprintf(u->portstr,sizeof(u->portstr),"%d",u->port);
    } else {
        size_t hlen=hostend-p; if(hlen>=sizeof(u->host)) hlen=sizeof(u->host)-1;
        memcpy(u->host,p,hlen); u->host[hlen]='\0';
    }
    if (slash) snprintf(u->path,sizeof(u->path),"%s",slash); else strcpy(u->path,"/");
}

/* globals */
int curl_global_init(long flags) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
#endif
    (void)flags;
    return CURLE_OK;
}
void curl_global_cleanup(void) {}

/* lifecycle */
CURL *curl_easy_init(void) {
    CURL *h=calloc(1,sizeof(*h)); if(!h) return NULL;
    h->out=stdout; h->verbose=0; return h;
}
void curl_easy_cleanup(CURL *h){ if(h) free(h); }

/* options */
int curl_easy_setopt(CURL *h,CURLoption opt,void *param){
    if(!h) return -1;
    switch(opt){
    case CURLOPT_URL:
        strncpy(h->url,(const char*)param,sizeof(h->url)-1);
        parse_url(h->url,&h->u); return 0;
    case CURLOPT_WRITEDATA: h->out=(FILE*)param; return 0;
    case CURLOPT_VERBOSE: h->verbose=(int)(intptr_t)param; return 0;
    default: return -1;
    }
}

/* perform */
int curl_easy_perform(CURL *h){
    if(!h||!h->url[0]) return CURLE_OTHER_ERROR;

    char cur_url[2048];
    strncpy(cur_url, h->url, sizeof(cur_url)-1);
    cur_url[sizeof(cur_url)-1] = '\0';

    const int max_redirects = 10;

    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    int sock = -1;

    for (int redirect_count = 0; redirect_count <= max_redirects; ++redirect_count) {
        /* cleanup previous iteration */
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); ssl = NULL; }
        if (ctx)  { SSL_CTX_free(ctx); ctx = NULL; }
        if (sock >= 0) { close(sock); sock = -1; }

        parse_url(cur_url, &h->u);

        struct addrinfo hints={0},*res=NULL;
        hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM;
        int rv=getaddrinfo(h->u.host,h->u.portstr,&hints,&res);
        if(rv!=0){ if(h->verbose) fprintf(stderr,"resolve: %s\n",gai_strerror(rv)); return CURLE_COULDNT_RESOLVE_HOST; }

        sock=-1;
        for(struct addrinfo*rp=res;rp;rp=rp->ai_next){
            int s = socket(rp->ai_family,rp->ai_socktype,rp->ai_protocol);
            if(s<0) continue;
            if(connect(s,rp->ai_addr,rp->ai_addrlen)==0) { sock = s; break; }
            close(s);
        }
        freeaddrinfo(res);
        if(sock<0) return CURLE_COULDNT_CONNECT;

        if(h->u.use_ssl){
            ctx = SSL_CTX_new(TLS_client_method());
            if(!ctx){ close(sock); sock=-1; return CURLE_SSL_CONNECT_ERROR; }
            /* Optionally configure CA verification with SSL_CTX_load_verify_locations & SSL_CTX_set_verify */
            ssl = SSL_new(ctx);
            if(!ssl){ SSL_CTX_free(ctx); ctx=NULL; close(sock); sock=-1; return CURLE_SSL_CONNECT_ERROR; }
            SSL_set_fd(ssl,sock);
            /* SNI */
            if(!SSL_set_tlsext_host_name(ssl, h->u.host)) {
                if(h->verbose) fprintf(stderr,"Warning: failed to set SNI host\n");
            }
            if(SSL_connect(ssl)<=0){
                if(h->verbose) ERR_print_errors_fp(stderr);
                goto fail;
            }
        }

        /* Host header (include port if non-default) */
        char host_hdr[512];
        if ((h->u.use_ssl && h->u.port != 443) || (!h->u.use_ssl && h->u.port != 80))
            snprintf(host_hdr, sizeof(host_hdr), "%s:%d", h->u.host, h->u.port);
        else
            snprintf(host_hdr, sizeof(host_hdr), "%s", h->u.host);

        char req[4096];
        snprintf(req,sizeof(req),
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: mini-libcurl/1.0\r\n"
                 "Accept: */*\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 h->u.path, host_hdr);

        /* send request (handle SSL WANT states) */
        size_t sent=0,len=strlen(req);
        while(sent<len){
            int n = h->u.use_ssl ? SSL_write(ssl, req+sent, (int)(len-sent)) : (int)send(sock, req+sent, len-sent, 0);
            if(n <= 0){
                if(h->u.use_ssl){
                    int err = SSL_get_error(ssl,n);
                    if(err==SSL_ERROR_WANT_READ || err==SSL_ERROR_WANT_WRITE) continue;
                }
                if(h->verbose) fprintf(stderr,"send error\n");
                goto fail;
            }
            sent += (size_t)n;
        }

        /* read headers first, then body */
        unsigned char hdr[65536]; size_t hdrlen=0;
        unsigned char buf[8192];
        int header_done = 0;
        int status_code = 0;
        char location[2048] = {0};
        int is_chunked = 0;
        long content_length = -1;

        /* helper to trim whitespace in place */
        auto_trim:;
        ; /* label only */

        for(;;){
            int n = h->u.use_ssl ? SSL_read(ssl, buf, sizeof(buf)) : (int)recv(sock, buf, sizeof(buf), 0);
            if(n == 0) break;
            if(n < 0){
                if(h->u.use_ssl){
                    int err = SSL_get_error(ssl,n);
                    if(err==SSL_ERROR_WANT_READ || err==SSL_ERROR_WANT_WRITE) continue;
                }
                goto fail;
            }

            if(!header_done){
                if(hdrlen + (size_t)n > sizeof(hdr)) goto fail;
                memcpy(hdr + hdrlen, buf, (size_t)n);
                hdrlen += (size_t)n;
                void *e = find_double_crlf(hdr, hdrlen);
                if(!e) continue;

                /* parse header block */
                size_t hl = (unsigned char*)e + 4 - hdr;
                size_t copylen = hl < sizeof(hdr) ? hl : sizeof(hdr)-1;
                hdr[copylen] = '\0';
                char *hdrstr = (char*)hdr;

                /* parse status line */
                char *line_end = strstr(hdrstr, "\r\n");
                if(line_end) *line_end = '\0';
                if(sscanf(hdrstr, "HTTP/%*s %d", &status_code) != 1) status_code = 0;

                /* iterate headers line by line */
                char *p = hdrstr;
                /* skip status line if it was truncated earlier */
                if(line_end) p = line_end + 2;
                while(p && *p){
                    char *nl = strstr(p, "\r\n");
                    if(!nl) nl = strchr(p, '\n');
                    if(!nl) break;
                    /* isolate a header line in place */
                    char tmp = *nl; *nl = '\0';
                    /* header name/value split */
                    char *colon = strchr(p, ':');
                    if(colon){
                        *colon = '\0';
                        char *name = p;
                        char *val = colon + 1;
                        while(*val == ' ' || *val == '\t') val++;
                        /* lowercase compare */
                        for(char *cp=name; *cp; ++cp) if(*cp >= 'A' && *cp <= 'Z') *cp |= 0x20;
                        if(strncmp(name, "location", 8) == 0){
                            size_t vlen = strlen(val);
                            if(vlen >= sizeof(location)) vlen = sizeof(location)-1;
                            memcpy(location, val, vlen);
                            location[vlen] = '\0';
                        } else if(strncmp(name, "transfer-encoding", 17) == 0){
                            /* check if chunked appears in val */
                            char *vv = val;
                            for(; *vv; ++vv){
                                if(strncasecmp(vv, "chunked", 7)==0){ is_chunked = 1; break; }
                            }
                        } else if(strncmp(name, "content-length", 14) == 0){
                            content_length = atol(val);
                        }
                    }
                    *nl = tmp;
                    p = (nl + ((tmp=='\r')?2:1));
                }

                /* write any data after headers */
                size_t bl = hdrlen - hl;
                if(bl > 0){
                    if(is_chunked){
                        /* place the trailing bytes into a small buffer to be processed by chunk decoder below */
                        /* For simplicity, we will treat this as initial chunk buffer */
                    } else {
                        size_t wrote = fwrite(hdr + hl, 1, bl, h->out);
                        (void)wrote;
                    }
                }
                header_done = 1;
                /* if not chunked, continue reading body writes directly; if chunked, step into chunked decoder */
                if(is_chunked){
                    /* create a simple parser that consumes any bytes remaining in hdr+hl and then continues reading */
                    size_t offset = hl;
                    /* function-like block to decode chunks */
                    for(;;){
                        /* ensure we have a full chunk-size line */
                        /* read more into hdr if needed */
                        while(1){
                            unsigned char *pcrlf = find_double_crlf(hdr+offset, hdrlen-offset); /* reuse but not ideal; instead find first CRLF */
                            (void)pcrlf;
                            /* find CRLF for chunk-size line */
                            unsigned char *nlpos = NULL;
                            for(size_t i=offset;i+1<hdrlen;i++){
                                if(hdr[i]=='\r' && hdr[i+1]=='\n'){ nlpos = hdr + i; break; }
                                if(hdr[i]=='\n'){ nlpos = hdr + i; break; }
                            }
                            if(nlpos) break;
                            /* read more bytes */
                            int rn = h->u.use_ssl ? SSL_read(ssl, buf, sizeof(buf)) : (int)recv(sock, buf, sizeof(buf), 0);
                            if(rn <= 0) { if(rn==0) goto chunk_done; goto fail; }
                            if(hdrlen + (size_t)rn > sizeof(hdr)) goto fail;
                            memcpy(hdr + hdrlen, buf, (size_t)rn);
                            hdrlen += (size_t)rn;
                        }
                        /* find end of chunk-size line */
                        size_t i=offset;
                        size_t line_end_index = (size_t)-1;
                        for(; i+1<hdrlen; ++i){
                            if(hdr[i]=='\r' && hdr[i+1]=='\n'){ line_end_index = i; break; }
                            if(hdr[i]=='\n'){ line_end_index = i; break; }
                        }
                        if(line_end_index == (size_t)-1) goto fail;
                        /* parse hex size */
                        char szbuf[64] = {0};
                        size_t sl = line_end_index - offset;
                        if(sl >= sizeof(szbuf)) goto fail;
                        memcpy(szbuf, hdr+offset, sl);
                        unsigned long chunk_size = strtoul(szbuf, NULL, 16);
                        /* move offset to start of chunk data */
                        offset = line_end_index;
                        if(hdr[offset]=='\r' && hdr[offset+1]=='\n') offset += 2; else offset += 1;
                        if(chunk_size == 0) { /* last chunk; consume possible final CRLF then break */ 
                            /* consume trailing CRLF if present */
                            if(offset + 1 <= hdrlen && hdr[offset]=='\r' && hdr[offset+1]=='\n') offset += 2;
                            goto chunk_done;
                        }
                        /* ensure we have chunk_size bytes available, read more if needed */
                        while(hdrlen - offset < chunk_size){
                            int rn = h->u.use_ssl ? SSL_read(ssl, buf, sizeof(buf)) : (int)recv(sock, buf, sizeof(buf), 0);
                            if(rn <= 0){ if(rn==0) goto chunk_done; goto fail; }
                            if(hdrlen + (size_t)rn > sizeof(hdr)) goto fail;
                            memcpy(hdr + hdrlen, buf, (size_t)rn);
                            hdrlen += (size_t)rn;
                        }
                        /* write chunk data */
                        size_t wrote = fwrite(hdr + offset, 1, chunk_size, h->out);
                        (void)wrote;
                        offset += chunk_size;
                        /* consume trailing CRLF after chunk */
                        if(offset + 1 <= hdrlen && hdr[offset]=='\r' && hdr[offset+1]=='\n') offset += 2;
                        else if(offset < hdrlen && hdr[offset]=='\n') offset += 1;
                        /* shift remaining bytes to start of hdr buffer */
                        if(offset < hdrlen){
                            memmove(hdr, hdr + offset, hdrlen - offset);
                            hdrlen = hdrlen - offset;
                            offset = 0;
                        } else {
                            hdrlen = 0; offset = 0;
                        }
                    }
                    chunk_done:;
                    /* after chunked finished, break the outer read loop to handle redirects or finish */
                } else {
                    /* not chunked: continue reading and writing directly in outer loop */
                }
            } else {
                /* headers done and not chunked: write body bytes directly */
                size_t wrote = fwrite(buf, 1, (size_t)n, h->out);
                (void)wrote;
            }
        } /* end read loop */

        /* cleanup network objects before redirect handling */
        if(ssl){ SSL_shutdown(ssl); SSL_free(ssl); ssl = NULL; }
        if(ctx){ SSL_CTX_free(ctx); ctx = NULL; }
        if(sock >= 0){ close(sock); sock = -1; }

        /* handle redirect */
        if ((status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308) && location[0] != '\0') {
            char next_url[2048] = {0};
            /* trim leading whitespace */
            char *loc = location;
            while(*loc == ' ' || *loc == '\t') loc++;
            /* protocol-relative: //host/path */
            if (strncmp(loc, "//", 2) == 0) {
                snprintf(next_url, sizeof(next_url), "%s:%s", h->u.use_ssl ? "https" : "http", loc);
            } else if (strncmp(loc, "http://", 7) == 0 || strncmp(loc, "https://", 8) == 0) {
                snprintf(next_url, sizeof(next_url), "%s", loc);
            } else if (loc[0] == '/') {
                snprintf(next_url, sizeof(next_url), "%s://%s%s", h->u.use_ssl ? "https" : "http", h->u.host, loc);
            } else {
                /* relative path: resolve against current URL base */
                char base[2048];
                strncpy(base, cur_url, sizeof(base)-1); base[sizeof(base)-1] = '\0';
                char *p = strstr(base, "://");
                if(p) { p += 3; p = strchr(p, '/'); } else { p = strchr(base, '/'); }
                if(!p) {
                    snprintf(next_url, sizeof(next_url), "%s/%s", base, loc);
                } else {
                    char dir[2048];
                    strncpy(dir, base, sizeof(dir)-1);
                    dir[sizeof(dir)-1] = '\0';
                    char *last = strrchr(dir, '/');
                    if(last) *(last+1) = '\0';
                    snprintf(next_url, sizeof(next_url), "%s%s", dir, loc);
                }
            }

            if (h->verbose) fprintf(stderr, "redirect -> %s\n", next_url);
            if (redirect_count == max_redirects) return CURLE_RECV_ERROR;

            if(next_url[0]=='\0') return CURLE_RECV_ERROR;
            strncpy(cur_url, next_url, sizeof(cur_url)-1);
            cur_url[sizeof(cur_url)-1] = '\0';

            if (h->out != stdout) {
                if (fflush(h->out) == 0) {
                    int fd = fileno(h->out);
                    if (fd >= 0) ftruncate(fd, 0);
                    fseek(h->out, 0, SEEK_SET);
                }
            }
            continue;
        }

        return CURLE_OK;
    }

    return CURLE_RECV_ERROR;

fail:
    if(ssl){ SSL_shutdown(ssl); SSL_free(ssl); ssl = NULL; }
    if(ctx){ SSL_CTX_free(ctx); ctx = NULL; }
    if(sock>=0){ close(sock); sock = -1; }
    return CURLE_RECV_ERROR;
}
