
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/param.h>

#include "ps2mcfs.h"

bool is_directory(struct dir_entry_t* dirent) {
	return dirent->mode & 0x20;
}
bool is_file(struct dir_entry_t* dirent) {
	return dirent->mode & 0x10;
}
uint32_t cluster_size(struct superblock_t* s) {
	return s->page_size * s->pages_per_cluster;
}
uint32_t relative_cluster_offset(struct superblock_t* s, uint32_t clus) {
	return (s->first_allocatable+clus) * cluster_size(s);
}
time_t date_time_to_timestamp(struct date_time_t* dt) {
	struct tm time = {
		dt->second,
		dt->minute,
		dt->hour,
		dt->day,
		dt->month-1,
		dt->year-1900,
		0,
		0,
		0
	};
	return mktime(&time);
}

struct superblock_t* ps2mcfs_get_superblock(void* data) {
	return (struct superblock_t*)(data);
}

uint32_t get_fat_entry(struct superblock_t* s, void* data, uint32_t clus) {
	uint32_t csize = cluster_size(s);
	uint32_t k = csize / 4; // fat/indirfat entries per cluster
	uint32_t fat_offset = clus % k;
	uint32_t indirect_index = clus / k;
	uint32_t indirect_offset = indirect_index % k;
	uint32_t dbl_indirect_index = indirect_index / k;
	uint32_t indirect_cluster_num = s->indirect_fat_clusters[dbl_indirect_index];
	void* fat_cluster_pos = data + indirect_cluster_num * csize + indirect_offset*4;
	uint32_t fat_cluster_num = *((uint32_t*) fat_cluster_pos);
	void* fat_entry_pos = data + fat_cluster_num * csize + fat_offset*4;
	return *((uint32_t*) fat_entry_pos);
}

void ps2mcfs_get_dirent(struct superblock_t* s, void* data, uint32_t clus0, uint32_t entrynum, struct dir_entry_t** dest) {
	uint32_t k = cluster_size(s) / sizeof(struct dir_entry_t); // directory entries per cluster
	uint32_t clusn = entrynum / k;
	while (clusn > 0) {
		clus0 = get_fat_entry(s, data, clus0) - 0x80000000;
		--clusn;
	}
	uint32_t cluster_offset = relative_cluster_offset(s, clus0) + (entrynum % k) * sizeof(struct dir_entry_t);
	*dest = (struct dir_entry_t*) (data + cluster_offset);
}

int ps2mcfs_browse (struct superblock_t* s, void* data, const char* path, uint32_t* destclus, struct dir_entry_t** destdirent) {
	const char* slash = strchr(path, '/');
	struct dir_entry_t* dirent;
	uint32_t clus0 = s->root_cluster;
	ps2mcfs_get_dirent(s, data, clus0, 0, &dirent);
	while (true) {
		if (slash != path) {
			if (!is_directory(dirent)) {
				return -ENOENT;
			}
			if (slash-path >= 32) {
				return -EINVAL;
			}
			if (slash-path == 2 && strncmp(path, "..", 2) == 0) {
				struct dir_entry_t* dirent2;
				ps2mcfs_get_dirent(s, data, clus0, 0, &dirent2);
				clus0 = dirent2->cluster;
				ps2mcfs_get_dirent(s, data, clus0, 0, &dirent2);
				ps2mcfs_get_dirent(s, data, clus0, dirent2->dir_entry, &dirent);
			}
			else if (strcmp(path, ".") != 0) {
				uint32_t i;
				for(i = 0; i < dirent->length; i++) {
					struct dir_entry_t* dirent2;
					ps2mcfs_get_dirent(s, data, clus0, i, &dirent2);
					if (strncmp(dirent2->name, path, slash-path) == 0) {
						dirent = dirent2;
						clus0 = dirent->cluster;
						break;
					}
				}
				if (i == dirent->length) {
					return -ENOENT;
				}
			}
		}
		if (*slash != 0) {
			path = slash+1;
			slash = strchr(path, '/');
			if (slash == NULL) // slash not found, make slash point to the last null
				slash = path + strlen(path);
		}
		else { // slash was already pointing to the last null char, quit
			break;
		}
	}
	if (destclus)
		*destclus = clus0;
	if (destdirent)
		*destdirent = dirent;
	return 0;
}

void ps2mcfs_stat(struct dir_entry_t* dirent, struct stat* stbuf) {
	if (is_directory(dirent))
		stbuf->st_mode |= S_IFDIR;
	else
		stbuf->st_mode |= S_IFREG;
	stbuf->st_size = dirent->length;
	stbuf->st_blksize = sizeof(struct dir_entry_t);
	stbuf->st_blocks = 1;
	stbuf->st_mtime = date_time_to_timestamp(&dirent->modification);
	stbuf->st_ctime = date_time_to_timestamp(&dirent->creation);
}

int ps2mcfs_read(struct superblock_t* s, void* data,  uint32_t clus0, struct dir_entry_t* dirent, void* buf, size_t size, off_t offset)  {
	if (offset > dirent->length)
		return 0;
	if (offset + size > dirent->length)
		size = dirent->length-offset;

	uint32_t k = cluster_size(s);
	size_t bytes = 0;
	while (offset > k) {
		uint32_t fat = get_fat_entry(s, data, clus0);
		if (fat == 0xFFFFFFFF || fat < 0x80000000)
			return 0; // unexpected end of file?
		clus0 = fat - 0x80000000;
		offset -= k;
	}
	memcpy(buf, data + relative_cluster_offset(s, clus0) + offset, MIN(size, k-offset));
	bytes = MIN(size, k-offset);
	while (bytes < size) {
		uint32_t fat = get_fat_entry(s, data, clus0);
		if (fat == 0xFFFFFFFF || fat < 0x80000000)
			break; // unexpected end of file?
		clus0 = fat - 0x80000000;
		memcpy(buf+bytes, data + relative_cluster_offset(s, clus0), MIN(size-bytes, k));
		bytes += MIN(size-bytes, k);
	}
	return bytes;
}

