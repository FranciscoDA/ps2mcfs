# ps2mcfs
FUSE driver for PlayStation 2 Virtual Memory Card (VMC) images. This will allow you to mount VMC images from Open PS2 Loader or PCSX2 in userspace.

Implemented operations:
 * read
 * stat
 * readdir
 * mkdir
 * utimens
 * create
 * write

The implemented operations allow basic read/write commands: mkdir, touch, cat, less, etc. However, writes are not persistent in the sense that changes are lost when the filesystem is unmounted. It's not yet possible to remove files or directories.

Also, some filesystem status considerations:
 * access times are missing (they're not supported by the filesystem specification). Files will show as being last accessed in Jan 1st of 1970
 * user/group ownership is missing (not supported either). Files will appear as being owned by the same user and group that mounted the filesystem
 * Per-file permissions are supported, but not umasks. Newly created files will appear as having the most permissive combination of permissions from the umask that FUSE provides
 * The timezone for create/modify times for files is always GMT+9:00 (Japan time zone). This is not yet implemented but it is expected to cause issues with some programs. For example, vim will create swap files for a file, warn that the file is already being edited and will not delete the swap files after exit.

```
Usage: ps2mcfs <memory-card-image> <mount-point> [FUSE options]
```

### Building

The following packages are needed to build the project in Ubuntu:
* libfuse3-dev
* pkgconf
* clang-tools

The executable is built with the makefile using `make`

### See also

[PlayStation 2 Memory Card File System](http://www.csclub.uwaterloo.ca:11068/mymc/ps2mcfs.html)

[Open PS2 Loader](https://bitbucket.org/ifcaro/open-ps2-loader/wiki/Home)

[PS2 Homebrew & emu scene](http://psx-scene.com/forums/ps2-homebrew-dev-emu-scene/)

[PS2 NBD server plugin to access VMC files remotely](https://github.com/bignaux/lwNBD/blob/main/plugins/mcman/lwnbd-mcman-plugin.md)

[PS2iconsys](https://github.com/ticky/ps2iconsys) allows converting PS2 icons into their respective geometry and texture files and viceversa