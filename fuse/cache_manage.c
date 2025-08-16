#include "cache_manage.h"

#define MUTEX_LOCK(x) pthread_mutex_lock(&x)
#define MUTEX_UNLOCK(x) pthread_mutex_unlock(&x)

// 100 MB threshhold until clearing cache
#define CACHE_SIZE_THRESHOLD (1024ULL * 1024ULL * 100ULL)

static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;
const static uint64_t max_bytes = CACHE_SIZE_THRESHOLD;
static uint64_t used_bytes;
static cache_t *head, *tail;

/* e.g. "/home/user/.cache/disfs/" */
char cache_root[PATH_MAX] = {0};

/* Fetch mtime of file located on server's endpoint */
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
    int rc = http_request(url, &resp, &status);
    if (rc != 0 || status != 201) {
        free(resp.ptr);
        return (time_t)-1;
    }

    time_t mtime = (time_t)atol(resp.ptr);
    free(resp.ptr);
    return mtime;
}


/* Initialize cache, clear states, wipes any old cache */
void cache_init(void) {
    head = NULL, tail = NULL;
    used_bytes = 0;
    snprintf(cache_root, sizeof(cache_root), "%s/.cache/disfs/", getenv("HOME"));
    rmtree(cache_root);
    mkdir_p(cache_root);
    used_bytes = 0;
}


int rmtree(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir)
        return -1;


    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char child[PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (lstat(child, &st) == -1) {
            closedir(dir);
            return -1;
        }

        if(S_ISDIR(st.st_mode)) {
            rmtree(child);
            rmdir(child);
        } else {
            unlink(child);
        }
    }

    closedir(dir);

    return 0;
}

/* Clean-up on exit */
void cache_exit(void) {
    MUTEX_LOCK(cache_lock);
    cache_t *cur = head, *next = NULL;
    while (cur) {
        next = cur->next;
        free(cur->path);
        free(cur);
        cur = next;
    }
    head = tail = NULL;
    used_bytes = 0;
    MUTEX_UNLOCK(cache_lock);

    rmtree(cache_root);
}


/* Create and append a cache_t node to the end of list */
int cache_record_append(const char *path, off_t size, int current_user_id) {
    char cache_path[PATH_MAX];
    BUILD_CACHE_PATH(cache_path, current_user_id, path);
    LOGMSG("[GC] appending user %d's cache %s", current_user_id, path);

    MUTEX_LOCK(cache_lock);

    cache_t *n = malloc(sizeof(*n));
    if (!n) {
        MUTEX_UNLOCK(cache_lock);
        return -1;
    }
    
    n->path = strdup(cache_path);
    n->size = size;
    n->next = NULL;
    n->prev = tail;

    if (tail)
        tail->next = n;
    else
        head = n;

    
    tail = n;
    used_bytes += size;

    MUTEX_UNLOCK(cache_lock);
    return 0;
}


/* Pop oldest cache entry from list */
void cache_record_pop() {
    MUTEX_LOCK(cache_lock);

    if (!head) {
        MUTEX_UNLOCK(cache_lock);
        return;
    }

    cache_t *next = head->next;
    if (next)
        next->prev = NULL;
    else
        tail = NULL;

    LOGMSG("[GC] popping cache %s", head->path);

    used_bytes -= head->size;
    unlink(head->path);
    free(head->path);
    free(head);
    head = next;

    MUTEX_UNLOCK(cache_lock);
}

/* Delete cache_t entry with exact path, compares only path when size < 0 */
static int _cache_record_delete_no_size(const char *full_path);
int cache_record_delete(const char *full_path, off_t size) {
    if (size < 0)
        return _cache_record_delete_no_size(full_path);
    
    LOGMSG("[GC] deleting cache %s", full_path);

    MUTEX_LOCK(cache_lock);

    cache_t **indirect = &head;
    while (*indirect) {
        cache_t *cur = *indirect;
        if (cur->size == size && strcmp(cur->path, full_path) == 0)
            break;
        indirect = &cur->next;
    }

    if (!*indirect) {
        MUTEX_UNLOCK(cache_lock);
        return -1;
    }

    cache_t *to_delete = *indirect;
    if (to_delete->next)
        to_delete->next->prev = to_delete->prev;
    else
        tail = to_delete->prev;  // to_delete is tail

    /* Make sure head is updated IF it's the deleted node */
    *indirect = to_delete->next;

    used_bytes -= to_delete->size;
    free(to_delete->path);
    free(to_delete);

    MUTEX_UNLOCK(cache_lock);
    return 0;
}

/* Handle cases where size is unavailable */
static int _cache_record_delete_no_size(const char *full_path) {
    MUTEX_LOCK(cache_lock);

    cache_t **indirect = &head;
    while (*indirect) {
        cache_t *cur = *indirect;
        if (strcmp(cur->path, full_path) == 0)
            break;
        indirect = &cur->next;
    }

    if (!*indirect) {
        MUTEX_UNLOCK(cache_lock);
        return -1;
    }

    cache_t *to_delete = *indirect;
    if (to_delete->next)
        to_delete->next->prev = to_delete->prev;
    else
        tail = to_delete->prev;

    *indirect = to_delete->next;

    used_bytes -= to_delete->size;
    free(to_delete->path);
    free(to_delete);

    MUTEX_UNLOCK(cache_lock);
    return 0;
}


int cache_record_rename(const char *from_path, const char *to_path, off_t size) {
    LOGMSG("[GC] local rename cache %s -> %s", from_path, to_path);
    MUTEX_LOCK(cache_lock);

    cache_t **indirect = &head;
    while (*indirect) {
        cache_t *cur = *indirect;
        if (cur->size == size && strcmp(cur->path, from_path) == 0)
            break;
        indirect = &cur->next;
    }

    cache_t *node = *indirect;
    if (!node) {
        MUTEX_UNLOCK(cache_lock);
        return -1;
    }
    free(node->path);
    node->path = strdup(to_path);

    MUTEX_UNLOCK(cache_lock);
    return 0;
}



/* Evict oldest entries until under size threshold */
void cache_garbage_collection(int current_user_id) {
    while (used_bytes > max_bytes) {
        LOGMSG("[GC] overflow detected, booting earliest cache.");
        cache_record_pop(current_user_id);
    }
}