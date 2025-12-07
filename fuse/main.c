#define FUSE_USE_VERSION 314

#include <fuse3/fuse.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <cjson/cJSON.h>
#include <linux/fs.h>

#include "fuse_utils.h"
#include "server_config.h"
#include "cache_manage.h"
#include "debug.h"  // Temporary

static int current_user_id;
static char current_username[32];
static int logged_in;

#define CSTR_LEN(s) (s), (sizeof(s) - 1)

#define SET_TIME(field, json_name) \
    do { \
        cJSON *jt = cJSON_GetObjectItemCaseSensitive(root, json_name); \
        if (cJSON_IsNumber(jt)) st->field = (time_t)jt->valueint; \
    } while(0)



static int do_getattr(const char *path, struct stat *st, struct fuse_file_info *fi)
{
    memset(st, 0, sizeof(struct stat));
    
    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        st->st_uid = fuse_get_context()->uid;
        st->st_gid = fuse_get_context()->gid;
        return 0;
    }

    if (IS_COMMAND_PATH(path)) {
        if (strcmp(path, "/.command/doggo") == 0 ||
            strncmp(path, CSTR_LEN("/.command/ping/")) == 0 ||
            strncmp(path, CSTR_LEN("/.command/register/")) == 0 ||
            strncmp(path, CSTR_LEN("/.command/serverip/")) == 0 ||
            strcmp(path, "/.command/pong") == 0) {

            st->st_mode = S_IFREG | 0644;
            st->st_nlink = 1;
            st->st_size = 128;
            return 0;
        }

        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }

    if (!logged_in)
        return -EACCES;

    char *esc = url_encode(path);
    if (!esc)
        return -EIO;

    char url[URL_MAX];
    snprintf(url, sizeof(url),
            "http://%s/stat?user_id=%d&path=%s",
            get_server_ip(), current_user_id, esc);
    curl_free(esc);

    string_buf_t resp = {0};
    uint32_t status = 0;
    int rc = http_request(url, &resp, &status);

    if (status == 520) {
        free(resp.ptr);
        return -ENOENT;
    }

    if (rc != 0) {
        free(resp.ptr);
        return -ECOMM;
    }

    if (status != 201) {
        free(resp.ptr);
        return -EIO;
    }

    // parse JSON
    cJSON *root = cJSON_Parse(resp.ptr);
    free(resp.ptr);
    if (!root) return -ENOENT;

    cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsNumber(t)) { cJSON_Delete(root); return -ENOENT; }

    int type = t->valueint;
    if (type == 2) {
        // Directory
        st->st_mode  = S_IFDIR | 0755;
        st->st_nlink = 2;
    }
    else if (type == 1) {
        // File
        st->st_mode  = S_IFREG | 0644;
        st->st_nlink = 1;

        cJSON *sz = cJSON_GetObjectItemCaseSensitive(root, "size");
        if (cJSON_IsNumber(sz))
            st->st_size = (off_t)sz->valueint;
    }
    else {
        cJSON_Delete(root);
        return -ENOENT;
    }
    
    st->st_uid = fuse_get_context()->uid;
    st->st_gid = fuse_get_context()->gid;

    SET_TIME(st_atime,  "atime");
    SET_TIME(st_mtime,  "mtime");
    SET_TIME(st_ctime,  "ctime");
    // Not all systems support creation time
    #ifdef HAVE_STRUCT_STAT_ST_BIRTHTIME
        SET_TIME(st_birthtime, "crtime");
    #endif

    cJSON_Delete(root);
    return 0;
}


