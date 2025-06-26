#define FUSE_USE_VERSION 314
#include <fuse3/fuse.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <cjson/cJSON.h>
#include "fuse_utils.h"

#define SERVER_IP "127.0.0.1:5050"
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

    if (strncmp(path, "/.", 2) == 0) {
        if (strcmp(path, "/.doggo") == 0 ||
            strncmp(path, CSTR_LEN("/.ping/")) == 0 ||
            strcmp(path, "/.pong") == 0) {

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
    
    char url[URL_MAX];
    snprintf(url, sizeof(url),
        "http://" SERVER_IP "/stat?user_id=%d&path=%s",
        current_user_id, path);
    
    string_buf_t resp;
    uint32_t status;
    int rc = http_get(url, &resp, &status);

    if (status == 520) {
        free(resp.ptr);
        return -ENOENT;
    }

    if (rc != 0)
        return -ECOMM;
    

    if (status != 200) {
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

    if (!logged_in && strcmp(path,"/") == 0) {
        filler(buf,"You're not logged o.o",NULL,0,0);
        return 0;
    } 

    // COMMANDS
    if (strncmp(path, "/.", 2) == 0) {
        filler(buf, "COMMANDS: (prefix=.)", NULL, 0, 0);
        filler(buf, "ping (login)", NULL, 0, 0);
        filler(buf, "pong(logout)", NULL, 0, 0);
        filler(buf, "doggo(dog gif)", NULL, 0, 0);
        return 0;
    }

    if (strcmp(path, "/.ping") == 0) {
        filler(buf, "User list:", NULL, 0, 0);
        filler(buf, "- William", NULL, 0, 0);
        return 0;
    }


    if (logged_in) {
        char url[URL_MAX];
        snprintf(url,sizeof(url),
                    "http://"SERVER_IP"/listdir?user_id=%d&path=%s",
                     current_user_id, path);
        string_buf_t resp;
        if (http_get(url, &resp, NULL) == 0) {
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

    return -ENOENT;
}

#define LOGIN_TEXT "NOT LOGGED IN! Type '$ cat .ping/{username}' to login.\n"
static int do_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi)
{
    // handle commands
    if (path[1] == '.') {
        if (!logged_in && 
          strncmp(path, CSTR_LEN("/.ping/")) == 0) {
            const char *username = path + sizeof("/.ping/") - 1;
            char url[URL_MAX];
            snprintf(url, sizeof(url),
              "http://" SERVER_IP "/login?user=%s", username);

              
            string_buf_t resp;
            if (http_get(url, &resp, NULL) == 0) {
                int id;
                char name[32];
                if (sscanf(resp.ptr, "%d:%32s", &id, name) == 2) {
                    current_user_id = id;
                    // making sure '\0' doesn't get overwritten
                    memcpy(current_username, name, sizeof(name)-1);
                    logged_in = 1;
                    free(resp.ptr);
                    return snprintf(buf, size, "Logged in as \"%s\"\n", name);
                }
            } else {
                return snprintf(buf, size, "Failed to login.\n");
            }
        }

        if (logged_in && strncmp(path, CSTR_LEN("/.pong")) == 0) {
            current_user_id = 0;
            logged_in = 0;
            return snprintf(buf, size, "Successfully logged out.\n");
        }

        if (strncmp(path, CSTR_LEN("/.doggo")) == 0) {
            http_get("http://" SERVER_IP "/dog_gif", NULL, NULL);
            return snprintf(buf, size, "Doggo gif sent to notification channel!\n");
        }

        return snprintf(buf, size, "Unknown command, 'ls .' for HELP.\n");
    }


    if (!logged_in) {
        return snprintf(buf, size, LOGIN_TEXT);
    }

    // do_open and stored fd in fi->fh
    ssize_t n = pread((int)fi->fh, buf, size, offset);
    if (n < 0)
        return -errno;
    return (int)n;
}


static int do_mkdir(const char *path, mode_t mode)
{
    if (!logged_in)
        return -EACCES;
    char url[URL_MAX];
    snprintf(url, sizeof(url), "http://" SERVER_IP "/mkdir?user_id=%d&path=%s",
                current_user_id, path);

    CURL *c = curl_easy_init();
    if (!c)
        return -EIO;
    
    uint32_t status = 0;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POST, 1L);         // make it POST
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, "");   // empty body
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 0L);    // inspect status

    CURLcode rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);


    if (rc != CURLE_OK)
        return -EIO;
    if (status == 201)
        return 0;
    if (status == 404)
        return -ENOENT;
    if (status == 403)
        return -EACCES;
    return -EIO;
}



