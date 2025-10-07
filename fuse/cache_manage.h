#pragma once
#include <stdint.h>
#include <sys/types.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <libgen.h>

#include "fuse_utils.h"
#include "server_config.h"
#include "debug.h"

#define CACHE_RM_FILESYSTEM (1 << 0)
#define CACHE_RM_CACHELOGS (1 << 1)
#define CACHE_RM_IGNORE_ENOENT (1 << 2)


extern char project_root[PATH_MAX];

/* for debug.h */
extern char logs_debug_path[PATH_MAX];
extern char cache_debug_path[PATH_MAX];

/* cache stored at -> HOME/.cache/disfs/{user_id}/{local_path}  */
#define BUILD_CACHE_PATH(buffer, id, path)  \
            snprintf( buffer, sizeof(buffer), "%s/.cache/disfs/%d%s", \
              getenv("HOME"), id, path )


/* Doubly linked-list for cached entries, earliest to latest */
typedef struct cache_entry_node {
    char *path;
    off_t size;
    time_t atime;
    struct cache_entry_node *next, *prev;
} cache_t;


time_t fetch_mtime(const char *path, int user_id);

int rmtree(const char *dir_path);
int cache_init(void);
void cache_exit(void);
int cache_record_append(const char *path, off_t size, int current_user_id);
int cache_record_delete(const char *path, int current_user_id, off_t size);
void cache_record_pop();
int cache_record_rename(const char *from_path, const char *to_path, off_t size);
int cache_remove_subtree(const char *path, int current_user_id, unsigned flags);
void update_cache_status(void);

void cache_garbage_collection(int current_user_id);