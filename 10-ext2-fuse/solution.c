#include <solution.h>
#include <ext2_func.h>

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

int image = 0;
struct ext2_super_block super_block;

int get_inode_struct(int inode_nr, struct ext2_inode *inode){
    off_t offset = 0;
    int res = find_inode_offset(image, super_block, inode_nr, &offset);
    if (res < 0){
        return  res;
    }

    int read_result = pread(image, inode, sizeof(*inode), offset);
    if (read_result < 0){
        return -errno;
    }
    return 0;
}

int get_inode_nr_by_path(const char *path, int *inode_nr){
    int dir_inode = ROOT_DIR_INODE_NR;

    if (strcmp(path, "/") != 0) {
        dir_inode = find_dir_inode_number_by_path(image, path, super_block);
        if (dir_inode < 0) {
            return dir_inode;
        }
    }
    *inode_nr = dir_inode;
    return 0;
}

static void *ext2_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    (void)conn;
    cfg->kernel_cache = 1;
    return NULL;
}

static int ext2_getattr(const char *path, struct stat *st, struct fuse_file_info *fi)
{
    (void)fi;

    int dir_inode = ROOT_DIR_INODE_NR;
    int res = get_inode_nr_by_path(path, &dir_inode);
    if (res < 0){
        return res;
    }


    off_t inode_offset = 0;
    res = find_inode_offset(image, super_block, dir_inode, &inode_offset);
    if (res < 0){
        return res;
    }

    struct ext2_inode inode;
    res = get_inode_struct(dir_inode, &inode);
    if (res < 0){
        return res;
    }

    memset(st, 0, sizeof(struct stat));
    // 000110110110
    st->st_mode = inode.i_mode & 077777555;
    st->st_nlink = inode.i_links_count;
    st->st_size = inode.i_size;
    st->st_atime = inode.i_atime;
    st->st_mtime = inode.i_mtime;
    st->st_ctime = inode.i_ctime;
    st->st_gid = inode.i_gid;
    st->st_uid = inode.i_uid;
    st->st_ino = dir_inode;
    st->st_blocks = inode.i_blocks;
    st->st_blksize = 1024 << super_block.s_log_block_size;


//    st->st_atime = st->st_mtime = st->st_ctime = time(NULL); // set current date
    return 0;
}


static int ext2_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t off,
                           struct fuse_file_info *fi, enum fuse_readdir_flags readdir_flags)
{
    (void)off;
    (void)fi;
    (void)readdir_flags;

    int dir_inode = ROOT_DIR_INODE_NR;
    int res = get_inode_nr_by_path(path, &dir_inode);
    if (res < 0){
        return res;
    }

    off_t inode_offset = 0;
    res = find_inode_offset(image, super_block, dir_inode, &inode_offset);
    if (res < 0){
        return res;
    }

    struct ext2_inode inode;
    res = get_inode_struct(dir_inode, &inode);
    if (res < 0){
        return res;
    }
    if ((inode.i_mode & 0x4000) == 0){
        return -ENOTDIR;
    }

    unsigned block_size = 1024 << super_block.s_log_block_size;
    res = parse_content_of_inode_as_dir(image, block_size, inode_offset, filler, buf);
    if (res < 0){
        return res;
    }

    return 0;
}


static int ext2_open(const char *path, struct fuse_file_info *fi)
{
    int dir_inode = ROOT_DIR_INODE_NR;
    int res = get_inode_nr_by_path(path, &dir_inode);
    if (res < 0){
        return res;
    }

    off_t inode_offset = 0;
    res = find_inode_offset(image, super_block, dir_inode, &inode_offset);
    if (res < 0){
        return res;
    }

    struct ext2_inode inode;
    res = get_inode_struct(dir_inode, &inode);
    if (res < 0){
        return res;
    }
    if ((inode.i_mode & 0x4000) != 0){
        return -EISDIR;
    }

    if ((fi->flags & O_WRONLY) || (fi->flags & O_RDWR)){  // set restriction to readonly
        return -EROFS;
    }
    return 0;
}

