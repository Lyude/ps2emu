What is this?
=============

This is a set of userspace tools for recording the data going to/from PS/2
devices in the Linux kernel. This can be very useful when developing input
drivers for devices we may not have, or fixing bugs that we can't normally
reproduce on our machines. Replaying of devices requires [the ps2emu kernel
module](https://github.com/Lyude/ps2emu-kmod) be installed on your machine.
Although it's included in this repository as a submodule, the version in this
repository might not be as up to date and as such it's recommended that you
clone and build it straight from it's own git repository.

How to build
============

Building the userspace tools should be as simple as

```
./autogen.sh
make
```

From there, you can record ps/2 devices using the ps2emu-record application,
and replay them using the kernel module and the ps2emu-replay application.
