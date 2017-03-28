
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>

#define FUSE_USE_VERSION 30

#include <fuse3/fuse.h>

#include "ps2mcfs.h"

static char vmc_file_path[PATH_MAX];
static void* vmc_raw_data;
static struct superblock_t* vmc_superblock = NULL;

static void* do_init(struct fuse_conn_info* conn, struct fuse_config* cfg) {
	FILE* f = fopen(vmc_file_path, "rb");
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);
	vmc_raw_data = malloc(size);
	fread(vmc_raw_data, size, 1, f);
	fclose(f);
	vmc_superblock = ps2mcfs_get_superblock(vmc_raw_data, size);
	return NULL;
}

void init_stat(struct stat* stbuf) {
	stbuf->st_gid = fuse_get_context()->gid;
	stbuf->st_uid = fuse_get_context()->uid;
}

static int do_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
	struct dir_entry_t dirent;
	int err = ps2mcfs_browse(vmc_superblock, vmc_raw_data, path, &dirent);
	if (err < 0) {
		return err;
	}
	init_stat(stbuf);
	ps2mcfs_stat(&dirent, stbuf);
	return 0;
}

static int do_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
	struct dir_entry_t parent_dirent;
	ps2mcfs_browse(vmc_superblock, vmc_raw_data, path, &parent_dirent);
	for (int i = 0; i < parent_dirent.length; ++i) {
		struct dir_entry_t dirent;
		struct stat dirstat;
		ps2mcfs_get_child(vmc_raw_data, parent_dirent.cluster, i, &dirent);
		init_stat(&dirstat);
		ps2mcfs_stat(&dirent, &dirstat);
		if (filler(buf, dirent.name, &dirstat, 0, 0) != 0)
			break;
	}
	return 0;
}

static int do_open(const char* path, struct fuse_file_info* fi) {
	return ps2mcfs_browse(vmc_superblock, vmc_raw_data, path, NULL);
}

static int do_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
	struct dir_entry_t dirent;
	ps2mcfs_browse(vmc_superblock, vmc_raw_data, path, &dirent);
	return ps2mcfs_read(vmc_superblock, vmc_raw_data, &dirent, buf, size, offset);
}

static int do_mkdir(const char* path, mode_t mode) {
	char dir_name[PATH_MAX];
	char base_name[NAME_MAX];
	strcpy(dir_name, path);
	strcpy(base_name, path);
	struct dir_entry_t parent_dirent;
	int err = ps2mcfs_browse(vmc_superblock, vmc_raw_data, dirname(dir_name), &parent_dirent);
	if (err)
		return err;
	// use the most permissive combination of umask
	mode = ((mode & 0700) / 64) | ((mode & 0070) / 8) | (mode & 0007);
	return ps2mcfs_mkdir(vmc_superblock, vmc_raw_data, &parent_dirent, basename(base_name), mode);
}

static struct fuse_operations operations = {
	.init = do_init,
	.getattr = do_getattr,
	.readdir = do_readdir,
	.open = do_open,
	.read = do_read,
	.mkdir = do_mkdir
};

void usage(char* arg0) {
	printf("Usage: %s <ps2-memory-card-image> <mountpoint> [FUSE options]\n", arg0);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}
	char** nargv = (char**)malloc((argc-1) * sizeof(char*));
	nargv[0] = argv[0];
	for (int i = 2; i < argc; i++)
		nargv[i-1] = argv[i];
	int nargc = argc-1;

	realpath(argv[1], vmc_file_path);
	return fuse_main(nargc, nargv, &operations, NULL);
}