static int do_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags)
{
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    // COMMANDS
    if (IS_COMMAND_PATH(path)) {
        filler(buf, "COMMANDS:", NULL, 0, 0);
        filler(buf, "serverip (Set server ip, defaults to localhost)", NULL, 0, 0);
        filler(buf, "register (register & login)", NULL, 0, 0);
        filler(buf, "ping (login)", NULL, 0, 0);
        filler(buf, "pong (logout)", NULL, 0, 0);
        filler(buf, "doggo (dog gif)", NULL, 0, 0);
        return 0;
    }

    if (strcmp(path, "/.command/ping") == 0 || strcmp(path, "/.command/register") == 0) {
        filler(buf, "Follow up with your username (no spaces)", NULL, 0, 0);
        return 0;
    }

    if (!logged_in) {
        if (strcmp(path,"/") == 0) {
            filler(buf, "You're not logged o.o", NULL, 0, 0);
            filler(buf, "do .command for commands", NULL, 0, 0);
            return 0;
        } 
        return -ENOENT;
    }
    
    /* For better debugging */
    update_cache_status();

    char *esc = url_encode(path);
    if (!esc)
        return -EIO;
    
    char url[URL_MAX];
    snprintf(url,sizeof(url),
            "http://%s/listdir?user_id=%d&path=%s",
            get_server_ip(), current_user_id, esc);
    curl_free(esc);

    string_buf_t resp = {0};
    uint32_t status = 0;
    if (http_request(url, &resp, &status) == 0) {
        if (status != 201) {
            free(resp.ptr);
            return 0;
        }

        cJSON *array = cJSON_Parse(resp.ptr);
        free(resp.ptr);
        if (cJSON_IsArray(array)) {
            cJSON *item;
            cJSON_ArrayForEach(item, array) {
                cJSON *item_name = cJSON_GetObjectItemCaseSensitive(item, "name");
                if (cJSON_IsString(item_name))
                    filler(buf, item_name->valuestring, NULL, 0, 0);
            }
        }
        cJSON_Delete(array);
    }
    return 0;
}

#define LOGIN_TEXT "NOT LOGGED IN! Type '$ cat .command/ping/{username}' to login.\n"
static int do_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi)
{
    LOGMSG("IN read");
    // handle commands
    if (IS_COMMAND_PATH(path)) {
        if (strncmp(path, CSTR_LEN("/.command/serverip/")) == 0) {
            const char *ip = path + sizeof("/.command/serverip/") - 1;
            int ret = change_server_ip(ip);
            if (ret == -1)
                return snprintf(buf, size, "Invalid format! e.g. 192.168.0.1\n");
            if (ret == 0)
                return snprintf(buf, size, "Server ip set to %s\n", ip);
        } else if (!logged_in && 
          strncmp(path, CSTR_LEN("/.command/register/")) == 0) {
            const char *username = path + sizeof("/.command/register/") - 1;
            
 
            char url[URL_MAX];
            snprintf(url, sizeof(url),
              "http://%s/register?user=%s", get_server_ip(), username);
              
            string_buf_t resp = {0};
            if (http_request(url, &resp, NULL) == 0) {
                int id;
                char name[32];
                if (sscanf(resp.ptr, "%d:%32s", &id, name) == 2) {
                    current_user_id = id;
                    // making sure '\0' doesn't get overwritten
                    memcpy(current_username, name, sizeof(name)-1);
                    logged_in = 1;
                    free(resp.ptr);
                    LOGMSG("Registered/logged in now! :D");
                    return snprintf(buf, size, "Registered and Logged in as \"%s\".\n", name);
                }
                free(resp.ptr);
                return snprintf(buf, size, "Failed to login. (No http response).\n");
            } else {
                free(resp.ptr);
                return snprintf(buf, size, "Failed to login.\n");
            }
        }
        else if (strncmp(path, CSTR_LEN("/.command/ping/")) == 0) {
            const char *username = path + sizeof("/.command/ping/") - 1;
 
            char url[URL_MAX];
            snprintf(url, sizeof(url),
                "http://%s/login?user=%s", get_server_ip(), username);

            string_buf_t resp = {0};
            if (http_request(url, &resp, NULL) == 0) {
                int id;
                char name[32];
                if (sscanf(resp.ptr, "%d:%32s", &id, name) == 2) {
                    current_user_id = id;
                    // making sure '\0' doesn't get overwritten
                    memcpy(current_username, name, sizeof(name)-1);
                    logged_in = 1;
                    free(resp.ptr);
                    LOGMSG("logged in now! :D");
                    return snprintf(buf, size, "Logged in as \"%s\".\n", name);
                }
            }
            free(resp.ptr);
            return snprintf(buf, size, "Failed to login as \"%s\".\n", username);
        }

        if (logged_in && strncmp(path, CSTR_LEN("/.command/pong")) == 0) {
            current_user_id = 0;
            logged_in = 0;
            return snprintf(buf, size, "Successfully logged out.\n");
        }

        if (strncmp(path, CSTR_LEN("/.command/doggo")) == 0) {
            char url[512];
            snprintf(url, sizeof(url), "http://%s/dog_gif", get_server_ip());
            http_request(url, NULL, NULL);
            return snprintf(buf, size, "Doggo gif sent to notification channel!\n");
        }

        return snprintf(buf, size, "Unknown command, 'ls .command' for HELP.\n");
    }


    if (!logged_in)
        return snprintf(buf, size, LOGIN_TEXT);


    // do_open should've stored fd in fi->fh
    fh_t *fh = (fh_t*)(uintptr_t)fi->fh;
    if (!fh)
        return -EBADF;
    int fd = fh->fd;
    ssize_t written = pread(fd, buf, size, offset);
    if (written < 0)
        return -errno;
    return (int)written;
}


