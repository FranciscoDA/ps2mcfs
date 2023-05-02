# ps2mcfs
![Tests status](https://github.com/franciscoda/ps2mcfs/actions/workflows/test.yml/badge.svg)

FUSE driver for PlayStation 2 Virtual Memory Card (VMC) images. This will allow you to mount VMC images from Open PS2 Loader or PCSX2 in userspace.

Implemented filesystem operations:
 * read
 * stat
 * readdir
 * mkdir
 * utimens
 * create
 * write
 * rmdir
 * unlink
 * rename

The implemented operations allow most read/write commands: mkdir, touch, cat, less, rm, mv, etc.


### Mounting memory card files

The following command can be used to mount a memory card file into a directory mountpoint
```
Usage: bin/fuseps2mc <memory-card-image> <mountpoint> [OPTIONS]
Mounts a Sony PlayStation 2 memory card image as a local filesystem in userspace

fuseps2mcfs options:
    -S                     sync filesystem changes to the memorycard file

Options:
    -h   --help            print help
    -V   --version         print version
    -f                     foreground operation
    ...
```

The only specific flag is `-S` which allows the program to save the filesystem changes into the memory card file.
Please note that ps2mcfs is still in early development, so the use of this flag is discouraged as it may cause file corruption.

Also, some filesystem status considerations:
 * access times are missing (they're not supported by the PS2 filesystem specification). Files will show as being last accessed in Jan 1st of 1970
 * user/group ownership is missing (not supported either). Files will appear as being owned by the same user and group that mounted the filesystem
 * Per-file permissions are supported, but not umasks. Newly created files will appear as having the most permissive combination of permissions from the umask that FUSE provides

### Obtaining memory card files

There are several ways you can obtain or create memory card images:

 * A PS2 virtual memory card image can be obtained from real hardware by storing it into a USB drive using [Open PS2 Loader](https://github.com/ps2homebrew/Open-PS2-Loader) (you will need a PS2 capable of running homebrew applications)
 * A memory card file can be obtained using the PCSX2 emulator, by copying the `.ps2` files from `~/.config/PCSX2/memcards`. It's strongly recommended that you format the memory card using the PS2 browser if it's not already formatted.
 * You can create a formatted memory card file using the `mkfs.ps2` binary included in this project (see below)

The following command can be used to create an empty memory card image:
```
Usage: bin/mkfs.ps2 -o OUTPUT_FILE [-s SIZE] [-e] [-h]
Create a virtual memory card image file.

  -s, --size=NUM        Set the memory card size in megabytes (options: 8)
  -e, --ecc             Add ECC bytes to the generated file
  -o, --output=FILE     Set the output file
  -h, --help            Show this help
```

It's worth noting that PCSX2 `.ps2` files include error correcting codes (ECC data), while Open PS2 Loader `.vmc` files usually don't.

This means that you will want to use the following command to generate memory cards for OPL:
```sh
bin/mkfs.ps2 -o SLES-XXX.vmc -s 8
```
And the following command for PCSX2:
```sh
bin/mkfs.ps2 -o Mcd002.ps2 -s 8 -e
```

### Building

The following packages are needed to build the project in Ubuntu:
* `libfuse3-dev`
* `pkgconf`
* `clang-tools`

Submodules must be initialized and updated to fetch dependencies on external libraries: `git submodule init && git submodule update -f`

The executables can then be built by invoking `make`

### See also

[PlayStation 2 Memory Card File System](http://www.csclub.uwaterloo.ca:11068/mymc/ps2mcfs.html)

[Open PS2 Loader](https://github.com/ps2homebrew/Open-PS2-Loader)

[PS2 Homebrew & emu scene](http://psx-scene.com/forums/ps2-homebrew-dev-emu-scene/)

[PS2 NBD server plugin](https://github.com/bignaux/lwNBD/blob/main/plugins/mcman/lwnbd-mcman-plugin.md) allows accessing VMC files through your PS2

[PS2iconsys](https://github.com/ticky/ps2iconsys) allows converting PS2 icons into their respective geometry and texture files and viceversa

[µnit](https://nemequ.github.io/munit/) Unit testing framework