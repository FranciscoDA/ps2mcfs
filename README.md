# ps2mcfs
FUSE driver for PlayStation 2 Virtual Memory Card (VMC) images. This will allow you to mount Open PS2 Loader's VMC images in userspace.

Currently, only read operations are implemented (stat, readdir, read)

    Usage: ps2mcfs <memory-card-image> <mount-point> [FUSE options]

See also:

[PlayStation 2 Memory Card File System](http://www.csclub.uwaterloo.ca:11068/mymc/ps2mcfs.html)

[Open PS2 Loader](https://bitbucket.org/ifcaro/open-ps2-loader/wiki/Home)

[PS2 Homebrew & emu scene](http://psx-scene.com/forums/ps2-homebrew-dev-emu-scene/)
