Summary: Tools for the second extended (ext2) filesystem 
Name: e2fsprogs
Version: 1.07
Release: 0
Copyright: GPL
Group: Utilities/System
Source: tsx-11.mit.edu:/pub/linux/packages/ext2fs/e2fsprogs-1.07.tar.gz
BuildRoot: /tmp/e2fsprogs-root

%description
This package includes a number of utilities for creating, checking,
and repairing ext2 filesystems.

%package devel
Summary: e2fs static libs and headers
Group: Development/Libraries

%description devel 
Libraries and header files needed to develop ext2 filesystem-specific
programs.

%prep
%setup

%build
CFLAGS="$RPM_OPT_FLAGS" ./configure --enable-elf-shlibs

make libs progs docs

%install
export PATH=/sbin:$PATH
make install DESTDIR="$RPM_BUILD_ROOT"
make install-libs DESTDIR="$RPM_BUILD_ROOT"

mv $RPM_BUILD_ROOT/usr/sbin/debugfs $RPM_BUILD_ROOT/sbin/debugfs

%clean
rm -rf $RPM_BUILD_ROOT

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%files
/sbin/e2fsck
/sbin/fsck.ext2
/usr/man/man8/e2fsck.8
/sbin/debugfs
/usr/man/man8/debugfs.8
/sbin/mke2fs
/sbin/badblocks
/sbin/tune2fs
/sbin/dumpe2fs
/sbin/fsck
/usr/sbin/mklost+found
/sbin/mkfs.ext2

/lib/libe2p.so.2.2
/lib/libext2fs.so.2.2
/lib/libss.so.2.0
/lib/libcom_err.so.2.0
/lib/libuuid.so.1.1

/usr/bin/chattr
/usr/bin/lsattr
/usr/man/man8/tune2fs.8
/usr/man/man8/mklost+found.8
/usr/man/man8/mke2fs.8
/usr/man/man8/dumpe2fs.8
/usr/man/man8/badblocks.8
/usr/man/man8/fsck.8
/usr/man/man1/chattr.1
/usr/man/man1/lsattr.1

%files devel
/usr/info/libext2fs.info*
/usr/lib/libe2p.a
/usr/lib/libext2fs.a
/usr/lib/libss.a
/usr/lib/libcom_err.a
/usr/lib/libuuid.a
/usr/include/ss
/usr/include/ext2fs
/usr/include/et
/usr/include/uuid
/usr/lib/libe2p.so
/usr/lib/libext2fs.so
/usr/lib/libss.so
/usr/lib/libcom_err.so
/usr/lib/libuuid.so

