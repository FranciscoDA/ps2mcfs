#ifndef __PS2MCFS_H__
#define __PS2MCFS_H__

#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h> // struct stat

#include "fat.h"

/**
 * Converts a UNIX `time_t` struct into an equivalent `date_time_t` struct that is suitable for storing a PS2 FAT direntry
*/
void ps2mcfs_time_to_date_time(time_t thetime, date_time_t* dt);

bool ps2mcfs_is_directory(const dir_entry_t* const dirent);
bool ps2mcfs_is_file(const dir_entry_t* const dirent);

/**
 * Reads the superblock from the vmc data and initializes the field in the output metadata struct
*/
int ps2mcfs_get_superblock(vmc_meta_t* metadata_out, void* raw_data, size_t size);

void ps2mcfs_ls(const vmc_meta_t* vmc_meta, dir_entry_t* parent, int(* cb)(dir_entry_t* child, void* extra), void* extra);
int ps2mcfs_browse(const vmc_meta_t* vmc_meta, dir_entry_t* root, const char* path, browse_result_t* dest);
dir_entry_t ps2mcfs_locate(const vmc_meta_t* vmc_meta, browse_result_t* src);

void ps2mcfs_stat(const dir_entry_t* const dirent, struct stat* stbuf);
int ps2mcfs_read(const vmc_meta_t* vmc_meta, const dir_entry_t* dirent, void* buf, size_t size, off_t offset);

void ps2mcfs_utime(const vmc_meta_t* vmc_meta, browse_result_t* dirent, date_time_t modification);

/**
 * Creates a directory entry for a new subdirectory
*/
int ps2mcfs_mkdir(const vmc_meta_t* vmc_meta, dir_entry_t* parent, const char* name, uint16_t mode);

/**
 * Creates a directory entry for a new file
*/
int ps2mcfs_create(const vmc_meta_t* vmc_meta, dir_entry_t* parent, const char* name, cluster_t cluster, uint16_t mode);

/**
 * Writes data into the file referenced by the directory entry
*/
int ps2mcfs_write(const vmc_meta_t* vmc_meta, const browse_result_t* dirent, const void* buf, size_t size, off_t offset);


int ps2mcfs_unlink(const vmc_meta_t* vmc_meta, const dir_entry_t unlinked_file, const dir_entry_t parent, size_t index_in_parent);

int ps2mcfs_rmdir(const vmc_meta_t* vmc_meta, const dir_entry_t removed_dir, const dir_entry_t parent, size_t index_in_parent);

int ps2mcfs_set_child(const vmc_meta_t* vmc_meta, cluster_t clus0, unsigned int entrynum, dir_entry_t* src);

#endif