static int do_mkdir(const char *path, mode_t mode)
{
    LOGMSG("IN mkdir");
    if (!logged_in || path[1] == '.')
        return -EACCES;

    char *esc = url_encode(path);
    if (!esc)
        return -EIO;

    char url[URL_MAX];
    snprintf(url, sizeof(url), "http://%s/mkdir?user_id=%d&path=%s",
            get_server_ip(), current_user_id, esc);
    curl_free(esc);

    CURL *c = curl_easy_init();
    if (!c)
        return -EIO;

    uint32_t status = 0;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, "");

    CURLcode rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);


    if (rc != CURLE_OK)
        return -EIO;
    if (status == 201)
        return 0;
    if (status == 404)
        return -ENOENT;

    return -EIO;
}


static int do_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
    // Using %jd and casting to intmax_t uses the largest safe integer
    LOGMSG("IN TRUNCATE path=%s, &size=%jd", path, (intmax_t)size);
    if (!logged_in)
        return -EACCES;

    
    uint32_t status = 0;
    uint32_t* status_ptr = (uint32_t*)((uintptr_t)&status | 1);

    char *esc = url_encode(path);
    if (!esc)
        return -EIO;

    char url[URL_MAX];
    snprintf(url, sizeof(url),
            "http://%s/truncate?user_id=%d&path=%s&size=%jd",
            get_server_ip(), current_user_id, esc, (intmax_t)size);
    curl_free(esc);

    if (http_request(url, NULL, status_ptr) != 0)
        return -ECOMM;

    LOGMSG("TRUNCATE STATUS: %d", status);
    if(status == 400)
        return -EEXIST;
    if (status != 201)
        return -EIO;
    
    /* Truncate local cache file */
    char cache_path[PATH_MAX];
    BUILD_CACHE_PATH(cache_path, current_user_id, path);

    if (truncate(cache_path, size) < 0)
        return -errno;


    /* Remove old cache here, do_release uploads new cache */
    struct stat st;
    off_t file_size = 0;
    if (stat(cache_path, &st) == 0)
        file_size = st.st_size;
    cache_record_delete(path, current_user_id, file_size);

    fh_t *fh = malloc(sizeof(fh_t));
    int fd = open(cache_path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        free(fh);
        return -errno;
    }
    fh->fd = fd;
    fh->dirty = 1;
    fi->fh = (uint64_t)(uintptr_t)fh;

    return 0;
}




