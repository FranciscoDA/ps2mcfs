#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h> // EEXIST, ENOENT
#include <limits.h> // NAME_MAX
#include <libgen.h> // dirname
#include <linux/fs.h> // RENAME_EXCHANGE, RENAME_NOREPLACE

#define FUSE_USE_VERSION 30

#include <fuse3/fuse.h>
#include <fuse3/fuse_common.h>

#include "vmc_types.h"
#include "ps2mcfs.h"
#include "utils.h"


// global static instance for VMC metadata
static struct vmc_meta vmc_metadata = {.superblock = {{0}}, .file = NULL, .ecc_bytes = 0, .page_spare_area_size = 0};

static void* do_init(struct fuse_conn_info* conn, struct fuse_config* cfg) {
	int err = ps2mcfs_get_superblock(&vmc_metadata);
	if (err == -1 || vmc_metadata.file == NULL) {
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


struct cli_options {
	// fuseps2mc options
	char* mc_path;
	int sync_to_fs;

	// standard fuse options
	char* mountpoint;
	int singlethread;
	int foreground;
	int debug;
	int show_version;
	int show_help;
	unsigned int max_threads;
};

static const struct fuse_opt CLI_OPTIONS[] = {
	//FUSE_OPT_KEY("--mc ", OPT_KEY_MC_PATH),
	//FUSE_OPT_KEY("-i ",   OPT_KEY_MC_PATH),
	{.templ = "-S",             .offset = offsetof(struct cli_options, sync_to_fs),   .value = true},
	{.templ = "-h",             .offset = offsetof(struct cli_options, show_help),    .value = 1},
	{.templ = "--help",         .offset = offsetof(struct cli_options, show_help),    .value = 1},
	{.templ = "-V",             .offset = offsetof(struct cli_options, show_version), .value = 1},
	{.templ = "--version",      .offset = offsetof(struct cli_options, show_version), .value = 1},
	{.templ = "-f",             .offset = offsetof(struct cli_options, foreground),   .value = 1},
	{.templ = "-s",             .offset = offsetof(struct cli_options, singlethread), .value = 1},
	{.templ = "max_threads=%u", .offset = offsetof(struct cli_options, max_threads),  .value = 1},
	FUSE_OPT_END
};


void usage(FILE* stream, const char* program_name) {
	fprintf(
		stream,
		"Usage: %s <memory-card-image> <mountpoint> [OPTIONS]\n"
		"Mounts a Sony PlayStation 2 memory card image as a local filesystem in userspace\n"
		"Options:\n"
		"    -S                     sync filesystem changes to the memorycard file\n"
		"    -h   --help            print help\n"
		"    -V   --version         print version\n"
		"    -f                     foreground operation\n"
		"    -s                     disable multi-threaded operation\n"
		"    -o max_threads         the maximum number of threads allowed (default: 10)\n",
		program_name
	);
}


int opt_proc(void* data, const char* arg, int key, struct fuse_args* outargs) {
	struct cli_options* opts = data;
	if (key == FUSE_OPT_KEY_OPT) { // did not match any template
		return 1;
	}
	else if (key == FUSE_OPT_KEY_NONOPT) {
		if (!opts->mc_path) {
			char mc_path[PATH_MAX] = "";
			if (realpath(arg, mc_path) == NULL) {
				fuse_log(FUSE_LOG_ERR, "fuse: bad mc file path `%s': %s\n", arg, strerror(errno));
				return -1;
			}
			return fuse_opt_add_opt(&opts->mc_path, mc_path);
		}
		else if (!opts->mountpoint) {
			char mountpoint[PATH_MAX] = "";
			if (realpath(arg, mountpoint) == NULL) {
				fuse_log(FUSE_LOG_ERR, "fuse: bad mount point `%s': %s\n", arg, strerror(errno));
				return -1;
			}
			return fuse_opt_add_opt(&opts->mountpoint, mountpoint);
		}
	}
	return 0;
}


int main(int argc, char** argv) {
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct cli_options opts = {
		.mc_path = NULL,
		.sync_to_fs = 0,

		.mountpoint = NULL,
		.show_help = 0,
		.show_version = 0,
		.singlethread = 0,
		.foreground = 0,
		.max_threads = 16
	};

	if (fuse_opt_parse(&args, &opts, CLI_OPTIONS, opt_proc) == -1)
		return -1;


	// code below is based on the implementation of `fuse_main_real()` from libfuse
	int res;
	struct fuse_loop_config *loop_config = NULL;

	if (opts.show_version) {
		printf("FUSE library version %u.%u\n", FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);
		res = 0;
		goto out1;
	}

	if (opts.show_help) {
		if(args.argv[0][0] != '\0')
			usage(stdout, args.argv[0]);
		//fuse_cmdline_help();
		fuse_lib_help(&args);
		res = 0;
		goto out1;
	}

	if (!opts.mc_path) {
		fprintf(stderr, "fuseps2mc: no memory card file specified\n");
		res = 2;
		goto out1;
	}
	if (!opts.mountpoint) {
		fuse_log(FUSE_LOG_ERR, "error: no mountpoint specified\n");
		res = 2;
		goto out1;
	}

	if (opts.sync_to_fs) {
		vmc_metadata.file = fopen(opts.mc_path, "rb+");
	}
	else {
		FILE* f = fopen(opts.mc_path, "rb+");
		if (!f) {
			fprintf(stderr, "error: could not open file: %s\n", opts.mc_path);
			res = 2;
			goto out1;
		}
		fseek(f, 0, SEEK_END);
		size_t size = ftell(f);
		fseek(f, 0, SEEK_SET);
		vmc_metadata.file = fmemopen(NULL, size, "rb+");

		while (!feof(f)) {
			char buffer[1024];
			fwrite(buffer, 1, fread(buffer, 1, sizeof(buffer), f), vmc_metadata.file);
		}

		fclose(f);
		fseek(vmc_metadata.file, 0, SEEK_SET);
	}
	if (!vmc_metadata.file) {
		fprintf(stderr, "error: could not open file: %s\n", opts.mc_path);
		res = 2;
		goto out1;
	}

	struct fuse* fuse = fuse_new(&args, &operations, sizeof(operations), NULL);
	if (fuse == NULL) {
		res = 3;
		goto out1;
	}

	if (fuse_mount(fuse,opts.mountpoint) != 0) {
		res = 4;
		goto out2;
	}

	if (fuse_daemonize(opts.foreground) != 0) {
		res = 5;
		goto out3;
	}

	struct fuse_session *se = fuse_get_session(fuse);
	if (fuse_set_signal_handlers(se) != 0) {
		res = 6;
		goto out3;
	}

	if (opts.singlethread)
		res = fuse_loop(fuse);
	else {
		loop_config = fuse_loop_cfg_create();
		if (loop_config == NULL) {
			res = 7;
			goto out3;
		}

		fuse_loop_cfg_set_clone_fd(loop_config, 0);

		fuse_loop_cfg_set_idle_threads(loop_config, 100);
		fuse_loop_cfg_set_max_threads(loop_config, opts.max_threads);
		#if FUSE_USE_VERSION < 32
		res = fuse_loop_mt(fuse, loop_config->clone_fd);
		#else
		res = fuse_loop_mt(fuse, loop_config);
		#endif
	}
	if (res)
		res = 8;

	fuse_remove_signal_handlers(se);
out3:
	fuse_unmount(fuse);
out2:
	fuse_destroy(fuse);
out1:
	fuse_loop_cfg_destroy(loop_config);
	free(opts.mountpoint);
	fuse_opt_free_args(&args);

	if (opts.mc_path != NULL)
		free(opts.mc_path);
	if (vmc_metadata.file != NULL)
		fclose(vmc_metadata.file);
	return res;
}

