#include "cache_manage.h"
#include <errno.h>

#define MUTEX_LOCK(x) pthread_mutex_lock(&x)
#define MUTEX_UNLOCK(x) pthread_mutex_unlock(&x)

// 100 MB threshhold until clearing cache
#define CACHE_SIZE_THRESHOLD (1024ULL * 1024ULL * 100ULL)

static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;
const static uint64_t max_bytes = CACHE_SIZE_THRESHOLD;
static uint64_t used_bytes = 0;
static cache_t *head, *tail;
static int cached_file_count = 0;

/* e.g. "/home/user/.cache/disfs/" */
char cache_root[PATH_MAX] = {0};

/* location of project dir root */
char project_root[PATH_MAX] = {0};

/* for debug.h */
char logs_debug_path[PATH_MAX] = {0};
char cache_debug_path[PATH_MAX] = {0};

/* Tries to get the project root, returns 0 on success */
int _init_project_root(void) {
    LOGMSG("Starting init_project_root");
    const char *env_root = getenv("PROJECT_ROOT");
    char resolved[PATH_MAX] = {0};

    // Try environment variable (hopefully set by makefile)
    if (env_root && *env_root) {
        if (!realpath(env_root, resolved))
            return -1;
        strncpy(project_root, resolved, sizeof(project_root) - 1);
    } else {
        // Else, reference from executable path
        char exe_path[PATH_MAX] = {0};
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len > 0) {
            exe_path[len] = '\0';
            strncpy(project_root, dirname(exe_path), sizeof(project_root) - 1);
        } else {
            if (!getcwd(project_root, sizeof(project_root)))
                return -1;
        }
    }
    /* Ensure directory ends with "DISFS" */
    const char *basename = strrchr(project_root, '/');
    if (!basename || strcmp(basename + 1, "DISFS") != 0)
        return -1;


    /* Annoying warning message by compiler */
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(logs_debug_path, sizeof(logs_debug_path), "%s/logs.txt", project_root);
    snprintf(cache_debug_path, sizeof(cache_debug_path), "%s/cache_status.txt", project_root);
    #pragma GCC diagnostic pop

    LOGMSG("Built project_root as: %s", project_root);
    return 0;
}


/* Initialize cache, clear states, wipes any old cache */
int cache_init(void) {
    if (_init_project_root() != 0)
        return -1;

    head = NULL, tail = NULL;
    used_bytes = 0;
    cached_file_count = 0;
    snprintf(cache_root, sizeof(cache_root), "%s/.cache/disfs/", getenv("HOME"));
    rmtree(cache_root);
    mkdir_p(cache_root);
    return 0;
}


/* Fetch mtime of file located on server's endpoint */
time_t fetch_mtime(const char *path, int user_id) {
    char *esc = url_encode(path);
    if (!esc)
        return (time_t)-1;
    
    char url[URL_MAX];
    snprintf(url, sizeof(url),
            "%s/mtime?user_id=%d&path=%s",
            get_server_url(), user_id, esc);
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


static int _cache_record_delete_no_size(const char *full_path);
/* Nukes files and directories without regard */
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

/* Nukes files and directories, along with cache entries */
static int _rm_tree_with_cache(const char *abs_path, unsigned flags)
{
    struct stat st;
    if (lstat(abs_path, &st) != 0) {
        if ((flags & CACHE_RM_IGNORE_ENOENT) && errno == ENOENT) return 0;
        return -errno;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(abs_path);
        if (!dir) {
            if ((flags & CACHE_RM_IGNORE_ENOENT) && errno == ENOENT) return 0;
            return -errno;
        }

        struct dirent *entry;
        char child[PATH_MAX];

        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.' &&
                (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
                continue;

            int n = snprintf(child, sizeof(child), "%s/%s", abs_path, entry->d_name);
            if (n <= 0 || n >= (int)sizeof(child)) { closedir(dir); return -ENAMETOOLONG; }

            int rc = _rm_tree_with_cache(child, flags);
            if (rc != 0) { closedir(dir); return rc; }
        }
        closedir(dir);

        /* directories usually aren’t tracked in cache list; skip log removal */
        if (flags & CACHE_RM_FILESYSTEM) {
            if (rmdir(abs_path) != 0 && !(errno == ENOENT && (flags & CACHE_RM_IGNORE_ENOENT)))
                return -errno;
        }
        return 0;
    }

    if (flags & CACHE_RM_CACHELOGS) {
        // calling cache_record_delete would be faster, TBD
        _cache_record_delete_no_size(abs_path);
    }
    if (flags & CACHE_RM_FILESYSTEM) {
        if (unlink(abs_path) != 0 && !(errno == ENOENT && (flags & CACHE_RM_IGNORE_ENOENT)))
            return -errno;
    }
    return 0;
}


int cache_remove_subtree(const char *path, int current_user_id, unsigned flags)
{
    if (!path)
        return -EINVAL;

    char full_path[PATH_MAX];
    BUILD_CACHE_PATH(full_path, current_user_id, path);
    return _rm_tree_with_cache(full_path, flags);
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
    cached_file_count = 0;
    MUTEX_UNLOCK(cache_lock);

    rmtree(cache_root);
}


/* Create and append a cache_t node to the end of list */
int cache_record_append(const char *path, off_t size, int current_user_id) {
    char cache_path[PATH_MAX];
    BUILD_CACHE_PATH(cache_path, current_user_id, path);
    LOGMSG("[GC] Appending cache: %s", cache_path);

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
    cached_file_count++;

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
    cached_file_count--;
    unlink(head->path);
    free(head->path);
    free(head);
    head = next;

    MUTEX_UNLOCK(cache_lock);
}

/* Delete cache_t entry with exact path, compares only path when size < 0 */
static int _cache_record_delete_no_size(const char *full_path);
int cache_record_delete(const char *path, int current_user_id, off_t size) {
    char full_path[PATH_MAX];
    BUILD_CACHE_PATH(full_path, current_user_id, path);

    if (size < 0)
        return _cache_record_delete_no_size(full_path);
    
    LOGMSG("[GC] Deleting cache: %s", full_path);

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
    cached_file_count--;
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
    cached_file_count--;
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

/* TEMP! To be changed into a proper command.
 * Done the simplest way for debug and testing 
 */
void update_cache_status(void) {
    if (!DISPLAY_CACHE_STATUS)
        return;
    LOGCACHE("[Cache status]\n"
            "- Used bytes: %lu\n"
            "- Files cached: %d",
            used_bytes, cached_file_count);
}


/* Evict oldest entries until under size threshold */
void cache_garbage_collection(int current_user_id) {
    while (used_bytes > max_bytes) {
        LOGMSG("[GC] overflow detected, booting earliest cache.");
        cache_record_pop(current_user_id);
    }
}