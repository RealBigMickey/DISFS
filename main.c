#define FUSE_USE_VERSION 314
#include <fuse3/fuse.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

char TEST_TEXT[] = "Hellow, World!\n";

static int do_getattr(const char *path, struct stat *st, struct fuse_file_info *fi)
{
    memset(st, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
    } else if (strcmp(path, "/hello.txt") == 0) {
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 1;
        st->st_size = strlen(TEST_TEXT);
    } else {
        return -ENOENT;
    }
    return 0;
}

static int do_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags)
{
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    filler(buf, "hello.txt", NULL, 0, 0);
    return 0;
}

static int do_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi)
{
    size_t len = strlen(TEST_TEXT);
    if (offset >= len)
        return 0;
    if (offset + size > len)
        size = len - offset;
    memcpy(buf, TEST_TEXT + offset, size);
    return size;
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
