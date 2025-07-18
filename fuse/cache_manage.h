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


/* Doubly linked-list for cached entries, earliest to latest */
typedef struct cache_entry_node {
    char *path;
    uint64_t size;
    time_t atime;
    struct cache_entry_node *next, *prev;
} cache_t;


time_t fetch_mtime(const char *path, int user_id);

int rmtree(const char *dir_path);
void cache_init(const char *cache_root);
void cache_exit(void);
void cache_record_add(const char *path, uint64_t size);
void cahce_record_remove(const char *path);
void cache_garbage_collection(void);