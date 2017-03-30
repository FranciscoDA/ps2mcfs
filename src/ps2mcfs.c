
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

bool ps2mcfs_is_directory(const dir_entry_t* const dirent) {
	return dirent->mode & DF_DIRECTORY;
}
bool ps2mcfs_is_file(const dir_entry_t* const dirent) {
	return dirent->mode & DF_FILE;
}

void ps2mcfs_time_to_date_time(time_t thetime, date_time_t* dt) {
	struct tm t;
	gmtime_r(&thetime, &t);
	dt->second = t.tm_sec;
	dt->minute = t.tm_min;
	dt->hour = t.tm_hour;
	dt->day = t.tm_mday;
	dt->month = t.tm_mon+1;
	dt->year = t.tm_year+1900;
}
time_t date_time_to_timestamp(const date_time_t* const dt) {
	struct tm time = {
		dt->second, dt->minute, dt->hour,
		dt->day, dt->month-1, dt->year-1900,
		0, 0, 0
	};
	return mktime(&time);
}

superblock_t* ps2mcfs_get_superblock(void* data, size_t size) {
	if (size < sizeof(superblock_t)) {
		return NULL;
	}
	superblock_t* s = (superblock_t*) data;
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
	superblock_t* s = (superblock_t*) data;
	return s->page_size * s->pages_per_cluster;
}
cluster_t fat_max_cluster(void* data) {
	superblock_t* s = (superblock_t*) data;
	return s->last_allocatable;
}
off_t fat_first_cluster_offset(void* data) {
	superblock_t* s = (superblock_t*) data;
	return s->first_allocatable * fat_cluster_size(data);
}
cluster_t* fat_get_entry_position(void* data, cluster_t clus) {
	superblock_t* s = (superblock_t*) data;
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

int ps2mcfs_get_child(void* data, cluster_t clus0, size_t entrynum, dir_entry_t* dest) {
	size_t sz = fat_read_bytes(data, clus0, entrynum*sizeof(dir_entry_t), sizeof(dir_entry_t), dest);
	if(sz != sizeof(dir_entry_t))
		return -ENOENT;
	return 0;
}
int ps2mcfs_set_child(void* data, cluster_t clus0, off_t entrynum, dir_entry_t* src) {
	size_t sz = fat_write_bytes(data, clus0, entrynum*sizeof(dir_entry_t), sizeof(dir_entry_t), src);
	if(sz != sizeof(dir_entry_t))
		return -ENOENT;
	return 0;
}

/**
 * climb up one node from the 'dirent' directory and store the result in place
 */
void ps2mcfs_climb(void* data, dir_entry_t* dirent) {
	ps2mcfs_get_child(data, dirent->cluster, 0, dirent); // dirent's '.' entry
	ps2mcfs_get_child(data, dirent->cluster, 0, dirent); // get parent '.' entry
	ps2mcfs_get_child(data, dirent->cluster, dirent->dir_entry, dirent); // get parent entry from grandparent
}


void ps2mcfs_ls(void* data, dir_entry_t* parent, int(* cb)(dir_entry_t* child, void* extra), void* extra) {
	size_t dirents_per_cluster = fat_cluster_size(data) / sizeof(dir_entry_t);
	cluster_t clus = parent->cluster;
	for (int i = 0; i < parent->length; ++i) {
		if (i % dirents_per_cluster == 0 && i != 0) {
			clus = fat_seek(data, clus, 1, SEEK_CUR);
			if (clus == 0xFFFFFFFF)
				break;
		}
		dir_entry_t child;
		ps2mcfs_get_child(data, clus, i % dirents_per_cluster, &child);
		if (cb(&child, extra) != 0)
			break;
	}
}

/**
 * Fetch the dirent that corresponds to 'path'
 * the dirent, parent and offset is returned in the dest pointer
 */
int ps2mcfs_browse(void* data, dir_entry_t* root, const char* path, browse_result_t* dest) {
	const superblock_t* s = (superblock_t*) data;
	const size_t dirents_per_cluster = fat_cluster_size(data) / sizeof(dir_entry_t);

	const char* slash = strchr(path, '/');
	bool is_basename = false;
	if (!slash) { // slash not found, we're at the file name
		slash = path + strlen(path);
		is_basename = true;
	}
	dir_entry_t dirent;
	if (root)
		dirent = *root;
	else
		ps2mcfs_get_child(data, s->root_cluster, 0, &dirent);

	cluster_t clus = dirent.cluster;
	size_t dirent_number;
	
	if (slash != path) {
		if (!ps2mcfs_is_directory(&dirent))
			return -ENOTDIR;
		else if (slash-path >= 32)
			return -ENAMETOOLONG;
		else if (slash-path == 2 && strncmp(path, "..", 2) == 0)
			ps2mcfs_climb(data, &dirent);
		else if (strcmp(path, ".") != 0) {
			for (dirent_number = 0; dirent_number < root->length; dirent_number++) {
				if (dirent_number % dirents_per_cluster == 0 && dirent_number != 0)
					clus = fat_seek(data, clus, 1, SEEK_CUR);
				ps2mcfs_get_child(data, clus, dirent_number % dirents_per_cluster, &dirent);
				if (strncmp(dirent.name, path, slash-path) == 0)
					break;
			}
			if (dirent_number == root->length)
				return -ENOENT;
		}
	}
	if (is_basename) {
		if (dest) {
			dest->dirent = dirent;
			dest->parent = *root;
			dest->location = fat_first_cluster_offset(data) + clus * fat_cluster_size(data) + (dirent_number % dirents_per_cluster)*sizeof(dir_entry_t);
		}
		return 0;
	}
	return ps2mcfs_browse(data, &dirent, slash + 1, dest);
}

void ps2mcfs_stat(const dir_entry_t* const dirent, struct stat* stbuf) {
	if (ps2mcfs_is_directory(dirent))
		stbuf->st_mode |= S_IFDIR;
	else
		stbuf->st_mode |= S_IFREG;
	stbuf->st_size = dirent->length;
	stbuf->st_blksize = sizeof(dir_entry_t);
	stbuf->st_blocks = 1;
	stbuf->st_mtime = date_time_to_timestamp(&dirent->modification);
	stbuf->st_ctime = date_time_to_timestamp(&dirent->creation);
	// fat32 doesn't manage per-user permissions, copy rwx permissions
	// across the 'user', 'group' and 'other' umasks
	stbuf->st_mode += (dirent->mode & 7) * 0111;
}

int ps2mcfs_read(void* data, const dir_entry_t* dirent, void* buf, size_t size, off_t offset)  {
	if (offset > dirent->length)
		return 0;
	if (offset + size > dirent->length)
		size = dirent->length-offset;
	return fat_read_bytes(data, dirent->cluster, offset, size, buf);
}

int ps2mcfs_add_child(void* data, dir_entry_t* parent, dir_entry_t* new_child) {
	size_t dirents_per_cluster = fat_cluster_size(data) / sizeof(dir_entry_t);
	cluster_t last = fat_truncate(data, parent->cluster, div_ceil(parent->length+1,dirents_per_cluster));
	if (last == 0xFFFFFFFF)
		return -ENOSPC;

	ps2mcfs_set_child(data, last, parent->length % dirents_per_cluster, new_child);
	dir_entry_t dummy;
	parent->length++;
	ps2mcfs_get_child(data, parent->cluster, 0, &dummy); // read the parent's '.' dummy entry
	ps2mcfs_set_child(data, dummy.cluster, dummy.dir_entry, parent); // use the data in that dummy to update the parent's own dirent
	return 0;
}

void ps2mcfs_utime(void* data, browse_result_t* dirent, date_time_t modification) {
	dirent->dirent.modification = modification;
	dirent->dirent.creation = modification; // probably not ok
	*((dir_entry_t*)(data + dirent->location)) = dirent->dirent;
}

int ps2mcfs_mkdir(void* data, dir_entry_t* parent, const char* name, uint16_t mode) {
	size_t dirents_per_cluster = fat_cluster_size(data)/sizeof(dir_entry_t);
	dir_entry_t new_child;
	new_child.mode = mode | DF_DIRECTORY;
	new_child.length = 2;
	ps2mcfs_time_to_date_time(time(NULL), &new_child.creation);
	new_child.cluster = fat_allocate(data, div_ceil(2, dirents_per_cluster));
	new_child.modification = new_child.creation;
	new_child.attributes = 0;
	strcpy(new_child.name, name);

	if (new_child.cluster == 0xFFFFFFFF) {
		return -ENOSPC;
	}
	
	int err = ps2mcfs_add_child(data, parent, &new_child);
	if (err) {
		fat_free(data, new_child.cluster, false);
		return err;
	}

	// make the '.' and '..' entries for the new child
	dir_entry_t dummy;
	// what else should be filled here?
	dummy.cluster = parent->cluster;
	dummy.dir_entry = parent->length-1;
	dummy.mode = new_child.mode;
	strcpy(dummy.name, ".");
	ps2mcfs_set_child(data, new_child.cluster, 0, &dummy);
	strcpy(dummy.name, "..");
	ps2mcfs_set_child(data, new_child.cluster, 1, &dummy);

	return 0;
}

int ps2mcfs_create(void* data, dir_entry_t* parent, const char* name, uint16_t mode) {
	dir_entry_t new_child;
	new_child.mode = mode | DF_FILE;
	new_child.length = 0;
	ps2mcfs_time_to_date_time(time(NULL), &new_child.creation);
	new_child.cluster = 0xFFFFFFFF; // create empty file
	new_child.modification = new_child.creation;
	new_child.attributes = 0;
	strcpy(new_child.name, name);
	
	int err = ps2mcfs_add_child(data, parent, &new_child);
	if (err)
		return err;
	return 0;
}

int ps2mcfs_write(void* data, browse_result_t* dirent, const void* buf, size_t size, off_t offset) {
	if (offset + size > dirent->dirent.length) {
		if (dirent->dirent.cluster == 0xFFFFFFFF) {
			dirent->dirent.cluster = fat_allocate(data, 1);
		}
		size_t k = fat_cluster_size(data);
		size_t needed_clusters = div_ceil(offset+size, k);
		cluster_t clusn = fat_truncate(data, dirent->dirent.cluster, needed_clusters);
		if (clusn == 0xFFFFFFFF)
			return -ENOSPC;
		dirent->dirent.length = offset + size;
		*((dir_entry_t*)(data+dirent->location)) = dirent->dirent;
	}
	return fat_write_bytes(data, dirent->dirent.cluster, offset, size, buf);
}
