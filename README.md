HEPunion File System
========

The HEPunion File System is a file system specifically developped for the CERN LHCb computing farm.

It is an union file system which is designed for merging two different directories, one in read-only and the second in read-write. It implements
basic features of union file systems, like Copy-On-Write (COW), whiteouts. Because it was required that it has a low amount of copyups, it also
implements features such as: copyups deletion on file merge, metadata separated handling for copyups.

It has been designed for low memory consumption and implementation without any change to the Linux kernel, with a pseudo file system stacking relying on VFS.
Its original target was SLC5 kernel.

It is currently being ported to 3.8 kernel (for SLC6 through ELrepo) thanks to a GSoC project.

It is HIGHLY experimental at the moment, uncomplete and will break your system. Do not use unless you know what you do. Use it in a VM that you can restore.

Reference:
SCHWEITZER P., BONACORSSI E., BRARDA L., NEUFELD N., « Fabric Management with diskless Servers and Quattor in LHCb », proceedings of ICALEPCS11 conference, 10-14 October, 2011, Grenoble, France, pp. 691-693

