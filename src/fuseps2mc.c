
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <linux/fs.h> // RENAME_EXCHANGE, RENAME_NOREPLACE
#include <linux/limits.h> // PATH_MAX

#define FUSE_USE_VERSION 30

#include <fuse3/fuse.h>

#include "vmc_types.h"
#include "ps2mcfs.h"
#include "utils.h"


static char vmc_file_path[PATH_MAX];
static vmc_meta_t vmc_metadata = {.superblock = NULL, .raw_data = NULL, .ecc_bytes = 0};

static void* do_init(struct fuse_conn_info* conn, struct fuse_config* cfg) {
	FILE* f = fopen(vmc_file_path, "rb");
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);
	void* vmc_raw_data = malloc(size);
	fread(vmc_raw_data, size, 1, f);
	fclose(f);
	int err = ps2mcfs_get_superblock(&vmc_metadata, vmc_raw_data, size);
	if (err == -1 || vmc_metadata.superblock == NULL || vmc_metadata.raw_data == NULL) {
		printf("Detected error while reading superblock\n");
		struct fuse_context* ctx = fuse_get_context();
		fuse_exit(ctx->fuse);
	}
	return NULL;
}

void init_stat(struct stat* stbuf) {
	stbuf->st_gid = fuse_get_context()->gid;
	stbuf->st_uid = fuse_get_context()->uid;
}

static int do_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
	browse_result_t result;
	int err = ps2mcfs_browse(&vmc_metadata, NULL, path, &result);
	if (err)
		return err;
	init_stat(stbuf);
	ps2mcfs_stat(&result.dirent, stbuf);
	return 0;
}

typedef struct {
	void* buf;
	fuse_fill_dir_t filler;
} readdir_args;

int readdir_cb(dir_entry_t* child, void* extra) {
	readdir_args* args = (readdir_args*) extra;
	struct stat dirstat;
	init_stat(&dirstat);
	ps2mcfs_stat(child, &dirstat);
	return args->filler(args->buf, child->name, &dirstat, 0, 0);
}

static int do_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
	browse_result_t parent;
	ps2mcfs_browse(&vmc_metadata, NULL, path, &parent);
	readdir_args extra = { .buf = buf, .filler = filler };
	ps2mcfs_ls(&vmc_metadata, &parent.dirent, readdir_cb, &extra);
	return 0;
}

static int do_open(const char* path, struct fuse_file_info* fi) {
	return ps2mcfs_browse(&vmc_metadata, NULL, path, NULL);
}

static int do_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
	browse_result_t result;
	ps2mcfs_browse(&vmc_metadata, NULL, path, &result);
	return ps2mcfs_read(&vmc_metadata, &result.dirent, buf, size, offset);
}

static int do_mkdir(const char* path, mode_t mode) {
	char dir_name[PATH_MAX];
	char base_name[NAME_MAX];
	strcpy(dir_name, path);
	strcpy(base_name, path);
	browse_result_t parent;
	int err = ps2mcfs_browse(&vmc_metadata, NULL, dirname(dir_name), &parent);
	if (err)
		return err;
	// use the most permissive combination of umask
	mode = ((mode/64)|(mode/8)|mode) & 0007;
	return ps2mcfs_mkdir(&vmc_metadata, &parent.dirent, basename(base_name), mode);
}

static int do_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
	printf("Creating file %s\n", path);
	char dir_name[PATH_MAX];
	char base_name[NAME_MAX];
	strcpy(dir_name, path);
	strcpy(base_name, path);
	browse_result_t parent;
	int err = ps2mcfs_browse(&vmc_metadata, NULL, dirname(dir_name), &parent);
	if (err)
		return err;
	mode = ((mode / 64) | (mode / 8) | mode) & 0007;
	return ps2mcfs_create(&vmc_metadata, &parent.dirent, basename(base_name), CLUSTER_INVALID, mode);
}

static int do_utimens(const char* path, const struct timespec tv[2], struct fuse_file_info* fi) {
	browse_result_t result;
	int err = ps2mcfs_browse(&vmc_metadata, NULL, path, &result);
	if (err)
		return err;
	date_time_t modification;
	if (tv[1].tv_nsec == UTIME_OMIT) {
		// UTIMENSAT(2): If the tv_nsec field of one of the timespec structures has the special
		// value UTIME_OMIT, then the corresponding file timestamp is left unchanged
		return 0;
	}
	if (tv[1].tv_nsec == UTIME_NOW) {
		// UTIMENSAT(2): If the tv_nsec field of one of the timespec structures has the special
		// value UTIME_NOW, then the corresponding file timestamp is set to the current time
		ps2mcfs_time_to_date_time(time(NULL), &modification);
	}
	else {
		// 1 second = 1e9 nanoseconds
		ps2mcfs_time_to_date_time(tv[1].tv_nsec / 1000000000, &modification);
	}
	ps2mcfs_utime(&vmc_metadata, &result, modification);
	return 0;
}