static int do_open(const char *path, struct fuse_file_info *fi)
{
    LOGMSG("IN open with path: %s", path);
    if (IS_COMMAND_PATH(path))
        return 0;

    if (!logged_in)
        return -EACCES;
    
    // Guard against directories, though unlikely
    if (fi->flags & O_DIRECTORY)
        return -EISDIR;
    

    char cache_path[PATH_MAX];
    BUILD_CACHE_PATH(cache_path, current_user_id, path);

    fh_t *fh = malloc(sizeof(fh_t));
    if (!fh) {
        return -ENOMEM;
    }

    if (fi->flags & O_TRUNC) {
        LOGMSG("O_TRUNC detected, truncating %s", path);
        int rc = do_truncate(path, 0, fi);
        if (rc != 0) {
            free(fh);
            return rc;
        }
        return 0;
    }

    time_t remote_mtime = 0;
    struct stat st;
    /* Check if cache exists && mtime == mtime on the server's side */
    if (access(cache_path, F_OK) == 0 &&
        stat(cache_path, &st) == 0) {
        remote_mtime = fetch_mtime(path, current_user_id);

        if (st.st_mtime == remote_mtime) {
            int flags = (fi->flags & O_ACCMODE) == O_RDONLY ? O_RDONLY : O_RDWR;
            if (fi->flags & O_APPEND)
                flags |= O_APPEND;
            int fd = open(cache_path, flags);
            if (fd < 0) {
                free(fh);
                return -errno;
            }

            fh->fd = fd;
            fh->dirty = 0;
            fi->fh = (uint64_t)(uintptr_t)fh;
            return 0;
        }
        LOGMSG("cache miss! (diff mtime), hitting '/download' route...");
        LOGMSG("\"%ld\" and \"%ld\"", (long)st.st_mtime, (long)remote_mtime);
    } else {
        LOGMSG("cache miss! (doesn't exist), hitting '/download' route...");
    }
    
    /* Download from server otherwise */
    char *dup = strdup(cache_path);
    char *dir = dirname(dup);
    mkdir_p(dir);
    free(dup);

    char *esc = url_encode(path);
    if (!esc) {
        free(fh);
        return -EIO;
    }

    char url[URL_MAX];
    snprintf(url, sizeof(url),
            "http://%s/download?user_id=%d&path=%s",
            get_server_ip(), current_user_id, esc);
    curl_free(esc);

    FILE *fp = fopen(cache_path, "wb");
    if (!fp) {
        free(fh);
        return -errno;
    }

    int rc = http_get_stream(url, fp);
    fclose(fp);
    if (rc != 0) {
        free(fh);
        unlink(cache_path);
         if (rc == 408)
            return -ETIMEDOUT;  // Upload stalled
        if (rc == 410)
            return -ECANCELED;  // Upload probably cancelled
        if (rc == 520)
            return -ENOENT;     // File not found
        return -ECOMM;
    }

    // set mtime to db mtime
    struct timespec times[2];
    times[0].tv_sec = 0;
    times[0].tv_nsec = UTIME_OMIT;
    times[1].tv_sec = remote_mtime;
    times[1].tv_nsec = 0;
    utimensat(AT_FDCWD, cache_path, times, 0);

    /* stash fh_t in fi->fh */
    int flags = (fi->flags & O_ACCMODE) == O_RDONLY ? O_RDONLY : O_RDWR;
    if (fi->flags & O_APPEND)
        flags |= O_APPEND;
    int fd = open(cache_path, flags);
    if (fd < 0) {
        free(fh);
        unlink(cache_path);
        return -errno;
    }
    fh->fd = fd;
    fh->dirty = 0;
    fi->fh = (uint64_t)(uintptr_t)fh;
    return 0;
}


