#include "cache_manage.h"


const static int max_bytes = 1024 * 1024 * 100;  // 100 MB
static uint64_t used_bytes = 0;
static cache_t *head = NULL, *tail = NULL;

/* "HOME/.cache/disfs/{user_id}" */
char cache_root[PATH_MAX] = {0};


time_t fetch_mtime(const char *path, int user_id) {
    char *esc = url_encode(path);
    if (!esc)
        return (time_t)-1;
    
    char url[URL_MAX];
    snprintf(url, sizeof(url),
            "http://%s/mtime?user_id=%d&path=%s",
            get_server_ip(), user_id, esc);
    curl_free(esc);

    string_buf_t resp = {0};
    uint32_t status = 0;
    int rc = http_get(url, &resp, &status);
    if (rc != 0 || status != 201) {
        free(resp.ptr);
        return (time_t)-1;
    }

    time_t mtime = (time_t)atoi(resp.ptr);
    free(resp.ptr);
    return mtime;
}



void cache_init(const char *root) {
    snprintf(cache_root, sizeof(cache_root), "%s/.cache/disfs/",
        getenv("HOME"));
    rmtree(cache_root);
}

int rmtree(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir)
        return -1;


    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (lstat(path, &st) == -1) 
            return -1;

        if(S_ISDIR(st.st_mode)) {
            rmtree(path);
        } else {
            unlink(path);
        }
    }

    closedir(dir);

    // remove final parent directory
    rmdir(dir_path);
    return 0;
}


void cache_exit(void) {
    cache_t *cur = head, *next = NULL;
    while (cur) {
        next = cur->next;
        free(cur->path);
        free(cur);
        cur = next;
    }
    rmtree(cache_root);
}