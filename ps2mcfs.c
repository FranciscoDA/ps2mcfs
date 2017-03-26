
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

#include "fat.h"
#include "ps2mcfs.h"

#define div_ceil(x, y) ((x)/(y) + ((x)%(y) != 0))

bool ps2mcfs_is_directory(const struct dir_entry_t* const dirent) {
	return dirent->mode & 0x20;
}
bool ps2mcfs_is_file(const struct dir_entry_t* const dirent) {
	return dirent->mode & 0x10;
}
/**
 * returns the absolute offset in bytes to the relative cluster 'clus'
 */
void* relative_to_absolute(void* data, off_t offset) {
	struct superblock_t* s = (struct superblock_t*) data;
	return data + s->first_allocatable * fat_cluster_size(s) + offset;
}

void date_time_now(struct date_time_t* dt) {
	struct tm t;
	time_t now = time(NULL);
	gmtime_r(&now, &t);
	dt->second = t.tm_sec;
	dt->minute = t.tm_min;
	dt->hour = t.tm_hour;
	dt->day = t.tm_mday;
	dt->month = t.tm_mon+1;
	dt->year = t.tm_year+1900;
}
time_t date_time_to_timestamp(const struct date_time_t* const dt) {
	struct tm time = {
		dt->second, dt->minute, dt->hour,
		dt->day, dt->month-1, dt->year-1900,
		0, 0, 0
	};
	return mktime(&time);
}

struct superblock_t* ps2mcfs_get_superblock(void* data, size_t size) {
	if (size < sizeof(struct superblock_t)) {
		return NULL;
	}
	struct superblock_t* s = (struct superblock_t*) data;
	char magic[] = "Sony PS2 Memory Card Format 1.2.0.0";
	if (strncmp(s->magic, magic, sizeof(magic)) == 0 &&
			s->clusters_per_card * s->pages_per_cluster * s->page_size == size &&
			size % (1024*1024) == 0 &&
			s->type == 2) {
		return s;
	}
	return NULL;
}

size_t fat_cluster_size(void* data) {
	struct superblock_t* s = (struct superblock_t*) data;
	return s->page_size * s->pages_per_cluster;
}
cluster_t fat_max_cluster(void* data) {
	struct superblock_t* s = (struct superblock_t*) data;
	return s->last_allocatable;
}
cluster_t* fat_get_entry_position(void* data, cluster_t clus) {
	struct superblock_t* s = (struct superblock_t*) data;
	uint32_t csize = fat_cluster_size(data);
	uint32_t k = csize / sizeof(cluster_t); // fat/indirfat entries per cluster
	uint32_t fat_offset = clus % k;
	uint32_t indirect_index = clus / k;
	uint32_t indirect_offset = indirect_index % k;
	uint32_t dbl_indirect_index = indirect_index / k;
	uint32_t indirect_cluster_num = s->indirect_fat_clusters[dbl_indirect_index];
	void* fat_cluster_pos = data + indirect_cluster_num * csize + indirect_offset*4;
	uint32_t fat_cluster_num = *((uint32_t*) fat_cluster_pos);
	return (cluster_t*) (data + fat_cluster_num * csize + fat_offset*sizeof(cluster_t));
}
cluster_t fat_get_table_entry(void* data, cluster_t clus) {
	return *(fat_get_entry_position(data, clus));
}
void fat_set_table_entry(void* data, cluster_t clus, cluster_t newval) {
	*fat_get_entry_position(data, clus) = newval;
}

int ps2mcfs_get_child(void* data, cluster_t clus0, off_t entrynum, struct dir_entry_t* dest) {
	off_t offset;
	if (!fat_seek_bytes(data, clus0, entrynum * sizeof(struct dir_entry_t), &offset))
		return -ENOENT;
	*dest = *((struct dir_entry_t*) relative_to_absolute(data, offset));
	return 0;
}
int ps2mcfs_set_child(void* data, cluster_t clus0, off_t entrynum, const struct dir_entry_t* const src) {
	off_t offset;
	if (!fat_seek_bytes(data, clus0, entrynum * sizeof(struct dir_entry_t), &offset))
		return -ENOENT;
	*((struct dir_entry_t*) relative_to_absolute(data, offset)) = *src;
	return 0;
}

/**
 * climb up one node from the 'dirent' directory and store the result in place
 */
void ps2mcfs_climb(void* data, struct dir_entry_t* dirent) {
	ps2mcfs_get_child(data, dirent->cluster, 0, dirent); // dirent's '.' entry
	ps2mcfs_get_child(data, dirent->cluster, 0, dirent); // get parent '.' entry
	ps2mcfs_get_child(data, dirent->cluster, dirent->dir_entry, dirent); // get parent entry from grandparent
}

/**
 * Fetch the dirent that corresponds to 'path'
 * the dirent is returned in the destdirent pointer
 *
 */
