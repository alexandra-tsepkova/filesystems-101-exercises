#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <ext2fs/ext2fs.h>
#include <solution.h>

#define ROOT_DIR_INODE_NR 2

char *get_entry_name (const char *path, unsigned *remaining_path_len){
    int path_len = strlen(path);
    char path_copy[path_len + 1];
    memcpy(path_copy, path, path_len + 1);
    char *local_entry = strtok(path_copy, "/");
    char *entry = calloc(strlen(local_entry) + 1, sizeof(char));
    strcpy(entry, local_entry);
    *remaining_path_len -= (strlen(entry) + 1);
    return entry;
}

char *get_remaining_path(const char *path){
    char *remaining_path = strchr(path, '/');
    return remaining_path;
}

static int find_inode_offset(int img, struct ext2_super_block super_block, int inode_nr, off_t *inode_offset){
    unsigned block_group_nr = (inode_nr - 1) / super_block.s_inodes_per_group;
    struct ext2_group_desc group_desc = {0};
    unsigned block_size = 1024 << super_block.s_log_block_size;

    off_t group_offset = block_size * (super_block.s_first_data_block + 1) + block_group_nr * sizeof(group_desc);

    int read_result = pread(img, &group_desc, sizeof(group_desc), group_offset);
    if (read_result < 0){
        return -errno;
    }

    unsigned index_in_group = (inode_nr - 1) % super_block.s_inodes_per_group;
    *inode_offset = block_size * group_desc.bg_inode_table + index_in_group * super_block.s_inode_size;
    return 0;
}

static int find_entry_direct_block(int img, unsigned block_size, unsigned block_nr, unsigned ext2_filetype, const char *path, int *file_inode_nr){
    struct ext2_dir_entry_2 dir_entry = {};
    off_t beginning_offset = block_size * block_nr;
    off_t current_offset = block_size * block_nr;
    short found = 0;
    while(current_offset < beginning_offset + block_size){

        int res = pread(img, &dir_entry, sizeof(dir_entry), current_offset);
        if (res < 0){
            return -errno;
        }
        if(dir_entry.inode == 0){
            return -ENOENT;
        }

        if (dir_entry.file_type != ext2_filetype) {
            current_offset += dir_entry.rec_len;
            continue;
        }

        char name[PATH_MAX];
        memset(name, '\0', PATH_MAX * sizeof(char));
        snprintf(name, dir_entry.name_len + 1, "%s", dir_entry.name);

        unsigned remaining_path_len = strlen(path);
        char* entry_name = get_entry_name(path, &remaining_path_len);
        unsigned name_len = strlen(name);

        if (strncmp(name, entry_name, name_len) == 0){
            found = 1;
            *file_inode_nr = dir_entry.inode;
        }
        current_offset += dir_entry.rec_len;
        free(entry_name);
    }
    if (found == 1) return 0;
    else return 1;
}

static int find_entry_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned ext2_filetype, const char *path, int *file_inode_nr, unsigned *indir_buf){
    off_t offset = block_nr * block_size;
    int find_result = 1;
    int res = pread(img, indir_buf, block_size, offset);
    if (res < (int)block_size){
        return -errno;
    }
    for (unsigned i = 0; i < (unsigned)block_size / sizeof(unsigned); ++i){
        if(indir_buf[i] == 0){
            return -ENOENT;
        }
        find_result = find_entry_direct_block(img, block_size, indir_buf[i], ext2_filetype, path, file_inode_nr);
        if(find_result <= 0){
            return find_result;
        }
    }
    return find_result;
}

static int find_entry_double_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned ext2_filetype, const char *path, int *file_inode_nr, unsigned *double_indir_buf){
    unsigned *indir_buf = calloc(1, block_size);
    off_t offset = block_nr * block_size;
    int find_result = 1;
    int res = pread(img, double_indir_buf, block_size, offset);
    if (res < (int)block_size){
        return -errno;
    }
    for (unsigned i = 0; i < (unsigned)block_size / sizeof(unsigned); ++i){
        find_result = find_entry_indirect_block(img, block_size, double_indir_buf[i], ext2_filetype, path, file_inode_nr, indir_buf);
        if(find_result <= 0){
            free(indir_buf);
            return find_result;
        }
    }
    free(indir_buf);
    return find_result;
}


