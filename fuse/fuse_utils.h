#pragma once

#include <curl/curl.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct string_buf {
    char *ptr;
    size_t len;
} string_buf_t;


size_t write_cb(void *data, size_t size, size_t nmemb, void *userp);

int http_get(const char *url, string_buf_t *resp, u_int32_t *status);


void local_cache_path(char *dst, size_t dstsz,
                            const char *user_home,
                            const char *remote_path);

int http_get_stream(const char *url, FILE *out);


void mkdir_p(const char *dir);