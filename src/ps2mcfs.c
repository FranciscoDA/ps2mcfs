
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

#include "vmc_types.h"
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

int ps2mcfs_get_superblock(vmc_meta_t* metadata_out, void* raw_data, size_t size) {
	// data is too small to contain a superblock
	if (size < sizeof(superblock_t)) {
		return -1;
	}
	superblock_t* s = (superblock_t*) raw_data;
	char magic[] = "Sony PS2 Memory Card Format 1.2.0.0";
	if (strncmp(s->magic, magic, sizeof(magic)) != 0) {
		printf(
			"Magic string mismatch. Make sure the memory card was properly formatted."
			"Expected: \"%s\". Read: \"%.*s\"\n", magic, (int)(sizeof(magic) - 1), s->magic
		);
		return -1;
	}
	const size_t expected_size = s->clusters_per_card * s->pages_per_cluster * s->page_size;
	const size_t expected_size_with_ecc = s->clusters_per_card * s->pages_per_cluster * (s->page_size + 16);
	if (size == expected_size) {
		metadata_out->ecc_bytes = 0;
		metadata_out->page_spare_area_size = 0;
	}
	else if (size == expected_size_with_ecc) {
		metadata_out->ecc_bytes = 12; // 3 bytes for each 128-byte chunk of a page
		metadata_out->page_spare_area_size = 16;
	}
	else {
		printf(
			"VMC File size mismatch: %luB\n"
			"\tExpected size (no ecc): %d clusters * %d pages per cluster * %d bytes per page = %luB.\n"
			"\tExpected size (16 byte ecc): %d clusters * %d pages per cluster * %d bytes per page = %luB.\n",
			size,
			s->clusters_per_card,
			s->pages_per_cluster,
			s->page_size,
			expected_size,
			s->clusters_per_card,
			s->pages_per_cluster,
			s->page_size + 16,
			expected_size_with_ecc
		);
		return -1;
	}
	if (s->type != 2) {
		printf("Unknown card type: %d. (expected 2)\n", s->type);
		return -1;
	}
	metadata_out->superblock = s;
	metadata_out->raw_data = raw_data;
	return 0;
}

int ps2mcfs_get_child(const vmc_meta_t* vmc_meta, cluster_t clus0, unsigned int entrynum, dir_entry_t* dest) {
	size_t sz = fat_read_bytes(vmc_meta, clus0, entrynum * sizeof(dir_entry_t), sizeof(dir_entry_t), dest);
	if (sz != sizeof(dir_entry_t))
		return -ENOENT;
	return 0;
}

int ps2mcfs_set_child(const vmc_meta_t* vmc_meta, cluster_t clus0, unsigned int entrynum, dir_entry_t* src) {
	size_t sz = fat_write_bytes(vmc_meta, clus0, entrynum*sizeof(dir_entry_t), sizeof(dir_entry_t), src);
	if(sz != sizeof(dir_entry_t))
		return -ENOENT;
	return 0;
}

/**
 * climb up one node from the 'dirent' directory and store the result in place
 */
void ps2mcfs_climb(const vmc_meta_t* vmc_meta, dir_entry_t* dirent) {
	ps2mcfs_get_child(vmc_meta, dirent->cluster, 0, dirent); // dirent's '.' entry
	ps2mcfs_get_child(vmc_meta, dirent->cluster, 0, dirent); // get parent '.' entry
	ps2mcfs_get_child(vmc_meta, dirent->cluster, dirent->dir_entry, dirent); // get parent entry from grandparent
}


void ps2mcfs_ls(const vmc_meta_t* vmc_meta, dir_entry_t* parent, int(* cb)(dir_entry_t* child, void* extra), void* extra) {
	size_t dirents_per_cluster = fat_cluster_capacity(vmc_meta) / sizeof(dir_entry_t);
	cluster_t clus = parent->cluster;
	for (int i = 0; i < parent->length; ++i) {
		if (i % dirents_per_cluster == 0 && i != 0) {
			if ((clus = fat_seek(vmc_meta, clus, 1)) == CLUSTER_INVALID)
				break;
		}
		dir_entry_t child;
		ps2mcfs_get_child(vmc_meta, clus, i % dirents_per_cluster, &child);
		if (cb(&child, extra) != 0)
			break;
	}
}

/**
 * Fetch the dirent that corresponds to 'path' the dirent, parent and offset is returned in the dest pointer
 */
