#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fuse.h>


#include <ext2fs/ext2fs.h>

#define ROOT_DIR_INODE_NR 2

char *get_entry_name (const char *path, unsigned *remaining_path_len);

char *get_remaining_path(const char *path);

int find_inode_offset(int img, struct ext2_super_block super_block, int inode_nr, off_t *inode_offset);

// ====================================================================================================================


int read_direct_block(int img, unsigned block_size, unsigned block_nr, fuse_fill_dir_t filler, void *buf);

int read_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned *indir_buf, fuse_fill_dir_t filler, void *buf);

int read_double_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned *double_indir_buf, fuse_fill_dir_t filler, void *buf);


int parse_content_of_inode_as_dir(int img, unsigned block_size, off_t inode_offset, fuse_fill_dir_t filler, void *buf);

// ====================================================================================================================

int find_entry_direct_block(int img, unsigned block_size, unsigned block_nr, unsigned ext2_filetype, const char *path, int *file_inode_nr);

int find_entry_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned ext2_filetype, const char *path, int *file_inode_nr, unsigned *indir_buf);

int find_entry_double_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned ext2_filetype, const char *path, int *file_inode_nr, unsigned *double_indir_buf);

int find_inode_number_by_path(int img, unsigned block_size, const char *path, int *file_inode_nr, off_t inode_offset, struct ext2_super_block super_block);
// ====================================================================================================================


int find_dir_inode_number_by_path(int img, const char *path, struct ext2_super_block super_block);

// ====================================================================================================================

int copy_direct_block(int img, unsigned block_size, unsigned block_nr, char *buf,
                      const unsigned file_size, unsigned *size_copied,
                      size_t fuse_size, off_t fuse_offset, char *fuse_buf, size_t *fuse_buf_offset);


int copy_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned *indir_buf,
                        const unsigned file_size, unsigned *size_copied,
                        size_t fuse_size, off_t fuse_offset, char *fuse_buf, size_t *fuse_buf_offset);


int copy_double_indirect_block(int img, unsigned block_size, unsigned block_nr, unsigned *double_indir_buf,
                               const unsigned file_size, unsigned *size_copied,
                               size_t fuse_size, off_t fuse_offset, char *fuse_buf, size_t *fuse_buf_offset);

int copy_inode(int img, unsigned block_size, struct ext2_inode inode,
               size_t fuse_size, off_t fuse_offset, char *fuse_buf, size_t *fuse_buf_offset);