static int ext2_read(const char *path, char *buf, size_t size, off_t off, struct fuse_file_info *fi){
    (void) fi;
    int dir_inode = ROOT_DIR_INODE_NR;
    int res = get_inode_nr_by_path(path, &dir_inode);
    if (res < 0){
        return res;
    }

    off_t inode_offset = 0;
    res = find_inode_offset(image, super_block, dir_inode, &inode_offset);
    if (res < 0){
        return res;
    }

    struct ext2_inode inode;
    res = get_inode_struct(dir_inode, &inode);
    if (res < 0){
        return res;
    }
    if ((inode.i_mode & 0x4000) != 0){
        return -EISDIR;
    }

    if ((fi->flags & O_WRONLY) || (fi->flags & O_RDWR)){  // set restriction to readonly
        return -EROFS;
    }

    size_t fuse_buf_offset = 0;
    res = copy_inode(image, 1024 << super_block.s_log_block_size, inode, size, off, buf, &fuse_buf_offset);
    if (res < 0) {
        return res;
    }
    return fuse_buf_offset;
}

//  all operations that change fs must return EROFS
static int ext2_mknod(const char *path, mode_t mode, dev_t rdev){
    (void) path;
    (void) mode;
    (void) rdev;

    return -EROFS;
}
static int ext2_mkdir(const char *path, mode_t mode){
    (void) path;
    (void) mode;

    return -EROFS;
}
static int ext2_unlink(const char *path){
    (void) path;

    return -EROFS;
}
static int ext2_rmdir(const char *path){
    (void) path;

    return -EROFS;
}
static int ext2_rename(const char *from, const char *to, unsigned int flags){
    (void) from;
    (void) to;
    (void) flags;

    return -EROFS;
}
static int ext2_truncate(const char *path, off_t size, struct fuse_file_info *fi){
    (void) path;
    (void) size;
    (void) fi;

    return -EROFS;
}
static int ext2_write(const char *path, const char *buf, size_t size, off_t off, struct fuse_file_info *fi){
    (void) path;
    (void) buf;
    (void) size;
    (void) off;
    (void) fi;

    return -EROFS;
}
static int ext2_create(const char *path, mode_t mode, struct fuse_file_info *fi){
    (void) path;
    (void) mode;
    (void) fi;

    return -EROFS;
}

static int ext2_write_buf(const char *path, struct fuse_bufvec *buf, off_t off, struct fuse_file_info *fi){
    (void)path;
    (void)buf;
    (void)off;
    (void)fi;

    return -EROFS;
}

static int ext2_setxattr (const char *path, const char *name, const char *value, size_t size, int flags){
    (void)path;
    (void)name;
    (void)value;
    (void)size;
    (void)flags;

    return -EROFS;
}

static int ext2_removexattr (const char *path, const char *name){
    (void)path;
    (void)name;

    return -EROFS;
}


static const struct fuse_operations ext2_ops = {
        .init = ext2_init,
        .getattr = ext2_getattr,
        .readdir = ext2_readdir,
        .open = ext2_open,
        .read = ext2_read,
        .mknod = ext2_mknod,
        .mkdir = ext2_mkdir,
        .unlink = ext2_unlink,
        .rmdir = ext2_rmdir,
        .rename = ext2_rename,
        .truncate = ext2_truncate,
        .write = ext2_write,
        .create = ext2_create,
        .write_buf = ext2_write_buf,
        .setxattr = ext2_setxattr,
        .removexattr = ext2_removexattr
};

int ext2fuse(int img, const char *mntp)
{
    image = img;
    ssize_t read_result = pread(image, &super_block, sizeof(super_block), SUPERBLOCK_OFFSET);
    if (read_result < 0){
        return -errno;
    }

//	(void) img;

	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &ext2_ops, NULL);
}
