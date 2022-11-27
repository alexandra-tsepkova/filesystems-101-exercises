#include <ext2_func.h>

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

int find_inode_offset(int img, struct ext2_super_block super_block, int inode_nr, off_t *inode_offset){
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

// ====================================================================================================================


int read_direct_block(int img, unsigned block_size, unsigned block_nr, fuse_fill_dir_t filler, void *buf){
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
        int type;
        if (dir_entry.file_type == EXT2_FT_REG_FILE) type = S_IFREG;
        else if (dir_entry.file_type == EXT2_FT_DIR) type = S_IFDIR;
        else return -1;

        char *name = {0};
        name = calloc(256, sizeof(char));
        memset(name, '\0', 256 * sizeof(char));
        snprintf(name, dir_entry.name_len + 1, "%s", dir_entry.name);

        struct stat st;
        st.st_mode = type;
        filler(buf, name, &st, 0, 0);

        free(name);
        current_offset += dir_entry.rec_len;
    }
    return 0;
}

int read_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned *indir_buf, fuse_fill_dir_t filler, void *buf){
    off_t offset = block_nr * block_size;
    int res = pread(img, indir_buf, block_size, offset);
    if (res < (int)block_size){
        return -errno;
    }
    for (unsigned i = 0; i < (unsigned)block_size / sizeof(unsigned); ++i){
        if(indir_buf[i] == 0){
            break;
        }
        int read_result = read_direct_block(img, block_size, indir_buf[i], filler, buf);
        if(read_result < 0){
            return read_result;
        }
    }
    return 0;
}

int read_double_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned *double_indir_buf, fuse_fill_dir_t filler, void *buf){
    unsigned *indir_buf = calloc(1, block_size);
    off_t offset = block_nr * block_size;
    int res = pread(img, double_indir_buf, block_size, offset);
    if (res < (int)block_size){
        return -errno;
    }
    for (unsigned i = 0; i < (unsigned)block_size / sizeof(unsigned); ++i){
        int read_result = read_indirect_block(img, block_size, double_indir_buf[i], indir_buf, filler, buf);
        if(read_result < 0){
            free(indir_buf);
            return read_result;
        }
    }
    free(indir_buf);
    return 0;
}