static int do_open(const char *path, struct fuse_file_info *fi)
{
    if (!logged_in && strncmp(path, CSTR_LEN("/.ping/")) == 0)
        return 0;

    if (!logged_in)
        return -EACCES;

    /* cache stored at -> HOME/.cache/disfs/{user_id}/{local_path}  */
    char cache_path[PATH_MAX];
    snprintf(cache_path, sizeof(cache_path),
             "%s/.cache/disfs%s", getenv("HOME"), path);

    /* remove stale cache (If it's even there) */
    unlink(cache_path);

    char *dup = strdup(cache_path);
    char *dir = dirname(dup);
    mkdir_p(dir);
    free(dup);

    char url[URL_MAX];
    snprintf(url, sizeof(url),
             "http://" SERVER_IP "/download?user_id=%d&path=%s",
             current_user_id, path);

    FILE *fp = fopen(cache_path, "wb");
    if (!fp)
        return -errno;

    if (http_get_stream(url, fp) != 0) {
        fclose(fp);
        unlink(cache_path);
        return -ECOMM;
    }
    fclose(fp);

    /* stash fd in fi->fh */
    int fd = open(cache_path, O_RDONLY);
    if (fd < 0) {
        unlink(cache_path);
        return -errno;
    }
    fi->fh = fd;
    return 0;
}


static int do_release(const char *path, struct fuse_file_info *fi)
{
    close((int)fi->fh);

    char cache_path[PATH_MAX];
    snprintf(cache_path, sizeof(cache_path),
             "%s/.cache/disfs%s", getenv("HOME"), path);

    FILE *fp = fopen(cache_path, "rb");
    if (!fp)
        return -errno;
    
    void *chunk_buf = malloc(CHUNK_SIZE);
    if (!chunk_buf){
        fclose(fp);
        return -ENOMEM;
    }


    int returner = 0;
    int chunk = 0;
    size_t n;
    while ((n = fread(chunk_buf, 1, CHUNK_SIZE, fp)) > 0) {
        char url[URL_MAX];
        snprintf(url, sizeof(url),
                "http://" SERVER_IP "/upload?user_id=%d&path=%s&chunk=%d",
                current_user_id, path, chunk);

        uint32_t status;
        if (http_post_stream(url, chunk_buf, n, &status) != 0)
            returner = -ECOMM;
        
        if (status != 200)
            returner = -EIO;

        chunk++;
    }
    free(chunk_buf);
    fclose(fp);

    if(returner == 0)
        if (unlink(cache_path) != 0)
            returner = -errno;

    return returner;
}

static int do_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    if (!logged_in)
        return -EACCES;
    

    uint32_t status;
    uint32_t* status_ptr = (uint32_t*)((uintptr_t)&status | 1);

    
    char url[URL_MAX];
    snprintf(url, sizeof(url),
             "http://" SERVER_IP "/create?user_id=%d&path=%s",
             current_user_id, path);


    if (http_get(url, NULL, status_ptr) != 0)
        return -ECOMM;
    
    if (status != 520)
        return -EEXIST;
    

    char cache_path[PATH_MAX];
    snprintf(cache_path, sizeof(cache_path),
             "%s/.cache/disfs%s", getenv("HOME"), path);
    
    char *tmp = strdup(cache_path);
    mkdir_p(dirname(tmp));
    free(tmp);

    int fd = open(cache_path, O_RDWR | O_CREAT | O_TRUNC, mode & 0777);

    if (fd < 0)
        return -errno;
    
    fi->fh = fd;
    return 0;
}





static struct fuse_operations ops = {
    .getattr = do_getattr,
    .readdir = do_readdir,
    .read = do_read,
    .mkdir = do_mkdir,
    .open = do_open,
    .release = do_release,
    .create = do_create
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &ops, NULL);
}
