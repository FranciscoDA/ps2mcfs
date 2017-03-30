
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include "fat.h"

static const uint16_t DF_READ = 0x0001;
static const uint16_t DF_WRITE = 0x0002;
static const uint16_t DF_EXECUTE = 0x0004;
static const uint16_t DF_FILE = 0x0010;
static const uint16_t DF_DIRECTORY = 0x0020;
static const uint16_t DF_EXISTS = 0x8000;

typedef struct {
	char _unused;
	uint8_t second;
	uint8_t minute;
	uint8_t hour;
	uint8_t day;
	uint8_t month;
	uint16_t year;
} date_time_t;

typedef struct {
	uint16_t mode;
	uint16_t _unused0;
	uint32_t length;
	date_time_t creation;
	cluster_t cluster;
	uint32_t dir_entry;
	date_time_t modification;
	uint32_t attributes;
	char _unused1[28];
	char name[32];
	char _unused2[416];
} dir_entry_t;

typedef struct {
	dir_entry_t dirent;
	dir_entry_t parent; // parent dirent
	off_t location; // absolute offset
} browse_result_t;

typedef struct {
	char magic[40];
	uint16_t page_size;
	uint16_t pages_per_cluster;
	uint16_t pages_per_block;
	uint16_t _unused0;
	uint32_t clusters_per_card;
	cluster_t first_allocatable;
	cluster_t last_allocatable;
	cluster_t root_cluster;
	uint32_t backup_block1;
	uint32_t backup_block2;
	char _unused1[8];
	cluster_t indirect_fat_clusters[32];
	cluster_t bad_block_list[32];
	uint8_t type;
	uint8_t flags;
} superblock_t;

void ps2mcfs_time_to_date_time(time_t thetime, date_time_t* dt);
bool ps2mcfs_is_directory(const dir_entry_t* const dirent);
bool ps2mcfs_is_file(const dir_entry_t* const dirent);
superblock_t* ps2mcfs_get_superblock(void* data, size_t size);

void ps2mcfs_ls(void* data, dir_entry_t* parent, int(* cb)(dir_entry_t* child, void* extra), void* extra);
int ps2mcfs_browse(void* data, dir_entry_t* root, const char* path, browse_result_t* dest);
dir_entry_t ps2mcfs_locate(void* data, browse_result_t* src);

void ps2mcfs_stat(const dir_entry_t* const dirent, struct stat* stbuf);
int ps2mcfs_read(void* data, const dir_entry_t* dirent, void* buf, size_t size, off_t offset);

void ps2mcfs_utime(void* data, browse_result_t* dirent, date_time_t modification);
int ps2mcfs_mkdir(void* data, dir_entry_t* parent, const char* name, uint16_t mode);
int ps2mcfs_create(void* data, dir_entry_t* parent, const char* name, uint16_t mode);
int ps2mcfs_write(void* data, browse_result_t* dirent, const void* buf, size_t size, off_t offset);