static int find_inode_number_by_path(int img, unsigned block_size, const char *path, int *file_inode_nr, off_t inode_offset, struct ext2_super_block super_block){
    struct ext2_inode inode = {0};
    ssize_t read_result = pread(img, &inode, sizeof(inode), inode_offset);
    if (read_result < 0){
        return -errno;
    }
    unsigned remaining_path_len = strlen(path);
    char* entry_name = get_entry_name(path, &remaining_path_len);
    free(entry_name);

    unsigned entry_type = remaining_path_len == 0 ? EXT2_FT_REG_FILE : EXT2_FT_DIR;

    for (int i = 0; i < EXT2_N_BLOCKS; ++i){
        if (inode.i_block[i] == 0){
            break;
        }

        if(i < EXT2_NDIR_BLOCKS){
            int find_result = find_entry_direct_block(img, block_size, inode.i_block[i], entry_type, path, file_inode_nr);
            if (find_result < 0){
                return find_result;
            }
            else if(find_result == 0){
                if(remaining_path_len != 0) {
                    char *remaining_path = get_remaining_path(path + 1);
                    off_t inode_offset = 0;
                    find_inode_offset(img, super_block, *file_inode_nr, &inode_offset);
                    return find_inode_number_by_path(img, block_size, remaining_path, file_inode_nr, inode_offset,
                                                     super_block);
                }
                break;
            }
        }
        else if (i == EXT2_IND_BLOCK){
            unsigned *indir_buf = calloc(1, block_size);
            int find_result = find_entry_indirect_block(img, block_size, inode.i_block[i], entry_type, path, file_inode_nr, indir_buf);
            free(indir_buf);
            if (find_result < 0){
                return find_result;
            }
            else if(find_result == 0){
                if(remaining_path_len != 0) {
                    char *remaining_path = get_remaining_path(path + 1);
                    off_t inode_offset = 0;
                    find_inode_offset(img, super_block, *file_inode_nr, &inode_offset);
                    return find_inode_number_by_path(img, block_size, remaining_path, file_inode_nr, inode_offset,
                                                     super_block);
                }
                break;
            }
        }
        else if (i == EXT2_DIND_BLOCK){
            unsigned *double_indir_buf = calloc(1, block_size);
            int find_result = find_entry_double_indirect_block(img, block_size, inode.i_block[i], entry_type, path, file_inode_nr, double_indir_buf);
            free(double_indir_buf);
            if (find_result < 0){
                return find_result;
            }
            else if(find_result == 0){
                if(remaining_path_len != 0) {
                    char *remaining_path = get_remaining_path(path + 1);
                    off_t inode_offset = 0;
                    find_inode_offset(img, super_block, *file_inode_nr, &inode_offset);
                    return find_inode_number_by_path(img, block_size, remaining_path, file_inode_nr, inode_offset,
                                                     super_block);
                }
                break;
            }
        }
        else{
            return -ENOENT;
        }
    }
    return 0;
}

static int copy_direct_block(int img, unsigned block_size, unsigned block_nr, char *buf,  int out,
                             const unsigned file_size, unsigned *size_copied){
    unsigned size;
    if ((file_size - *size_copied) > block_size){
        size = block_size;
    }
    else{
        size = file_size - *size_copied;
    }

    off_t offset = block_nr * block_size;
    int res = pread(img, buf, size, offset);
    if (res < 0){
        return -errno;
    }
    res = pwrite(out, buf, size, *size_copied);
    if(res < 0){
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
        if ((inode.i_block[i] == 0) || (size_copied == file_size)){
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

int dump_file(int img, const char *path, int out)
{
    struct ext2_super_block super_block = {0};
    ssize_t read_result = pread(img, &super_block, sizeof(super_block), SUPERBLOCK_OFFSET);
    if (read_result < 0){
        return -errno;
    }

    // first find root dir inode offset

    off_t root_dir_inode_offset = 0;
    find_inode_offset(img, super_block, ROOT_DIR_INODE_NR, &root_dir_inode_offset);

    unsigned block_size = 1024 << super_block.s_log_block_size;
    int file_inode_nr = ROOT_DIR_INODE_NR;

    int path_len = strlen(path);
    char path_copy[path_len + 1];
    memset(path_copy, '\0', (path_len + 1) * sizeof(char));
    snprintf(path_copy, path_len + 1, "%s", path);

    // find file inode by path

    int find_result = find_inode_number_by_path(img, block_size, path_copy, &file_inode_nr, root_dir_inode_offset, super_block);
    if (find_result < 0){
        return find_result;
    }
    // then we need to copy by inode

    off_t file_inode_offset = 0;
    find_inode_offset(img, super_block, file_inode_nr, &file_inode_offset);

    return copy_inode(img, block_size, out, file_inode_offset);
}