static int do_release(const char *path, struct fuse_file_info *fi)
{
    LOGMSG("IN release with path: %s", path);
    /* Ignore commands or not logged in */
    if (IS_COMMAND_PATH(path) || !logged_in)
        return 0;


    fh_t *fh = (fh_t*)(uintptr_t)fi->fh;
    if (!fh)
        return -EBADF;
    int fd = fh->fd;
    if (fd) {
        fsync(fd);
        close(fd);
    }

    /* skip if file's clean */
    if (!fh->dirty) {
        free(fh);
        return 0;
    }

    char cache_path[PATH_MAX];
    BUILD_CACHE_PATH(cache_path, current_user_id, path);

    /* Reconcile cache from history */
    struct stat st;
    off_t file_size = 0;
    if (stat(cache_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        free(fh);
        return 0;
    }
    file_size = st.st_size;

    int returner = 0;
    if (!is_temp_path(path)) {
        returner = upload_file_chunks(path, current_user_id, file_size, cache_path, st.st_mtim.tv_sec);
        if (returner != 0) {
            free(fh);
            return returner;
        }
    }
    
    /* Update cache records */
    cache_record_delete(path, current_user_id, -1);
    cache_record_append(path, file_size, current_user_id);
    cache_garbage_collection(current_user_id);

    LOGMSG("File(%s) is dirty! leaving release (%d)", path, returner);
    free(fh);
    return returner;
}

/* Creates a temporary file (empty) in cache folder, logging onto server is handled on release */
static int do_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    LOGMSG("IN CREATE path=%s mode=0%o fi->flags=0x%lx", path, mode, (unsigned long)fi->flags);
    if (!logged_in || IS_COMMAND_PATH(path))
        return -EACCES;
    
    
    char cache_path[PATH_MAX];
    BUILD_CACHE_PATH(cache_path, current_user_id, path);

    char *tmp = strdup(cache_path);
    mkdir_p(dirname(tmp));
    free(tmp);

    /* if temp_path, just create the file without hitting up the server */
    if(is_temp_path(path)) {
        int fd = open(cache_path, O_RDWR | O_CREAT | O_TRUNC, mode & 0777);
        if (fd < 0)
            return -errno;
        fh_t *fh = malloc(sizeof(fh_t));
        if (!fh) {
            close(fd);
            return -ENOMEM;
        }
        fh->fd = fd;
        fh->dirty = 0;
        fi->fh = (uint64_t)(uintptr_t)fh;
        return 0;
    }
    

    uint32_t status = 0;
    uint32_t* status_ptr = (uint32_t*)((uintptr_t)&status | 1);

    char *esc = url_encode(path);
    if (!esc)
        return -EIO;
    
    char url[URL_MAX];
    snprintf(url, sizeof(url),
            "http://%s/create?user_id=%d&path=%s",
            get_server_ip(), current_user_id, esc);
    curl_free(esc);

    if (http_request(url, NULL, status_ptr) != 0)
        return -ECOMM;
    LOGMSG("CREATE STATUS: %d", status);
    if(status == 400)
        return -EEXIST;
    if (status != 201)
        return -EIO;

    cache_record_append(path, 0, current_user_id);


    int fd = open(cache_path, O_RDWR | O_CREAT | O_TRUNC, mode & 0777);
    if (fd < 0)
        return -errno;

    fh_t *fh = malloc(sizeof(fh_t));
    if (!fh) {
        close(fd);
        return -ENOMEM;
    }
    fh->fd = fd;
    fh->dirty = 0;
    fi->fh = (uint64_t)(uintptr_t)fh;

    LOGMSG("leaving create");
    return 0;
}


static int do_write(const char *path, const char *buf,
                    size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    LOGMSG("IN write");
    if (!logged_in || IS_COMMAND_PATH(path))
        return -EACCES;
    fh_t *fh = (fh_t*)(uintptr_t)fi->fh;
    if (!fh)
        return -EBADF;
    int fd = fh->fd;
    

    ssize_t written = pwrite(fd, buf, size, offset);
    if (written > 0)
        fh->dirty = 1;

    return (written < 0) ? -errno : (int)written ;
}


static int do_unlink(const char *path)
{
    if (!logged_in)
        return -EACCES;
    
    uint32_t status = 0;
    uint32_t* status_ptr = (uint32_t*)((uintptr_t)&status | 1);
    char *esc = url_encode(path);
    if (!esc)
        return -EIO;

    char url[URL_MAX];
    snprintf(url, sizeof(url),
            "http://%s/unlink?user_id=%d&path=%s",
            get_server_ip(), current_user_id, esc);
    curl_free(esc);

    if (http_request(url, NULL, status_ptr) != 0)
        return -ECOMM;
    
    if (status != 201) {
        LOGMSG("unlink error: returned %d for path \"%s\"", status, path);
        return -ENOENT;
    }

    char cache_path[PATH_MAX];
    BUILD_CACHE_PATH(cache_path, current_user_id, path);

    /* Remove from cache history */
    struct stat st;
    int size = 0 ;
    if (stat(cache_path, &st) == 0)
        size = st.st_size;
    if (cache_record_delete(path, current_user_id, size) != 0)
        // File might not be cached locally
        // return -EEXIST;
    if (unlink(cache_path) != 0)
        return -errno;

    return 0;
}


