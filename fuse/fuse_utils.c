#include "fuse_utils.h"
#include <stdlib.h>
#include <string.h>

// size = size of each member, nmemb = number of members
size_t write_cb(void *data, size_t size, size_t nmemb, void *userp)
{
    string_buf_t *buf = (string_buf_t*)userp;
    size_t total = size * nmemb;
    char *tmp = realloc(buf->ptr, buf->len + total + 1);
    if (!tmp)
        return 0;
    buf->ptr = tmp;
    memcpy(buf->ptr + buf->len, data, total);
    buf->len += total;
    buf->ptr[buf->len] = '\0';

    return total;
}

/* Returns 0 on successful HTTP request, else -1.
 * If status exists, fill it with the HTTP response code.
 */
int http_get(const char *url, string_buf_t *resp, u_int32_t *status) {
    CURL *c = curl_easy_init();
    if (!c)
        return -1;

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);  // failure on 4XX 5XX 

    if (resp) {
        resp->ptr = malloc(1);
        resp->len = 0;
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, resp);
    } else {
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, NULL);
    }

    CURLcode rc = curl_easy_perform(c);

    if (status)
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, status);
    if (rc == CURLE_OK) {
        curl_easy_cleanup(c);
        return 0;
    }

    curl_easy_cleanup(c);
    return -1;
}


