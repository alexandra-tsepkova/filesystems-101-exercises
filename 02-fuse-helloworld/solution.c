#include <solution.h>

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

static void *hellofs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    (void)conn;
    cfg->kernel_cache = 1;
    cfg->set_uid = 1;
    cfg->uid = getuid();
    cfg->set_gid = 1;
    cfg->gid = getgid();
    return NULL;
}

static int hellofs_getattr(const char *path, struct stat *st, struct fuse_file_info *fi)
{
    (void)fi;
    memset(st, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0775;
        st->st_nlink = 2;
    } else if (strcmp(path, "/hello") == 0) {
        st->st_mode = S_IFREG | 0400;
        st->st_nlink = 1;
    } else {
        return -ENOENT;
    }
    st->st_size = 32;
    st->st_atime = st->st_mtime = st->st_ctime = time(NULL); // set current date
    return 0;
}


static int hellofs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t off,
                           struct fuse_file_info *fi, enum fuse_readdir_flags readdir_flags)
{
    (void)off;
    (void)fi;
    (void)readdir_flags;

    if (strcmp(path, "/") != 0){
        return -ENOTDIR;
    }

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    filler(buf, "hello", NULL, 0, 0);
    return 0;
}


static int hellofs_open(const char *path, struct fuse_file_info *fi)
{
    if (strcmp(path, "/") == 0) {
        return -EISDIR;
    }
    if ((fi->flags & O_WRONLY) || (fi->flags & O_RDWR)){  // set restriction to readonly
        return -EROFS;
    }
    if (strcmp(path, "hello")){
        return 0;
    }
    return -ENOENT;
}

static int hellofs_read(const char *path, char *buf, size_t size, off_t off, struct fuse_file_info *fi){
    (void) fi;
    if(strcmp(path, "/hello") != 0) {
        return -ENOENT;
    }

    struct fuse_context *fuse_context = fuse_get_context();
    pid_t pid_hello = fuse_context->pid;
    char hello_pid[100] = {0};
    snprintf(hello_pid, 100, "hello, %d\n", pid_hello);
    int n = strlen(hello_pid);

    if (off < n){
        if (off + size > (unsigned)n)
            size = n - off;
        memcpy(buf, hello_pid + off, size);
        return size;
    }
    else {
        return 0;
    }
}

//  all operations that change fs must return EROFS
static int hellofs_mknod(const char *path, mode_t mode, dev_t rdev){
    (void) path;
    (void) mode;
    (void) rdev;

    return -EROFS;
}
static int hellofs_mkdir(const char *path, mode_t mode){
    (void) path;
    (void) mode;

    return -EROFS;
}
static int hellofs_unlink(const char *path){
    (void) path;

    return -EROFS;
}
static int hellofs_rmdir(const char *path){
    (void) path;

    return -EROFS;
}
static int hellofs_rename(const char *from, const char *to, unsigned int flags){
    (void) from;
    (void) to;
    (void) flags;

    return -EROFS;
}
static int hellofs_truncate(const char *path, off_t size, struct fuse_file_info *fi){
    (void) path;
    (void) size;
    (void) fi;

    return -EROFS;
}
static int hellofs_write(const char *path, const char *buf, size_t size, off_t off, struct fuse_file_info *fi){
    (void) path;
    (void) buf;
    (void) size;
    (void) off;
    (void) fi;

    return -EROFS;
}
static int hellofs_create(const char *path, mode_t mode, struct fuse_file_info *fi){
    (void) path;
    (void) mode;
    (void) fi;

    return -EROFS;
}

static int hellofs_write_buf(const char *path, struct fuse_bufvec *buf, off_t off, struct fuse_file_info *fi){
    (void)path;
    (void)buf;
    (void)off;
    (void)fi;

    return -EROFS;
}

int hellofs_setxattr (const char *path, const char *name, const char *value, size_t size, int flags){
    (void)path;
    (void)name;
    (void)value;
    (void)size;
    (void)flags;

    return -EROFS;
}

int hellofs_removexattr (const char *path, const char *name){
    (void)path;
    (void)name;

    return -EROFS;
}


static const struct fuse_operations hellofs_ops = {
        .init = hellofs_init,
        .getattr = hellofs_getattr,
        .readdir = hellofs_readdir,
        .open = hellofs_open,
        .read = hellofs_read,
        .mknod = hellofs_mknod,
        .mkdir = hellofs_mkdir,
        .unlink = hellofs_unlink,
        .rmdir = hellofs_rmdir,
        .rename = hellofs_rename,
        .truncate = hellofs_truncate,
        .write = hellofs_write,
        .create = hellofs_create,
        .write_buf = hellofs_write_buf,
        .setxattr = hellofs_setxattr,
        .removexattr = hellofs_removexattr
};

int helloworld(const char *mntp)
{
	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &hellofs_ops, NULL);
}