int parse_content_of_inode_as_dir(int img, unsigned block_size, off_t inode_offset, fuse_fill_dir_t filler, void *buf){
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
            read_result = read_direct_block(img, block_size, inode.i_block[i], filler, buf);
            if (read_result < 0){
                free(indir_buf);
                free(double_indir_buf);
                return read_result;
            }

        }
        else if (i == EXT2_IND_BLOCK){
            read_result = read_indirect_block(img, block_size, inode.i_block[i], indir_buf, filler, buf);
            if (read_result < 0){
                free(indir_buf);
                free(double_indir_buf);
                return read_result;
            }
        }
        else if (i == EXT2_DIND_BLOCK){
            read_result = read_double_indirect_block(img, block_size, inode.i_block[i], double_indir_buf, filler, buf);
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

// ====================================================================================================================

int find_entry_direct_block(int img, unsigned block_size, unsigned block_nr, unsigned ext2_filetype, const char *path, int *file_inode_nr){
    struct ext2_dir_entry_2 dir_entry = {};
    (void)ext2_filetype;
    off_t beginning_offset = block_size * block_nr;
    off_t current_offset = block_size * block_nr;
    while(current_offset < beginning_offset + block_size){

        int res = pread(img, &dir_entry, sizeof(dir_entry), current_offset);
        if (res < 0){
            return -errno;
        }
        if(dir_entry.inode == 0){
            return -ENOENT;
        }

        char name[PATH_MAX];
        memset(name, '\0', PATH_MAX * sizeof(char));
        memcpy(name, dir_entry.name, dir_entry.name_len);
//        snprintf(name, dir_entry.name_len + 1, "%s", dir_entry.name);

        unsigned remaining_path_len = strlen(path);
        char* entry_name = get_entry_name(path, &remaining_path_len);
        unsigned entry_name_len = strlen(entry_name);
        unsigned name_len = strlen(name);

        if (name_len == entry_name_len){
            if (strncmp(name, entry_name, name_len) == 0){
                free(entry_name);
//                if((dir_entry.file_type != ext2_filetype) && (ext2_filetype == EXT2_FT_DIR)){
//                    return -ENOTDIR;
//                }
                *file_inode_nr = dir_entry.inode;
                return 0;
            }
        }
        current_offset += dir_entry.rec_len;
        free(entry_name);
    }
    return 1;
}

int find_entry_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned ext2_filetype, const char *path, int *file_inode_nr, unsigned *indir_buf){
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

int find_entry_double_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned ext2_filetype, const char *path, int *file_inode_nr, unsigned *double_indir_buf){
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

int find_inode_number_by_path(int img, unsigned block_size, const char *path, int *file_inode_nr, off_t inode_offset, struct ext2_super_block super_block){
    struct ext2_inode inode = {0};
    ssize_t read_result = pread(img, &inode, sizeof(inode), inode_offset);
    if (read_result < 0){
        return -errno;
    }
    unsigned remaining_path_len = strlen(path);
    char* entry_name = get_entry_name(path, &remaining_path_len);
    free(entry_name);

    unsigned entry_type = remaining_path_len == 0 ? EXT2_FT_UNKNOWN : EXT2_FT_DIR;

    for (int i = 0; i < EXT2_N_BLOCKS; ++i){
        if (inode.i_block[i] == 0){
            return -ENOENT;
        }

        if(i < EXT2_NDIR_BLOCKS){
            int find_result = find_entry_direct_block(img, block_size, inode.i_block[i], entry_type, path, file_inode_nr);
            if (find_result < 0){
                return find_result;
            }
            else if(find_result == 0){
                if(remaining_path_len != 0) {
                    if (remaining_path_len == 1) return -ENOTDIR;
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
                    if (remaining_path_len == 1) return -ENOTDIR;
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
                    if (remaining_path_len == 1) return -ENOTDIR;
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

// ====================================================================================================================


int find_dir_inode_number_by_path(int img, const char *path, struct ext2_super_block super_block){
    off_t root_dir_inode_offset = 0;
    find_inode_offset(img, super_block, ROOT_DIR_INODE_NR, &root_dir_inode_offset);

    unsigned block_size = 1024 << super_block.s_log_block_size;
    int dir_inode_nr = ROOT_DIR_INODE_NR;

    int path_len = strlen(path);
    char path_copy[path_len + 1];
    memset(path_copy, '\0', (path_len + 1) * sizeof(char));
    snprintf(path_copy, path_len + 1, "%s", path);

    int find_result = find_inode_number_by_path(img, block_size, path_copy, &dir_inode_nr, root_dir_inode_offset, super_block);
    if (find_result < 0){
        return find_result;
    }
    return dir_inode_nr;
}

//int find_file_inode_nr_by_path(int img, const char *path, struct ext2_super_block super_block){
//    off_t root_dir_inode_offset = 0;
//    find_inode_offset(img, super_block, ROOT_DIR_INODE_NR, &root_dir_inode_offset);
//
//    unsigned block_size = 1024 << super_block.s_log_block_size;
//    int file_inode_nr = ROOT_DIR_INODE_NR;
//
//    int path_len = strlen(path);
//    char path_copy[path_len + 1];
//    memset(path_copy, '\0', (path_len + 1) * sizeof(char));
//    snprintf(path_copy, path_len + 1, "%s", path);
//
//    int find_result = find_inode_number_by_path(img, block_size, path_copy, &file_inode_nr, root_dir_inode_offset, super_block);
//    if (find_result < 0){
//        return find_result;
//    }
//    return file_inode_nr;
//}

// ====================================================================================================================

int copy_direct_block(int img, unsigned block_size, unsigned block_nr, char *buf,
                      const unsigned file_size, unsigned *size_copied,
                      size_t fuse_size, off_t fuse_offset, char *fuse_buf, size_t *fuse_buf_offset){
    unsigned size;
    if ((file_size - *size_copied) > block_size){
        size = block_size;
    }
    else{
        size = file_size - *size_copied;
    }
    off_t offset = block_nr * block_size;

    off_t block_file_offset = *size_copied;
    size_t block_read_size = size;

    off_t intersection_offset_start = block_file_offset > fuse_offset ? block_file_offset : fuse_offset;
    off_t intersection_offset_end = (block_file_offset + block_read_size) > (fuse_offset + fuse_size) ? (fuse_offset + fuse_size) : (block_file_offset + block_read_size);


    if (intersection_offset_start >= intersection_offset_end) {
        *size_copied += size;
        return 0;
    }

    size_t intersection_size = intersection_offset_end - intersection_offset_start;
    off_t intersection_block_offset = (fuse_offset + *fuse_buf_offset) - block_file_offset;

    int res = pread(img, buf, intersection_size, offset + intersection_block_offset);
    if (res < 0){
        return -errno;
    }



    memcpy(fuse_buf + *fuse_buf_offset, buf, intersection_size);
    *fuse_buf_offset += intersection_size;
    *size_copied += size;
    return 0;
}


int copy_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned *indir_buf,
                        const unsigned file_size, unsigned *size_copied,
                        size_t fuse_size, off_t fuse_offset, char *fuse_buf, size_t *fuse_buf_offset){
    off_t offset = block_nr * block_size;
    int res = pread(img, indir_buf, block_size, offset);
    if (res < (int)block_size){
        return -errno;
    }
    for (unsigned i = 0; i < (unsigned)block_size / sizeof(unsigned); ++i){
        if(indir_buf[i] == 0){
            break;
        }
        char *buf = calloc(1, block_size);
        int copy_result = copy_direct_block(img, block_size, indir_buf[i], buf, file_size, size_copied, fuse_size, fuse_offset, fuse_buf, fuse_buf_offset);
        free(buf);
        if(copy_result < 0){
            return copy_result;
        }
    }
    return 0;
}


int copy_double_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned *double_indir_buf,
                               const unsigned file_size, unsigned *size_copied,
                               size_t fuse_size, off_t fuse_offset, char *fuse_buf, size_t *fuse_buf_offset){
    unsigned *indir_buf = calloc(1, block_size);
    off_t offset = block_nr * block_size;
    int res = pread(img, double_indir_buf, block_size, offset);
    if (res < (int)block_size){
        return -errno;
    }
    for (unsigned i = 0; i < (unsigned)block_size / sizeof(unsigned); ++i){
        int copy_result = copy_indirect_block(img, block_size, double_indir_buf[i], indir_buf, file_size, size_copied, fuse_size, fuse_offset, fuse_buf, fuse_buf_offset);
        if(copy_result < 0){
            free(indir_buf);
            return copy_result;
        }
    }
    free(indir_buf);
    return 0;
}

int copy_inode(int img, unsigned block_size, struct ext2_inode inode,
               size_t fuse_size, off_t fuse_offset, char *fuse_buf, size_t *fuse_buf_offset) {
    char *buf;
    unsigned *indir_buf;
    unsigned *double_indir_buf;
    int copy_result = 0;

    const unsigned file_size = inode.i_size;
    unsigned size_copied = 0;


    for (int i = 0; i < EXT2_N_BLOCKS; ++i){
        if ((inode.i_block[i] == 0) || (size_copied == file_size) || (*fuse_buf_offset >= fuse_size)){
            break;
        }

        if(i < EXT2_NDIR_BLOCKS){
            buf = calloc(1, block_size);
            copy_result = copy_direct_block(img, block_size, inode.i_block[i], buf, file_size, &size_copied, fuse_size, fuse_offset, fuse_buf, fuse_buf_offset);
            free(buf);
            if (copy_result < 0){
                return copy_result;
            }

        }
        else if (i == EXT2_IND_BLOCK){
            indir_buf = calloc(1, block_size);
            copy_result = copy_indirect_block(img, block_size, inode.i_block[i], indir_buf, file_size, &size_copied, fuse_size, fuse_offset, fuse_buf, fuse_buf_offset);
            free(indir_buf);
            if (copy_result < 0){
                return copy_result;
            }
        }
        else if (i == EXT2_DIND_BLOCK){
            double_indir_buf = calloc(1, block_size);
            copy_result = copy_double_indirect_block(img, block_size, inode.i_block[i], double_indir_buf, file_size, &size_copied, fuse_size, fuse_offset, fuse_buf, fuse_buf_offset);
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