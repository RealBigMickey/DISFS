#pragma once

#include <curl/curl.h>

typedef struct string_buf {
    char *ptr;
    size_t len;
} string_buf_t;


size_t write_cb(void *data, size_t size, size_t nmemb, void *userp);

int http_get(const char *url, string_buf_t *resp, u_int32_t *status);