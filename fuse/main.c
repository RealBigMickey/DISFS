#define FUSE_USE_VERSION 314
#include <sys/types.h>
#include <fuse3/fuse.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
    
    char url[256];
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
        return -EIO;
    

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
        char url[512];
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
            char url[256];
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

    if(!logged_in)
        return snprintf(buf, size, LOGIN_TEXT);
    return -ENOENT;
}


static int do_mkdir(const char *path, mode_t mode)
{
    if (!logged_in)
        return -EACCES;
    char url[256];
    snprintf(url, sizeof(url), "http://" SERVER_IP "/mkdir?user_id=%d&path=%s",
                current_user_id, path);

    CURL *c = curl_easy_init();
    if (!c)
        return -EIO;
    
    uint32_t status = 0;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POST, 1L);         // <-- make it POST
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, "");   // empty body
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 0L);    // we want to inspect status

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




static struct fuse_operations ops = {
    .getattr = do_getattr,
    .readdir = do_readdir,
    .read = do_read,
    .mkdir = do_mkdir
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &ops, NULL);
}
