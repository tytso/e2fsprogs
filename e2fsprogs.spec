Summary: Utilities for managing the second extended (ext2) filesystem.
Name: e2fsprogs
Version: 1.18
Release: 0
Copyright: GPL
Group: System Environment/Base
Source: ftp://tsx-11.mit.edu/pub/linux/packages/ext2fs/e2fsprogs-1.18.tar.gz
BuildRoot: /tmp/e2fsprogs-root

%description
The e2fsprogs package contains a number of utilities for creating,
checking, modifying and correcting any inconsistencies in second
extended (ext2) filesystems.  E2fsprogs contains e2fsck (used to repair
filesystem inconsistencies after an unclean shutdown), mke2fs (used to
initialize a partition to contain an empty ext2 filesystem), debugfs
(used to examine the internal structure of a filesystem, to manually
repair a corrupted filesystem or to create test cases for e2fsck), tune2fs
(used to modify filesystem parameters) and most of the other core ext2fs
filesystem utilities.

You should install the e2fsprogs package if you are using any ext2
filesystems (if you're not sure, you probably should install this
package).

%package devel
Summary: Ext2 filesystem-specific static libraries and headers.
Group: Development/Libraries
Requires: e2fsprogs

%description devel 
E2fsprogs-devel contains the libraries and header files needed to
develop second extended (ext2) filesystem-specific programs.

You should install e2fsprogs-devel if you want to develop ext2
filesystem-specific programs.  If you install e2fsprogs-devel, you will
also need to install e2fsprogs.

%prep
%setup

%build
CFLAGS="$RPM_OPT_FLAGS" ./configure --enable-elf-shlibs

make libs progs docs

%install
export PATH=/sbin:$PATH
make install DESTDIR="$RPM_BUILD_ROOT"
make install-libs DESTDIR="$RPM_BUILD_ROOT"

%clean
rm -rf $RPM_BUILD_ROOT

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%files
%attr(-, root, root) %doc README RELEASE-NOTES
%attr(-, root, root) /sbin/e2fsck
%attr(-, root, root) /sbin/e2label
%attr(-, root, root) /sbin/fsck.ext2
%attr(-, root, root) /sbin/debugfs
%attr(-, root, root) /sbin/mke2fs
%attr(-, root, root) /sbin/badblocks
%attr(-, root, root) /sbin/tune2fs
%attr(-, root, root) /sbin/dumpe2fs
%attr(-, root, root) /sbin/fsck
%attr(-, root, root) /usr/sbin/mklost+found
%attr(-, root, root) /sbin/mkfs.ext2

%attr(-, root, root) /lib/libe2p.so.2.3
%attr(-, root, root) /lib/libext2fs.so.2.4
%attr(-, root, root) /lib/libss.so.2.0
%attr(-, root, root) /lib/libcom_err.so.2.0
%attr(-, root, root) /lib/libuuid.so.1.2

%attr(-, root, root) /usr/bin/chattr
%attr(-, root, root) /usr/bin/lsattr
%attr(-, root, root) /usr/bin/uuidgen
%attr(-, root, root) /usr/man/man8/e2fsck.8
%attr(-, root, root) /usr/man/man8/e2label.8
%attr(-, root, root) /usr/man/man8/debugfs.8
%attr(-, root, root) /usr/man/man8/tune2fs.8
%attr(-, root, root) /usr/man/man8/mklost+found.8
%attr(-, root, root) /usr/man/man8/mke2fs.8
%attr(-, root, root) /usr/man/man8/dumpe2fs.8
%attr(-, root, root) /usr/man/man8/badblocks.8
%attr(-, root, root) /usr/man/man8/fsck.8
%attr(-, root, root) /usr/man/man1/chattr.1
%attr(-, root, root) /usr/man/man1/lsattr.1
%attr(-, root, root) /usr/man/man1/uuidgen.1

%files devel
%attr(-, root, root) /usr/info/libext2fs.info*
%attr(-, root, root) /usr/lib/libe2p.a
%attr(-, root, root) /usr/lib/libext2fs.a
%attr(-, root, root) /usr/lib/libss.a
%attr(-, root, root) /usr/lib/libcom_err.a
%attr(-, root, root) /usr/lib/libuuid.a
%attr(-, root, root) /usr/include/ss
%attr(-, root, root) /usr/include/ext2fs
%attr(-, root, root) /usr/include/et
%attr(-, root, root) /usr/include/uuid
%attr(-, root, root) /usr/lib/libe2p.so
%attr(-, root, root) /usr/lib/libext2fs.so
%attr(-, root, root) /usr/lib/libss.so
%attr(-, root, root) /usr/lib/libcom_err.so
%attr(-, root, root) /usr/lib/libuuid.so
%attr(-, root, root) /usr/bin/mk_cmds
%attr(-, root, root) /usr/bin/compile_et
%attr(-, root, root) /usr/share/et/et_c.awk
%attr(-, root, root) /usr/share/et/et_h.awk
%attr(-, root, root) /usr/share/ss/ct_c.awk
%attr(-, root, root) /usr/share/ss/ct_c.sed
%attr(-, root, root) /usr/man/man1/compile_et.1
%attr(-, root, root) /usr/man/man3/com_err.3

