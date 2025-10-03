#define _POSIX_C_SOURCE 200112L
#include "curl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
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
    struct addrinfo hints={0},*res=NULL;
    hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM;
    int rv=getaddrinfo(h->u.host,h->u.portstr,&hints,&res);
    if(rv!=0){ if(h->verbose) fprintf(stderr,"resolve: %s\n",gai_strerror(rv)); return CURLE_COULDNT_RESOLVE_HOST; }
    int sock=-1; for(struct addrinfo*rp=res;rp;rp=rp->ai_next){ sock=socket(rp->ai_family,rp->ai_socktype,rp->ai_protocol); if(sock<0) continue;
        if(connect(sock,rp->ai_addr,rp->ai_addrlen)==0) break; close(sock); sock=-1; }
    freeaddrinfo(res); if(sock<0) return CURLE_COULDNT_CONNECT;

    SSL_CTX *ctx=NULL; SSL *ssl=NULL;
    if(h->u.use_ssl){ ctx=SSL_CTX_new(TLS_client_method()); if(!ctx){close(sock);return CURLE_SSL_CONNECT_ERROR;}
        ssl=SSL_new(ctx); SSL_set_fd(ssl,sock);
        if(SSL_connect(ssl)<=0){ if(h->verbose) ERR_print_errors_fp(stderr);
            SSL_free(ssl); SSL_CTX_free(ctx); close(sock); return CURLE_SSL_CONNECT_ERROR;} }

    char req[2048]; snprintf(req,sizeof(req),
        "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",h->u.path,h->u.host);

    size_t sent=0,len=strlen(req);
    while(sent<len){ int n=h->u.use_ssl?SSL_write(ssl,req+sent,(int)(len-sent)):send(sock,req+sent,len-sent,0);
        if(n<=0){ if(h->verbose) fprintf(stderr,"send error\n"); goto fail; } sent+=n; }

    int header_done=0; unsigned char hdr[8192]; size_t hdrlen=0; unsigned char buf[4096];
    for(;;){ int n=h->u.use_ssl?SSL_read(ssl,buf,sizeof(buf)):recv(sock,buf,sizeof(buf),0);
        if(n==0) break; if(n<0) goto fail;
        if(!header_done){ if(hdrlen+(size_t)n>sizeof(hdr)) goto fail;
            memcpy(hdr+hdrlen,buf,n); hdrlen+=n;
            void*e=find_double_crlf(hdr,hdrlen); if(e){ header_done=1; size_t hl=(unsigned char*)e+4-hdr; size_t bl=hdrlen-hl;
                if(bl>0) fwrite(hdr+hl,1,bl,h->out);} }
        else fwrite(buf,1,n,h->out); }

    if(h->u.use_ssl){SSL_shutdown(ssl);SSL_free(ssl);SSL_CTX_free(ctx);} close(sock); return CURLE_OK;
fail:
    if(h->u.use_ssl){ if(ssl){SSL_shutdown(ssl);SSL_free(ssl);} if(ctx)SSL_CTX_free(ctx);} if(sock>=0)close(sock);
    return CURLE_RECV_ERROR;
}
