#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <ext2fs/ext2fs.h>
#include <solution.h>


static int copy_direct_block(int img, unsigned block_size, unsigned block_nr, char *buf,  int out,
                             const unsigned file_size, unsigned *size_copied){
    unsigned size;
    if ((file_size - *size_copied) > block_size){
        size = block_size;
    }
    else{
        size = file_size - *size_copied;
    }
    if (block_nr == 0){
        memset(buf, 0, size);

    }
    else {
        off_t offset = block_nr * block_size;
        int res = pread(img, buf, size, offset);
        if (res < 0) {
            return -errno;
        }
    }
    int res = pwrite(out, buf, size, *size_copied);
    if (res < 0) {
        return -errno;
    }
    *size_copied += size;
    return 0;
}


static int copy_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned *indir_buf,  int out,
                               const unsigned file_size, unsigned *size_copied){
    char *buf = calloc(1, block_size);
    off_t offset = block_nr * block_size;
    int res = pread(img, indir_buf, block_size, offset);
    if (res < (int)block_size){
        return -errno;
    }
    for (unsigned i = 0; i < (unsigned)block_size / sizeof(unsigned); ++i){
        if(indir_buf[i] == 0){
            break;
        }
        int copy_result = copy_direct_block(img, block_size, indir_buf[i], buf, out, file_size, size_copied);
        if(copy_result < 0){
            free(buf);
            return copy_result;
        }
    }
    free(buf);
    return 0;
}


static int copy_double_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned *double_indir_buf,
                                      int out, const unsigned file_size, unsigned *size_copied){
    unsigned *indir_buf = calloc(1, block_size);
    off_t offset = block_nr * block_size;
    int res = pread(img, double_indir_buf, block_size, offset);
    if (res < (int)block_size){
        return -errno;
    }
    for (unsigned i = 0; i < (unsigned)block_size / sizeof(unsigned); ++i){
        int copy_result = copy_indirect_block(img, block_size, double_indir_buf[i], indir_buf, out, file_size, size_copied);
        if(copy_result < 0){
            free(indir_buf);
            return copy_result;
        }
    }
    free(indir_buf);
    return 0;
}

static int copy_inode(int img, unsigned block_size, int out, off_t inode_offset){
    struct ext2_inode inode = {0};
    ssize_t read_result = pread(img, &inode, sizeof(inode), inode_offset);
    if (read_result < 0){
        return -errno;
    }

    char *buf;
    unsigned *indir_buf;
    unsigned *double_indir_buf;
    int copy_result = 0;

    const unsigned file_size = inode.i_size;
    unsigned size_copied = 0;


    for (int i = 0; i < EXT2_N_BLOCKS; ++i){
        if (size_copied == file_size){
            break;
        }

        if(i < EXT2_NDIR_BLOCKS){
            buf = calloc(1, block_size);
            copy_result = copy_direct_block(img, block_size, inode.i_block[i], buf, out, file_size, &size_copied);
            free(buf);
            if (copy_result < 0){
                return copy_result;
            }

        }
        else if (i == EXT2_IND_BLOCK){
            indir_buf = calloc(1, block_size);
            copy_result = copy_indirect_block(img, block_size, inode.i_block[i], indir_buf, out, file_size, &size_copied);
            free(indir_buf);
            if (copy_result < 0){
                return copy_result;
            }
        }
        else if (i == EXT2_DIND_BLOCK){
            double_indir_buf = calloc(1, block_size);
            copy_result = copy_double_indirect_block(img, block_size, inode.i_block[i], double_indir_buf, out, file_size, &size_copied);
            free(double_indir_buf);
            if (copy_result < 0){
                return copy_result;
            }
        }
        else{
            return 0;
        }
    }
    return 0;
}


int dump_file(int img, int inode_nr, int out)
{
    struct ext2_super_block super_block = {0};
    ssize_t read_result = pread(img, &super_block, sizeof(super_block), SUPERBLOCK_OFFSET);
    if (read_result < 0){
        return -errno;
    }

    unsigned block_group_nr = (inode_nr - 1) / super_block.s_inodes_per_group;
    unsigned block_size = 1024 << super_block.s_log_block_size;
    struct ext2_group_desc group_desc = {0};
    off_t group_offset = block_size * (super_block.s_first_data_block + 1) + block_group_nr * sizeof(group_desc);

    read_result = pread(img, &group_desc, sizeof(group_desc), group_offset);
    if (read_result < 0){
        return -errno;
    }

    unsigned index_in_group = (inode_nr - 1) % super_block.s_inodes_per_group;
    off_t inode_offset = block_size * group_desc.bg_inode_table + index_in_group * super_block.s_inode_size;

    return copy_inode(img, block_size, out, inode_offset);
}