static int do_rmdir(const char *path)
{
    if (!logged_in)
        return -EACCES;
    if (strcmp(path, "/") == 0)
        return -EPERM;
    
    uint32_t status = 0;
    uint32_t* status_ptr = (uint32_t*)((uintptr_t)&status | 1);

    char *esc = url_encode(path);
    if (!esc)
        return -EIO;

    char url[URL_MAX];
    snprintf(url, sizeof(url),
            "http://%s/rmdir?user_id=%d&path=%s",
            get_server_ip(), current_user_id, esc);
    curl_free(esc);

    if (http_request(url, NULL, status_ptr) != 0)
        return -ECOMM;
    
    if (status == 404) {
        LOGMSG("rmdir not empty for path \"%s\"", path);
        return -ENOTEMPTY;
    }
    
    if (status != 201) {
        LOGMSG("rmdir error: returned %d for path \"%s\"", status, path);
        return -ENOENT;
    }

    char cache_path[PATH_MAX];
    BUILD_CACHE_PATH(cache_path, current_user_id, path);
    
    cache_remove_subtree(path, current_user_id, 
        CACHE_RM_FILESYSTEM | CACHE_RM_CACHELOGS | CACHE_RM_IGNORE_ENOENT);

    return 0;
}

/* Suprisingly complicated, outlined into 4 cases:
 *   1) exchange flag set -> exchange files "from_path", "to_path"
 *   1) exchange flag set -> exchange files "from_path", "to_path"
 *   2) replace "to_path" with "from_path" (same parent dir)
 *   3) replace "to_path" with "from_path" (diff parent dir)
 *   4) replacing file is TEMP, handle uploading to back-end
 * NOREPLACE only checks back-end for collision, as locally shouldn't have
 * files present if not in back-end.
 */
static int do_rename(const char *from_path,
                     const char *to_path,
                     unsigned int flags)
{
    LOGMSG("IN rename %s -> %s flags=0x%x", from_path, to_path, flags);

    if (!logged_in)
        return -EACCES;
    if (IS_COMMAND_PATH(from_path) || IS_COMMAND_PATH(to_path))
        return -EACCES;

    const char *home_path = getenv("HOME");
    if (!home_path)
        return -ENOENT;

    char oldc[PATH_MAX], newc[PATH_MAX];
    BUILD_CACHE_PATH(oldc, current_user_id, from_path);
    BUILD_CACHE_PATH(newc, current_user_id, to_path);

    char *dup = strdup(newc);
    if (!dup)
        return -ENOMEM;


    mkdir_p(dirname(dup));
    free(dup);

    const int from_is_temp = is_temp_path(from_path);

    /* Wouldn't make sense if files locally and back-end didn't match */
    int source_exists = 0;
    if (backend_exists(current_user_id, from_path, &source_exists) != 0)
        return -ECOMM;
    if (!source_exists)
        return -EIO;

    int dest_exists = 0;
    if (backend_exists(current_user_id, to_path, &dest_exists) != 0)
        return -ECOMM;


    /* swap two files */
    if (flags & RENAME_EXCHANGE) {
        if (!dest_exists || !source_exists)
            return -EEXIST;
        if (from_is_temp || is_temp_path(to_path))
            return -ENOENT;

        char *a = url_encode(from_path), *b = url_encode(to_path);
        if (!a || !b) {
            curl_free(a);
            curl_free(b);
            return -ENOMEM;
        }

        char url[URL_MAX];
        snprintf(url, sizeof(url),
                 "http://%s/swap?user_id=%d&a=%s&b=%s",
                 get_server_ip(), current_user_id, a, b);
        curl_free(a);
        curl_free(b);

        uint32_t status = 0;
        int rc = http_post_status(url, &status);
        if (rc)
            return rc;
        if (status == 520)
            return -ENOENT;
        if (status != 201)
            return -EIO;
        
        struct stat st;
        off_t from_size = 0, to_size = 0;

        if (stat(oldc, &st) == 0)
            from_size = st.st_size;
        if (stat(newc, &st) == 0)
            to_size = st.st_size;


        if ((rc = cache_swap(oldc, newc)) != 0)
            return rc;
        cache_record_rename(from_path, to_path, from_size);
        cache_record_rename(to_path, from_path, to_size);

        return 0;
    }

    /* Replace logic below! Note: rename() replaces automatically */

    /* exit if RENAME_NOREPLACE is set and destination exists */
    if ((flags & RENAME_NOREPLACE) && dest_exists)
        return -EEXIST;

    struct stat st;
    off_t source_size = 0;
    if (stat(oldc, &st) == 0)
        source_size = st.st_size;


    /* Remove file in backend & cache records if exists */ 
    if (dest_exists) {
        int rc = backend_unlink(current_user_id, to_path);
        if (rc)
            return rc;

        struct stat st;
        off_t delete_size = 0;
        if (stat(newc, &st) == 0)
            delete_size = st.st_size;

        cache_record_delete(to_path, current_user_id, delete_size);

        dest_exists = 0;  // not used currently, but kept anyways   
    }

    /* Same parent dir -> simple rename
     * else -> move file there
     */
    const int same_parent = same_parent_dir(from_path, to_path);

    char url[URL_MAX];
    char *a = url_encode(from_path), *b = url_encode(to_path);
    if (!a || !b) {
        curl_free(a);
        curl_free(b);
        return -EIO;
    }

    /* branching on same_parent saves on redundant ops */
    snprintf(url, sizeof(url),
                "http://%s/%s?user_id=%d&a=%s&b=%s",
                get_server_ip(),
                same_parent ? "rename" : "rename_move",
                current_user_id, a, b);

    curl_free(a);
    curl_free(b);


    uint32_t status = 0;
    int rc = http_post_status(url, &status);
    if (rc) return rc;
    if (status == 409)
        return -EEXIST;
    if (status == 520)
        return -ENOENT;
    if (status != 201)
        return -EIO;


    /* local cache rename ↓ */
    if (rename(oldc, newc) != 0 && errno != ENOENT)
        LOGMSG("cache rename %s -> %s: %m", oldc, newc);  // log on failure
    
    
    /* cache updates:
       - TEMP source: it never had a record -> append new record at to_path
       - NON-TEMP: rename existing record from from_path -> to_path
    */
    if (from_is_temp) {
        FILE *fp = fopen(from_path, "rb");
        if (!fp)
            return -EIO;

        struct stat st;
        if (fstat(fileno(fp), &st) != 0) {
            fclose(fp);
            return -EIO;
        }

        rc = upload_file_chunks(to_path, current_user_id, source_size, newc, st.st_mtim.tv_sec);
        if (rc)
            return rc;

        // only touch cache if it's a file
        if (stat(newc, &st) == 0 && S_ISREG(st.st_mode))
        cache_record_append(to_path, source_size, current_user_id);
        cache_garbage_collection(current_user_id);
    } else {
        cache_record_rename(from_path, to_path, source_size);
    }

    return 0;
}


