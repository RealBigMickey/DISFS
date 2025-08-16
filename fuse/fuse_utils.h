#pragma once
#define _GNU_SOURCE
#include <curl/curl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "server_config.h"
#include "debug.h"

#define URL_MAX 512
#define CHUNK_SIZE (10 * 1024 * 1024 - 256)  // ~10 MB with some overhead


typedef struct string_buf {
    char *ptr;
    size_t len;
} string_buf_t;

size_t write_cb(void *data, size_t size, size_t nmemb, void *userp);


#define IS_COMMAND_PATH(p) (strncmp(p, "/.command", 9) == 0)

/* e.g. "/foo/dog" would return at the location of 'd'*/
static inline const char* base_name(const char *p) {
    const char *slash = strrchr(p, '/');
    return slash ? slash + 1 : p;
}

/* Treats ANY dot-basename as a temp file, except for commands */
static inline int is_temp_path(const char *p) {
    if (IS_COMMAND_PATH(p))
        return 0;
    const char *b = base_name(p);
    return b[0] == '.';
}
int same_parent_dir(const char *a, const char *b) ;



int http_request(const char *url, string_buf_t *resp, u_int32_t *status);
int http_post_status(const char *url, uint32_t *status_out);
int http_get_stream(const char *url, FILE *out);
int http_post_stream(const char *url, const void *data, size_t len, uint32_t *status);

char *url_encode(const char* path);
void mkdir_p(const char *dir);

int upload_file_chunks(int current_user_id, const char *logical_path, const char *cache_path);


int backend_unlink(int current_user_id, const char *path);
int backend_exists(int current_user_id, const char *path, int *exists_out);

int cache_swap(const char *a, const char *b);