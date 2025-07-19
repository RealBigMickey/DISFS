#pragma once
#include <stdint.h>
#include <sys/types.h>
#include "fuse_utils.h"
#include "server_config.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

/* cache stored at -> HOME/.cache/disfs/{user_id}/{local_path}  */
#define BUILD_CACHE_PATH(cache_path, id, path)  \
            snprintf(cache_path, sizeof(cache_path), "%s/.cache/disfs/%d%s", \
            getenv("HOME"), id, path)


/* Doubly linked-list for cached entries, earliest to latest */
typedef struct cache_entry_node {
    char *path;
    uint64_t size;
    time_t atime;
    struct cache_entry_node *next, *prev;
} cache_t;


time_t fetch_mtime(const char *path, int user_id);

int rmtree(const char *dir_path);
void cache_init(void);
void cache_exit(void);
void cache_record_append(const char *path, uint64_t size, int current_user_id);
void cache_record_delete(const char *path, uint64_t size, int current_user_id);
void cache_record_pop(int current_user_id);

void cache_garbage_collection(int current_user_id);