static int do_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    LOGMSG("IN utimens");
    if (!logged_in || IS_COMMAND_PATH(path))
        return -EACCES;
    
    char cache_path[PATH_MAX];
    BUILD_CACHE_PATH(cache_path, current_user_id, path);
    (void)utimensat(AT_FDCWD, cache_path, tv, 0);

    // propagate mtime to backend too
    int push_backend = 1;
    time_t mtime_sec = 0;

    if (!tv || tv[1].tv_nsec == UTIME_NOW) {
        // NULL = use now for both
        mtime_sec = time(NULL);
    } else if(tv[1].tv_nsec == UTIME_OMIT) {
        push_backend = 0;
    } else {
        mtime_sec = tv[1].tv_sec;
    }

    if (push_backend) {
        char *esc = url_encode(path);
        if (!esc)
            return -EIO;

        char url[URL_MAX];
        snprintf(url, sizeof(url),
                 "http://%s/modi_mtime?user_id=%d&path=%s&mtime=%jd",
                 get_server_ip(), current_user_id, esc, (intmax_t)mtime_sec);
        curl_free(esc);

        uint32_t status = 0;
        int rc = http_post_status(url, &status);
        if (rc)
            return rc;
        if (status != 200 && status != 201)
            return -EIO;
    }
    return 0;
}

void *do_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    LOGMSG("STARTING do_init");
    if(cache_init() != 0) {
        fprintf(stderr, "Cache failed to initialized.\n");
        abort();
    }
    return NULL;
}

void do_destroy(void *private_data) {
    cache_exit();
}

static struct fuse_operations ops = {
    .init = do_init,
    .destroy = do_destroy,
    .getattr = do_getattr,
    .readdir = do_readdir,
    .read = do_read,
    .mkdir = do_mkdir,
    .open = do_open,
    .release = do_release,
    .create = do_create,
    .write = do_write,
    .truncate = do_truncate,
    .unlink = do_unlink,
    .rmdir = do_rmdir,
    .rename = do_rename,
    .utimens = do_utimens,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &ops, NULL);
}
