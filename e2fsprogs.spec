Summary: Utilities for managing the second extended (ext2) filesystem.
Name: e2fsprogs
Version: 1.32
Release: 0
Copyright: GPL
Group: System Environment/Base
Buildroot: /var/tmp/%{name}-root
Source: ftp://download.sourceforge.net/pub/sourceforge/e2fsprogs/e2fsprogs-%{version}.tar.gz
Prereq: /sbin/ldconfig

%description
The e2fsprogs package contains a number of utilities for creating,
checking, modifying and correcting any inconsistencies in second
extended (ext2) filesystems.  E2fsprogs contains e2fsck (used to repair
filesystem inconsistencies after an unclean shutdown), mke2fs (used to
initialize a partition to contain an empty ext2 filesystem), debugfs
(used to examine the internal structure of a filesystem, to manually
repair a corrupted filesystem or to create test cases for e2fsck), tune2fs
(used to modify filesystem parameters), resize2fs to grow and shrink
unmounted ext2 filesystems, and most of the other core ext2fs filesystem
utilities.

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
filesystem-specific programs.  If you install e2fsprogs-devel, you'll
also need to install e2fsprogs.

%prep
%setup

%build
CFLAGS="$RPM_OPT_FLAGS" ; export CFLAGS
./configure --enable-elf-shlibs --mandir=%{_mandir} --infodir=%{_infodir}

make libs progs docs

%install
rm -rf $RPM_BUILD_ROOT
export PATH=/sbin:$PATH
make install install-libs DESTDIR="$RPM_BUILD_ROOT"

%clean
rm -rf $RPM_BUILD_ROOT

%post
/sbin/ldconfig
# Remove possibly old version
/bin/rm -f /usr/sbin/resize2fs

%postun -p /sbin/ldconfig

%post devel
if [ -x /sbin/install-info ]; then
   /sbin/install-info %{_infodir}/libext2fs.info.gz %{_infodir}/dir
fi

%postun devel
if [ $1 = 0 -a -x /sbin/install-info ]; then
   /sbin/install-info --delete %{_infodir}/libext2fs.info.gz %{_infodir}/dir
fi

%files
%defattr(-,root,root)
%doc README RELEASE-NOTES

/sbin/badblocks
/sbin/debugfs
/sbin/dumpe2fs
/sbin/e2fsck
/sbin/e2label
/sbin/e2image
/sbin/fsck
/sbin/fsck.ext2
/sbin/fsck.ext3
/sbin/mke2fs
/sbin/mkfs.ext2
/sbin/mkfs.ext3
/sbin/tune2fs
/sbin/resize2fs
/usr/sbin/mklost+found

/lib/libcom_err.so.*
/lib/libe2p.so.*
/lib/libext2fs.so.*
/lib/libss.so.*
/lib/libuuid.so.*

/usr/bin/chattr
/usr/bin/lsattr
/usr/bin/uuidgen
%{_mandir}/man1/chattr.1*
%{_mandir}/man1/lsattr.1*
%{_mandir}/man1/uuidgen.1*

%{_mandir}/man8/badblocks.8*
%{_mandir}/man8/debugfs.8*
%{_mandir}/man8/dumpe2fs.8*
%{_mandir}/man8/e2fsck.8*
%{_mandir}/man8/e2label.8*
%{_mandir}/man8/e2image.8*
%{_mandir}/man8/fsck.8*
%{_mandir}/man8/mke2fs.8*
%{_mandir}/man8/mklost+found.8*
%{_mandir}/man8/resize2fs.8*
%{_mandir}/man8/tune2fs.8*

%files devel
%defattr(-,root,root)
%{_infodir}/libext2fs.info*
/usr/bin/compile_et
/usr/bin/mk_cmds

/usr/lib/libcom_err.a
/usr/lib/libcom_err.so
/usr/lib/libe2p.a
/usr/lib/libe2p.so
/usr/lib/libext2fs.a
/usr/lib/libext2fs.so
/usr/lib/libss.a
/usr/lib/libss.so
/usr/lib/libuuid.a
/usr/lib/libuuid.so

/usr/share/et
/usr/share/ss
/usr/include/et
/usr/include/ext2fs
/usr/include/ss
/usr/include/uuid
%{_mandir}/man1/compile_et.1*
%{_mandir}/man3/com_err.3*
%{_mandir}/man3/libuuid.3*
%{_mandir}/man3/uuid*.3*