int ps2mcfs_browse(const vmc_meta_t* vmc_meta, dir_entry_t* root, const char* path, browse_result_t* dest) {
	const size_t dirents_per_cluster = fat_cluster_capacity(vmc_meta) / sizeof(dir_entry_t);

	const char* slash = strchr(path, '/');
	bool is_basename = false;
	if (!slash) { // slash not found, we're at the base file name
		slash = path + strlen(path);
		is_basename = true;
	}
	dir_entry_t dirent;
	if (root) {
		dirent = *root;
	}
	else {
		ps2mcfs_get_child(
			vmc_meta,
			vmc_meta->superblock->root_cluster,
			0,
			&dirent
		);
	}

	cluster_t clus = dirent.cluster;
	size_t dirent_number;

	if (slash != path) {
		if (!ps2mcfs_is_directory(&dirent))
			return -ENOTDIR;
		else if (slash-path >= 32)
			return -ENAMETOOLONG;
		else if (slash-path == 2 && strncmp(path, "..", 2) == 0)
			ps2mcfs_climb(vmc_meta, &dirent);
		else if (strcmp(path, ".") != 0) {
			for (dirent_number = 0; dirent_number < root->length; dirent_number++) {
				if (dirent_number % dirents_per_cluster == 0 && dirent_number != 0)
					clus = fat_seek(vmc_meta, clus, 1);
				ps2mcfs_get_child(vmc_meta, clus, dirent_number % dirents_per_cluster, &dirent);
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
			dest->location = fat_logical_to_physical_offset(vmc_meta, clus, (dirent_number % dirents_per_cluster) * sizeof(dir_entry_t));
		}
		return 0;
	}
	return ps2mcfs_browse(vmc_meta, &dirent, slash + 1, dest);
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

int ps2mcfs_read(const vmc_meta_t* vmc_meta, const dir_entry_t* dirent, void* buf, size_t size, off_t offset)  {
	if (offset > dirent->length)
		return 0;
	if (offset + size > dirent->length)
		size = dirent->length-offset;
	return fat_read_bytes(vmc_meta, dirent->cluster, offset, size, buf);
}

int ps2mcfs_add_child(const vmc_meta_t* vmc_meta, dir_entry_t* parent, dir_entry_t* new_child) {
	size_t dirents_per_cluster = fat_cluster_capacity(vmc_meta) / sizeof(dir_entry_t);
	cluster_t last = fat_truncate(vmc_meta, parent->cluster, div_ceil(parent->length + 1, dirents_per_cluster));
	if (last == CLUSTER_INVALID)
		return -ENOSPC;

	ps2mcfs_set_child(vmc_meta, last, parent->length % dirents_per_cluster, new_child);
	dir_entry_t dummy;
	parent->length++;
	// now need to write the updated parent length into the parent's dir entry
	ps2mcfs_get_child(vmc_meta, parent->cluster, 0, &dummy); // read the parent's `.` entry (which points to its parent)
	ps2mcfs_set_child(vmc_meta, dummy.cluster, dummy.dir_entry, parent); // write the updated `parent` dirent
	return 0;
}

void ps2mcfs_utime(const vmc_meta_t* vmc_meta, browse_result_t* dirent, date_time_t modification) {
	dirent->dirent.modification = modification;
	dirent->dirent.creation = modification; // probably not ok
	*((dir_entry_t*)(vmc_meta->raw_data + dirent->location)) = dirent->dirent;
}

int ps2mcfs_mkdir(const vmc_meta_t* vmc_meta, dir_entry_t* parent, const char* name, uint16_t mode) {
	size_t dirents_per_cluster = fat_cluster_capacity(vmc_meta)/sizeof(dir_entry_t);
	dir_entry_t new_child;
	new_child.mode = mode | DF_DIRECTORY;
	new_child.length = 2;
	ps2mcfs_time_to_date_time(time(NULL), &new_child.creation);
	new_child.cluster = fat_allocate(vmc_meta, div_ceil(2, dirents_per_cluster));
	new_child.modification = new_child.creation;
	new_child.attributes = 0;
	strcpy(new_child.name, name);

	if (new_child.cluster == CLUSTER_INVALID) {
		return -ENOSPC;
	}

	int err = ps2mcfs_add_child(vmc_meta, parent, &new_child);
	if (err) {
		fat_truncate(vmc_meta, new_child.cluster, 0);
		return err;
	}

	// make the '.' and '..' entries for the new child
	dir_entry_t dummy;
	// what else should be filled here?
	dummy.cluster = parent->cluster;
	dummy.dir_entry = parent->length-1;
	dummy.mode = new_child.mode;
	strcpy(dummy.name, ".");
	ps2mcfs_set_child(vmc_meta, new_child.cluster, 0, &dummy);
	strcpy(dummy.name, "..");
	ps2mcfs_set_child(vmc_meta, new_child.cluster, 1, &dummy);

	return 0;
}

int ps2mcfs_create(const vmc_meta_t* vmc_meta, dir_entry_t* parent, const char* name, uint16_t mode) {
	dir_entry_t new_child;
	new_child.mode = mode | DF_FILE;
	new_child.length = 0;
	ps2mcfs_time_to_date_time(time(NULL), &new_child.creation);
	new_child.cluster = CLUSTER_INVALID; // create empty file
	new_child.modification = new_child.creation;
	new_child.attributes = 0;
	strcpy(new_child.name, name);

	int err = ps2mcfs_add_child(vmc_meta, parent, &new_child);
	if (err)
		return err;
	return 0;
}

int ps2mcfs_write(const vmc_meta_t* vmc_meta, browse_result_t* dirent, const void* buf, size_t size, off_t offset) {
	if (offset + size > dirent->dirent.length) {
		if (dirent->dirent.cluster == CLUSTER_INVALID) {
			dirent->dirent.cluster = fat_allocate(vmc_meta, 1);
		}
		size_t k = fat_cluster_capacity(vmc_meta);
		size_t needed_clusters = div_ceil(offset+size, k);
		cluster_t clusn = fat_truncate(vmc_meta, dirent->dirent.cluster, needed_clusters);
		if (clusn == CLUSTER_INVALID)
			return -ENOSPC;
		dirent->dirent.length = offset + size;
		*((dir_entry_t*)(vmc_meta->raw_data + dirent->location)) = dirent->dirent;
	}
	return fat_write_bytes(vmc_meta, dirent->dirent.cluster, offset, size, buf);
}