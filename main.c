#define FUSE_USE_VERSION 314
#include <fuse3/fuse.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


char TEST_TEXT[] = "Hellow, World!\n";
#define CSTR_LEN(s) (s), (sizeof(s) - 1)

typedef struct string_buf {
    char *ptr;
    size_t len;
} string_buf_t;

// size = size of each member, nmemb = number of members
size_t write_cb(void *data, size_t size, size_t nmemb, void *userp)
{
    string_buf_t *buf = (string_buf_t*)userp;
    size_t total = size * nmemb;
    char *tmp = realloc(buf->ptr, buf->len + total + 1);
    if (!tmp)
        return 0;
    buf->ptr = tmp;
    memcpy(buf->ptr + buf->len, data, total);
    buf->len += total;
    buf->ptr[buf->len] = '\0';

    return total;
}


#define SERVER_IP "127.0.0.1:5050"
static int current_user_id;
static char current_username[32];
static int logged_in;


static int http_get(const char *url, string_buf_t *resp) {
    CURL *c = curl_easy_init();
    if (!c)
        return -1;
    resp->ptr = malloc(1); resp->len = 0;

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, resp);

    // fails on 4XX and 5XX
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);

    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    return (rc == CURLE_OK) ? 0: -1;
}


static int do_getattr(const char *path, struct stat *st, struct fuse_file_info *fi)
{
    memset(st, 0, sizeof(struct stat));
    
    if (strcmp(path, "/") == 0 || strcmp(path, "/.ping") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }

    if (strncmp(path, "/.", 2) == 0) {
        // catch all "dot-commands" as virtual files
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 1;
        st->st_size = 128;
        return 0;
    }
    
    if (strncmp(path, CSTR_LEN("/.ping/")) == 0) {
        st->st_mode  = S_IFREG | 0644;
        st->st_nlink = 1;
        st->st_size  = 128;
        return 0;
    }

    return -ENOENT;
}


static int do_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags)
{
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    if (strcmp(path, "/") == 0) {
        filler(buf, ".ping", NULL, 0, 0);
        return 0;
    }
    if (strcmp(path, "/.ping") == 0) {
        filler(buf, ".", NULL, 0, 0);
        filler(buf, "..", NULL, 0, 0);
        filler(buf, "William", NULL, 0, 0);
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
            char url[128];
            snprintf(url, sizeof(url),
              "http://" SERVER_IP "/login?user=%s", username);

              
            string_buf_t resp;
            if (http_get(url, &resp) == 0) {
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

        return -EIO;
    }

    if(!logged_in)
        return snprintf(buf, size, LOGIN_TEXT);
    return -ENOENT;
}

static struct fuse_operations ops = {
    .getattr = do_getattr,
    .readdir = do_readdir,
    .read = do_read,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &ops, NULL);
}
