
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

struct date_time_t {
	char _unused;
	uint8_t second;
	uint8_t minute;
	uint8_t hour;
	uint8_t day;
	uint8_t month;
	uint16_t year;
};

struct dir_entry_t {
	uint16_t mode;
	uint16_t _unused0;
	uint32_t length;
	struct date_time_t creation;
	uint32_t cluster;
	uint32_t dir_entry;
	struct date_time_t modification;
	uint32_t attributes;
	char _unused1[28];
	char name[32];
	char _unused2[416];
};

struct superblock_t {
	char magic[40];
	uint16_t page_size;
	uint16_t pages_per_cluster;
	uint16_t pages_per_block;
	uint16_t _unused0;
	uint32_t clusters_per_card;
	uint32_t first_allocatable;
	uint32_t last_allocatable;
	uint32_t root_cluster;
	uint32_t backup_block1;
	uint32_t backup_block2;
	char _unused1[8];
	uint32_t indirect_fat_clusters[32];
	uint32_t bad_block_list[32];
	uint8_t type;
	uint8_t flags;
};

struct superblock_t* ps2mcfs_get_superblock(void* data, size_t size);
void ps2mcfs_get_dirent(struct superblock_t* s, void* data, uint32_t clus0, uint32_t entrynum, struct dir_entry_t** dest);
int ps2mcfs_browse (struct superblock_t* s, void* data, const char* path, uint32_t* destclus, struct dir_entry_t** destdirent);
void ps2mcfs_stat(struct dir_entry_t* dirent, struct stat* stbuf);
int ps2mcfs_read(struct superblock_t* s, void* data,  uint32_t clus0, struct dir_entry_t* dirent, void* buf, size_t size, off_t offset);