static int do_write(const char* path, const char* data, size_t size, off_t offset, struct fuse_file_info* fi) {
	browse_result_t result;
	int err = ps2mcfs_browse(&vmc_metadata, NULL, path, &result);
	if (err)
		return err;
	return ps2mcfs_write(&vmc_metadata, &result, (const void*) data, size, offset);
}

static int do_unlink(const char* path) {
	browse_result_t result;
	int err = ps2mcfs_browse(&vmc_metadata, NULL, path, &result);
	if (err)
		return err;
	return ps2mcfs_unlink(&vmc_metadata, result.dirent, result.parent, result.index);
}

static int do_rmdir(const char* path) {
	browse_result_t result;
	int err = ps2mcfs_browse(&vmc_metadata, NULL, path, &result);
	if (err)
		return err;
	return ps2mcfs_rmdir(&vmc_metadata, result.dirent, result.parent, result.index);
}

static int do_rename(const char * path_from, const char * path_to, unsigned int flags) {
	browse_result_t origin, destination;
	int err1 = ps2mcfs_browse(&vmc_metadata, NULL, path_from, &origin);
	int err2 = ps2mcfs_browse(&vmc_metadata, NULL, path_to, &destination);

	if (err1) {
		return err1;
	}
	if ((flags & RENAME_NOREPLACE) && !err2) {
		return -EEXIST;
	}
	if ((flags & RENAME_EXCHANGE) && err2) {
		return -ENOENT;
	}
	// target file does not exist. create it
	if (err2 == -ENOENT) {
		char dir_name[PATH_MAX];
		char base_name[NAME_MAX];
		strcpy(dir_name, path_to);
		strcpy(base_name, path_to);
		// browse to the parent dir
		if ((err2 = ps2mcfs_browse(&vmc_metadata, NULL, dirname(dir_name), &destination)))
			return err2;
		if ((err2 = ps2mcfs_create(&vmc_metadata, &destination.dirent, basename(base_name), origin.dirent.cluster, origin.dirent.mode)))
			return err2;
	}

	// browse again to reload dirents and parent dirents
	if ((err1 = ps2mcfs_browse(&vmc_metadata, NULL, path_from, &origin)))
		return err1;
	if ((err2 = ps2mcfs_browse(&vmc_metadata, NULL, path_to, &destination)))
		return err2;

	// Swap files origin <-> destination
	ps2mcfs_set_child(&vmc_metadata, destination.parent.cluster, destination.index, &origin.dirent);
	ps2mcfs_set_child(&vmc_metadata, origin.parent.cluster, origin.index, &destination.dirent);
	// delete the old origin, which is now stored in destination.parent[destination.index]
	if (!(flags & RENAME_EXCHANGE)) {
		// prevent deleting the contents of the moved file in case they point to the same file contents
		if (origin.dirent.cluster == destination.dirent.cluster)
			origin.dirent.cluster = CLUSTER_INVALID;
		ps2mcfs_unlink(&vmc_metadata, origin.dirent, destination.parent, destination.index);
	}
	return 0;
}

static struct fuse_operations operations = {
	.init = do_init,
	.getattr = do_getattr,
	.readdir = do_readdir,
	.open = do_open,
	.read = do_read,
	.mkdir = do_mkdir,
	.create = do_create,
	.utimens = do_utimens,
	.write = do_write,
	.unlink = do_unlink,
	.rmdir = do_rmdir,
	.rename = do_rename,
};

void usage(FILE* stream, char* arg0) {
	fprintf(
		stream,
		"Usage: %s <ps2-memory-card-image> <mountpoint> [FUSE options]\n"
		"Mounts a Sony PlayStation 2 memory card image as a local filesystem in userspace\n",
		arg0
	);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		usage(stderr, argv[0]);
		return 1;
	}
	realpath(argv[1], vmc_file_path);
	SWAP(argv[1], argv[argc - 1]);
	return fuse_main(argc - 1, argv, &operations, NULL);
}