int ps2mcfs_browse(const struct superblock_t* const s, void* data, const char* path, struct dir_entry_t* destdirent) {
	const char* slash = strchr(path, '/');
	struct dir_entry_t dirent;
	ps2mcfs_get_child(data, s->root_cluster, 0, &dirent);
	while (slash != NULL) {
		if (slash != path) {
			if (!ps2mcfs_is_directory(&dirent))
				return -ENOENT;
			if (slash-path >= 32)
				return -EINVAL;
			if (slash-path == 2 && strncmp(path, "..", 2) == 0) {
				ps2mcfs_climb(data, &dirent);
			}
			else if (strcmp(path, ".") != 0) {
				int i;
				for(i = 0; i < dirent.length; i++) {
					struct dir_entry_t dirent2;
					ps2mcfs_get_child(data, dirent.cluster, i, &dirent2);
					if (strncmp(dirent2.name, path, slash-path) == 0) {
						dirent = dirent2;
						break;
					}
				}
				if (i == dirent.length)
					return -ENOENT;
			}
		}
		if (*slash != 0) {
			path = slash+1;
			slash = strchr(path, '/');
			if (slash == NULL) // slash not found, make slash point to the last null
				slash = path + strlen(path);
		}
		else { // slash was already pointing to the last null char, return
			break;
		}
	}
	if (destdirent)
		*destdirent = dirent;
	return 0;
}

void ps2mcfs_stat(const struct dir_entry_t* const dirent, struct stat* stbuf) {
	if (ps2mcfs_is_directory(dirent))
		stbuf->st_mode |= S_IFDIR;
	else
		stbuf->st_mode |= S_IFREG;
	stbuf->st_size = dirent->length;
	stbuf->st_blksize = sizeof(struct dir_entry_t);
	stbuf->st_blocks = 1;
	stbuf->st_mtime = date_time_to_timestamp(&dirent->modification);
	stbuf->st_ctime = date_time_to_timestamp(&dirent->creation);
	// fat32 doesn't manage per-user permissions, copy rwx permissions
	// across the 'user', 'group' and 'other' umasks
	stbuf->st_mode += (dirent->mode & 7) * 0111;
}

int ps2mcfs_read(const struct superblock_t* const s, void* data, struct dir_entry_t* dirent, void* buf, size_t size, off_t offset)  {
	if (offset > dirent->length)
		return 0;
	if (offset + size > dirent->length)
		size = dirent->length-offset;

	size_t k = fat_cluster_size(data);
	off_t x;
	if(!fat_seek_bytes(data, dirent->cluster, offset, &x))
		return 0; // shouldn't happen
	memcpy(buf, relative_to_absolute(data, x), MIN(size, k-offset%k));
	size_t bytes = MIN(size, k-offset%k);
	cluster_t clus0 = x/k;
	while (bytes < size) {
		clus0 = fat_seek(data, clus0, 1, SEEK_CUR);
		if (clus0 == 0xFFFFFFFF)
			break; // unexpected end of file?
		memcpy(buf+bytes, relative_to_absolute(data, clus0*k), MIN(size-bytes, k));
		bytes += MIN(size-bytes, k);
	}
	return bytes;
}

int ps2mcfs_mkdir(const struct superblock_t* const s, void* data, struct dir_entry_t* parent, const char* name, uint16_t mode) {
	size_t dirents_per_cluster = fat_cluster_size(data) / sizeof(struct dir_entry_t);
	cluster_t last = fat_truncate(data, parent->cluster, parent->length/dirents_per_cluster);

	struct dir_entry_t new_child;
	new_child.mode = mode | 0x20;
	new_child.length = 2;
	date_time_now(&new_child.creation);
	new_child.cluster = fat_find_free_cluster(data, 0);
	fat_truncate(data, new_child.cluster, 2/dirents_per_cluster);
	new_child.modification = new_child.creation;
	new_child.attributes = 0;
	strcpy(new_child.name, name);
	ps2mcfs_set_child(data, last, parent->length % dirents_per_cluster, &new_child);

	// make the '.' and '..' entries for the new child
	struct dir_entry_t dummy;
	// what else should i fill here?
	dummy.cluster = parent->cluster;
	dummy.dir_entry = parent->length;
	dummy.mode = new_child.mode;
	strcpy(dummy.name, ".");
	ps2mcfs_set_child(data, new_child.cluster, 0, &dummy);
	strcpy(dummy.name, "..");
	ps2mcfs_set_child(data, new_child.cluster, 1, &dummy);

	// update the parent
	parent->length++;
	// load the parent's '.' entry
	ps2mcfs_get_child(data, parent->cluster, 0, &dummy);
	ps2mcfs_set_child(data, dummy.cluster, dummy.dir_entry, parent);
	return 0;
}

int ps2mcfs_write(const struct superblock_t* const s, void* data, struct dir_entry_t* dirent, void* buf, size_t size, off_t offset) {
	size_t k = fat_cluster_size(data);
	if (offset + size > dirent->length) {
		size_t needed_clusters = div_ceil(offset+size, k);
		cluster_t clusn = fat_truncate(data, dirent->cluster, needed_clusters);
		if (clusn == 0xFFFFFFFF)
			return -ENOSPC;
		dirent->length = offset + size;
	}

	size_t bytes = 0;
	off_t x;
	if(!fat_seek_bytes(data, dirent->cluster, offset/k, &x))
		return 0; // shouldn't happen
	
	memcpy(relative_to_absolute(data, x) + offset, buf, MIN(size, k-(offset%k)));
	bytes = MIN(size, k-offset%k);
	cluster_t clus0 = x/k;
	while (bytes < size) {
		clus0 = fat_seek(data, clus0, 1, SEEK_CUR);
		if (clus0 == 0xFFFFFFFF)
			break; // unexpected end of file?
		memcpy(relative_to_absolute(data, clus0*k), buf+bytes, MIN(size-bytes, k));
		bytes += MIN(size-bytes, k);
	}
	return bytes;
}
