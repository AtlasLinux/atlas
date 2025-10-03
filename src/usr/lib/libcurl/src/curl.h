#ifndef CURL_H
#define CURL_H

#include <stdio.h>

/* return codes */
#define CURLE_OK 0
#define CURLE_COULDNT_RESOLVE_HOST 6
#define CURLE_COULDNT_CONNECT 7
#define CURLE_SSL_CONNECT_ERROR 35
#define CURLE_SEND_ERROR 55
#define CURLE_RECV_ERROR 56
#define CURLE_OTHER_ERROR 99

typedef struct CURL CURL;

/* Global init/cleanup */
int curl_global_init(long flags);
void curl_global_cleanup(void);

/* Handle lifecycle */
CURL *curl_easy_init(void);
void curl_easy_cleanup(CURL *handle);

/* Options */
typedef enum {
    CURLOPT_URL = 10000,
    CURLOPT_WRITEDATA,
    CURLOPT_VERBOSE
} CURLoption;

int curl_easy_setopt(CURL *handle, CURLoption opt, void *param);

/* Perform */
int curl_easy_perform(CURL *handle);

#endif /* CURL_H */
