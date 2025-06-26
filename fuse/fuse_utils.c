#include "fuse_utils.h"
#include <stdlib.h>
#include <string.h>

#define NO_LSLASH(x) (*x == '/' ? x+1 : x)

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

static size_t write_file_cb(void *ptr, size_t sz, size_t nm, void *userdata) {
    return fwrite(ptr, sz, nm, (FILE *)userdata);
}

/* Returns 0 on successful HTTP request, else -1.
 * If status exists, fill it with the HTTP response code.
 * LSB on status's address dictates GET or POST,
 * GET -> 0, POST -> 1
 */
int http_get(const char *url, string_buf_t *resp, uint32_t *status) {
    CURL *c = curl_easy_init();
    if (!c)
        return -1;

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);  // failure on > 400

    if ((uintptr_t)status & 1)
        curl_easy_setopt(c, CURLOPT_POST, 1L);  // make it POST


    if (resp) {
        resp->ptr = malloc(1);
        resp->len = 0;
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, resp);
    } else {
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, NULL);
    }

    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);

    if (status)
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, status);
    return (rc == CURLE_OK) ? 0 : -1;
}

/* Gets file from http stream */
int http_get_stream(const char *url, FILE *out) {
    CURL *c = curl_easy_init();
    if (!c)
        return -1;

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_file_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, out);

    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    return (rc == CURLE_OK) ? 0 : -1;
}


/* Sends file through http */
int http_post_stream(const char *url, const void *data, size_t len, uint32_t *status)
{
    CURL *c = curl_easy_init();
    if (!c)
        return -1;
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)len);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    
    CURLcode rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(c);

    if (status)
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, status);
    return (rc == CURLE_OK) ? 0 : -1;
}



void mkdir_p(const char *dir) {
    char tmp[PATH_MAX];
    char *p;
    size_t len;
    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (tmp[len-1] == '/') tmp[len-1] = 0;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}




void local_cache_path(char *dst, size_t dstsz,
                             const char *user_home,
                             const char *remote_path)
{
    snprintf(dst, dstsz, "%s/.cache/disfs/%s", user_home, NO_LSLASH(remote_path)); // skip leading '/'
}

