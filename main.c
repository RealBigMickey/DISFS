#define FUSE_USE_VERSION 314
#include <fuse3/fuse.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#define LOCAL_IP "127.0.0.1:5050"
char TEST_TEXT[] = "Hellow, World!\n";



typedef struct string_buf {
    char *ptr;
    size_t len;
} string_buf_t;

// size = size of each member, nmemb = number of members
size_t write_cb(void *data, size_t size, size_t nmemb, void *userp)
{
    string_buf_t *buf = (string_buf_t*)userp;
    size_t total = size * nmemb;
    buf->ptr = realloc(buf->ptr, buf->len + total + 1);
    memcpy(buf->ptr + buf->len, data, total);
    buf->len += total;
    buf->ptr[buf->len] = '\0';

    return total;
}





static int do_getattr(const char *path, struct stat *st, struct fuse_file_info *fi)
{
    memset(st, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
    } else if (strcmp(path, "/ping") == 0) {
        st->st_mode  = S_IFREG | 0644;
        st->st_nlink = 1;
        st->st_size  = 1024;
    } else 
        return -ENOENT;

    return 0;
}


static int do_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags)
{
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    filler(buf, "ping", NULL, 0, 0);
    return 0;
}

static int do_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi)
{
    if (strcmp(path, "/ping") == 0) {
        CURL *curl = curl_easy_init();
        string_buf_t response = {malloc(1), 0};
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, "http://" LOCAL_IP "/ping");
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            CURLcode res = curl_easy_perform(curl);  // res not used yet
            curl_easy_cleanup(curl);
        }

        size_t len = response.len;
        if (offset >= len) {
            free(response.ptr);
            return 0;
        }
        if (offset + size > len)
            size = len - offset;
        
        memcpy(buf, response.ptr + offset, size);
        free(response.ptr);
        return size;
    }
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
