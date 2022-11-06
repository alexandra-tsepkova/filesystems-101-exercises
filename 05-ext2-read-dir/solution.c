#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <ext2fs/ext2fs.h>
#include <solution.h>

static int read_direct_block(int img, unsigned block_size, unsigned block_nr){
    struct ext2_dir_entry_2 dir_entry;
    off_t beginning_offset = block_size * block_nr;
    off_t current_offset = block_size * block_nr;
    while(current_offset < beginning_offset + block_size){
        int res = pread(img, &dir_entry, sizeof(dir_entry), current_offset);
        if (res < 0){
            return -errno;
        }
        if(dir_entry.inode == 0){
            break;
        }
        char type;
        if (dir_entry.file_type == EXT2_FT_REG_FILE) type = 'f';
        else if (dir_entry.file_type == EXT2_FT_DIR) type = 'd';
        else return -1;

        char *name = {0};
        name = calloc(256, sizeof(char));
        memset(name, '\0', 256 * sizeof(char));
        sprintf(name, "%s", dir_entry.name);

//        printf("inode: %u, type: %c, name: %s, len: %u\n", dir_entry.inode, type, name, dir_entry.rec_len);
        report_file(dir_entry.inode, type, name);
        free(name);
        current_offset += dir_entry.rec_len;
    }
    return 0;
}

static int read_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned *indir_buf){
    off_t offset = block_nr * block_size;
    int res = pread(img, indir_buf, block_size, offset);
    if (res < (int)block_size){
        return -errno;
    }
    for (unsigned i = 0; i < (unsigned)block_size / sizeof(unsigned); ++i){
        if(indir_buf[i] == 0){
            break;
        }
        int read_result = read_direct_block(img, block_size, indir_buf[i]);
        if(read_result < 0){
            return read_result;
        }
    }
    return 0;
}

static int read_double_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned *double_indir_buf){
    unsigned *indir_buf = calloc(1, block_size);
    off_t offset = block_nr * block_size;
    int res = pread(img, double_indir_buf, block_size, offset);
    if (res < (int)block_size){
        return -errno;
    }
    for (unsigned i = 0; i < (unsigned)block_size / sizeof(unsigned); ++i){
        int read_result = read_indirect_block(img, block_size, double_indir_buf[i], indir_buf);
        if(read_result < 0){
            free(indir_buf);
            return read_result;
        }
    }
    free(indir_buf);
    return 0;
}


static int parse_content_of_inode_as_dir(int img, unsigned block_size, off_t inode_offset){
    struct ext2_inode inode = {0};
    ssize_t read_result = pread(img, &inode, sizeof(inode), inode_offset);
    if (read_result < 0){
        return -errno;
    }

    unsigned *indir_buf = {0};
    unsigned *double_indir_buf = {0};

    indir_buf = calloc(1, block_size);
    double_indir_buf = calloc(1, block_size);


    for (int i = 0; i < EXT2_N_BLOCKS; ++i){
        if (inode.i_block[i] == 0){
            break;
        }

        if(i < EXT2_NDIR_BLOCKS){
            read_result = read_direct_block(img, block_size, inode.i_block[i]);
            if (read_result < 0){
                free(indir_buf);
                free(double_indir_buf);
                return read_result;
            }

        }
        else if (i == EXT2_IND_BLOCK){
            read_result = read_indirect_block(img, block_size, inode.i_block[i], indir_buf);
            if (read_result < 0){
                free(indir_buf);
                free(double_indir_buf);
                return read_result;
            }
        }
        else if (i == EXT2_DIND_BLOCK){
            read_result = read_double_indirect_block(img, block_size, inode.i_block[i], double_indir_buf);
            if (read_result < 0){
                free(indir_buf);
                free(double_indir_buf);
                return read_result;
            }
        }
        else{
            free(indir_buf);
            free(double_indir_buf);
            return 0;
        }
    }
    free(indir_buf);
    free(double_indir_buf);
    return 0;
}



    int dump_dir(int img, int inode_nr)
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

    return parse_content_of_inode_as_dir(img, block_size, inode_offset);
}
