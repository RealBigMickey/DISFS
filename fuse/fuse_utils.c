#include "fuse_utils.h"
#include <stdlib.h>
#include <errno.h>
#include <libgen.h>



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

static size_t write_file_cb(void *ptr, size_t sz, size_t nm, void *userdata)
{
    return fwrite(ptr, sz, nm, (FILE *)userdata);
}



/* Returns 0 on successful HTTP request, else -1.
 * If status exists, fill it with the HTTP response code.
 * LSB on status's address dictates GET or POST,
 * GET -> 0, POST -> 1
 */
int http_request(const char *url, string_buf_t *resp, uint32_t *status)
{
    CURL *c = curl_easy_init();
    if (!c)
        return -1;

    curl_easy_setopt(c, CURLOPT_URL, url);

    if ((uintptr_t)status & 1) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);  // make it POST
        status = (uint32_t*)((uintptr_t)status & ~1);
    }


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
    curl_easy_cleanup(c);
    return (rc == CURLE_OK) ? 0 : -1;
}



/* Sends a POST request to destined http, saves response to status_out.
 * Discards response string.
 */
int http_post_status(const char *url, uint32_t *status_out)
{
    // creates LSB flag for http_request to POST
    uint32_t status = 0;
    uint32_t *status_ptr = (uint32_t*)((uintptr_t)&status | 1);
    if (http_request(url, NULL, status_ptr) != 0)
        return -ECOMM;
    if (status_out)
        *status_out = status;
    return 0;
}



/* Gets file from http stream */
int http_get_stream(const char *url, FILE *out)
{
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



void mkdir_p(const char *dir)
{
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



char *url_encode(const char* path)
{
    CURL *c = curl_easy_init();
    if (!c)
        return NULL;

    char *esc = curl_easy_escape(c, path, 0);
    curl_easy_cleanup(c);

    /* Check against string buffer overflows */
    size_t len = strnlen(esc, 512 + 1);
    if (len > 512) {
        curl_free(esc);
        return NULL;
    }

    return esc;
}




int same_parent_dir(const char *a, const char *b)
{
    char *da = strdup(a), *db = strdup(b);
    if (!da || !db) {
        free(da);
        free(db);
        return 0;
    }
    int returner = (strcmp(dirname(da), dirname(db)) == 0);
    free(da);
    free(db);
    return returner;
}



int backend_unlink(int current_user_id, const char *path)
{
    char *esc = url_encode(path);
    if (!esc) return -EIO;
    char url[URL_MAX];
    snprintf(url, sizeof(url),
             "%s/unlink?user_id=%d&path=%s",
             get_server_url(), current_user_id, esc);
    curl_free(esc);

    uint32_t status = 0;
    if (http_post_status(url, &status) != 0)
        return -ECOMM;
    if (status == 201)
        return 0;
    if (status == 520)
        return -ENOENT;
    return -EIO;
}

/* uploads the file at cache_path to the server backend */
int upload_file_chunks(const char *logical_path, int current_user_id, size_t size, const char *cache_path, time_t mtime)
{
    FILE *fp = fopen(cache_path, "rb");
    if (!fp)
        return -EIO;

    /* In case of empty files, somehow */
    int total_chunks = (size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int end_chunk = total_chunks > 0 ? total_chunks - 1 : 0;


    char *esc = url_encode(logical_path);
    if (!esc) {
        fclose(fp);
        return -EIO;
    }

    char url[URL_MAX];
    snprintf(url, sizeof(url),
            "%s/prep_upload?user_id=%d&path=%s&size=%lu&end_chunk=%d&mtime=%lld",
            get_server_url(), current_user_id, esc, (unsigned long)size, end_chunk, (long long)mtime);
    curl_free(esc);

    LOGMSG("url sent: %s", url);

    uint32_t status = 0;
    if (http_post_status(url, &status) != 0) {
        fclose(fp);
        return -ECOMM;
    }
    if (status != 201) {
        fclose(fp);
        return -EIO;
    }

    /* Allocate chunk buffer */
    void *chunk_buf = malloc(CHUNK_SIZE);
    if (!chunk_buf) {
        fclose(fp);
        return -ENOMEM;
    }

    /* Upload chunks sequentially */
    int returner = 0;
    int chunk = 0;
    size_t n;
    while ((n = fread(chunk_buf, 1, CHUNK_SIZE, fp)) > 0) {
        LOGMSG("Uploading chunk %d/%d", chunk, end_chunk);
        
        esc = url_encode(logical_path);
        if (!esc) {
            returner = -EIO;
            break;
        }

        snprintf(url, sizeof(url),
                "%s/upload?user_id=%d&path=%s&chunk=%d",
                get_server_url(), current_user_id, esc, chunk);
        curl_free(esc);

        status = 0;
        if (http_post_stream(url, chunk_buf, n, &status) != 0) {
            returner = -ECOMM;
            break;
        }

        LOGMSG("Chunk %d upload status: %d", chunk, status);
        
        if (status != 201) {
            returner = -EIO;
            break;
        }

        chunk++;
    }
    
    free(chunk_buf);
    fclose(fp);

    return returner;
}



int backend_exists(int current_user_id, const char *path, int *exists_out)
{
    char *esc = url_encode(path);
    if (!esc)
        return -EIO;
    char url[URL_MAX];
    snprintf(url, sizeof(url),
             "%s/stat?user_id=%d&path=%s",
             get_server_url(), current_user_id, esc);
    curl_free(esc);

    uint32_t status = 0;
    if (http_request(url, NULL, &status) != 0)
        return -ECOMM;
    if (exists_out)
        *exists_out = (status == 201);
    return 0;
}


int cache_swap(const char *a, const char *b) {
#if defined(RENAME_EXCHANGE)
    if (renameat2(AT_FDCWD, a, AT_FDCWD, b, RENAME_EXCHANGE) == 0)
        return 0;
    return -errno;
#else
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s.__swap__.%d", a, getpid());
    if (rename(a, tmp) != 0)
        return -errno;
    if (rename(b, a) != 0)
        return -errno;
    if (rename(tmp, b) != 0)
        return -errno;
    return 0;
#endif
}

