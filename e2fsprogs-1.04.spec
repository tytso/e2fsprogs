Description: Tools for the second extended (ext2) filesystem 
Name: e2fsprogs
Version: 1.04
Release: 0
Copyright: GPL
Group: Utilities/System
Source: tsx-11.mit.edu:/pub/linux/packages/ext2fs/e2fsprogs-1.04.tar.gz

%package devel
Description: e2fs static libs and headers
Group: Development/Libraries

%prep
%setup

%build

%ifarch i386
CFLAGS="$RPM_OPT_FLAGS" ./configure --enable-profile --enable-elf-shlibs
%endif

%ifarch axp
CFLAGS="$RPM_OPT_FLAGS" ./configure --enable-profile
%endif

#make 
make libs progs

%install
rm -rf /usr/include/ss /usr/include/et /usr/include/ext2fs
export PATH=/sbin:$PATH
make install
make install-libs

mv /usr/sbin/debugfs /sbin/debugfs

%ifarch i386
%post
/sbin/ldconfig
%endif

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

%ifarch i386
/lib/libe2p.so.2.1
/lib/libext2fs.so.2.0
/lib/libss.so.2.0
/lib/libcom_err.so.2.0
%endif

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
/usr/lib/libe2p.a
/usr/lib/libext2fs.a
/usr/lib/libss.a
/usr/lib/libcom_err.a
/usr/lib/libe2p_p.a
/usr/lib/libext2fs_p.a
/usr/lib/libss_p.a
/usr/lib/libcom_err_p.a
/usr/include/ss
/usr/include/ext2fs
/usr/include/et

%ifarch i386
/lib/libe2p.so
/lib/libext2fs.so
/lib/libss.so
/lib/libcom_err.so
%endif
