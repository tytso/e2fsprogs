%define	_root_sbindir	/sbin
%define	_root_libdir	/%{_lib}
%define	_root_localedir	/usr/share/locale
%define	_root_etcdir	/etc

Summary: Utilities for managing ext2/ext3/ext4 filesystems
Name: e2fsprogs
Version: 1.47.2
Release: 0
License: GPLv2
Group: System Environment/Base
Source:  ftp://download.sourceforge.net/pub/sourceforge/e2fsprogs/e2fsprogs-%{version}.tar.gz
Url: http://e2fsprogs.sourceforge.net/
Prereq: /sbin/ldconfig
BuildRoot: %{_tmppath}/%{name}-root

%description
The e2fsprogs package contains a number of utilities for creating,
checking, modifying, and correcting any inconsistencies in ext2, ext3,
and ext4 filesystems.  E2fsprogs contains e2fsck (used to repair
filesystem inconsistencies after an unclean shutdown), mke2fs (used to
initialize a partition to contain an empty ext2 filesystem), debugfs
(used to examine the internal structure of a filesystem, to manually
repair a corrupted filesystem or to create test cases for e2fsck),
tune2fs (used to modify filesystem parameters), resize2fs to grow and
shrink unmounted ext2 filesystems, and most of the other core ext2fs
filesystem utilities.

You should install the e2fsprogs package if you are using any ext2,
ext3, or ext4 filesystems (if you're not sure, you probably should
install this package).  You may also need to install it (even if you
don't use ext2/ext3/ext4) for the libuuid and libblkid libraries and
fsck tool that are included here.

%package devel
Summary: Ext2 filesystem-specific static libraries and headers.
Group: Development/Libraries
Requires: e2fsprogs = %{version}
Prereq: /sbin/install-info

%description devel
E2fsprogs-devel contains the libraries and header files needed to
develop ext2, ext3, or ext4 filesystem-specific programs.

You should install e2fsprogs-devel if you want to develop
ext2. ext3. or ext4 filesystem-specific programs.  If you install
e2fsprogs-devel, you'll also want to install e2fsprogs.

%package -n uuidd
Summary: helper daemon to guarantee uniqueness of time-based UUIDs
Group: System Environment/Daemons
License: GPLv2
Requires: e2fsprogs = %{version}
Requires(pre): shadow-utils

%description -n uuidd
The uuidd package contains a userspace daemon (uuidd) which guarantees
uniqueness of time-based UUID generation even at very high rates on
SMP systems.

%prep
%setup

%build
%configure --enable-elf-shlibs --enable-nls \
	%{?extra_config_flags:%extra_config_flags}
make
make check

%install
rm -rf $RPM_BUILD_ROOT
export PATH=/sbin:$PATH
make install install-libs DESTDIR="$RPM_BUILD_ROOT" \
	root_sbindir=%{_root_sbindir} root_libdir=%{_root_libdir}
/sbin/ldconfig -n ${RPM_BUILD_ROOT}%{_libdir}

# Add a dir that uuidd needs that the Makefiles don't create
install -d $RPM_BUILD_ROOT/var/lib/libuuid

%find_lang %{name}

%clean
rm -rf $RPM_BUILD_ROOT

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%post devel
if [ -x /sbin/install-info -a -f %{_infodir}/libext2fs.info.gz ]; then
    /sbin/install-info %{_infodir}/libext2fs.info.gz %{_infodir}/dir
fi
exit 0

%postun devel
if [ $1 = 0 -a -x /sbin/install-info -a -f %{_infodir}/libext2fs.info.gz ]; then
   /sbin/install-info --delete %{_infodir}/libext2fs.info.gz %{_infodir}/dir
fi
exit 0

%pre -n uuidd
getent group uuidd >/dev/null || groupadd -r uuidd
getent passwd uuidd >/dev/null || \
useradd -r -g uuidd -d /var/lib/libuuid -s /sbin/nologin \
    -c "UUID generator helper daemon" uuidd
exit 0

%files -f %{name}.lang
%defattr(-,root,root)
%doc README RELEASE-NOTES

%{_root_sbindir}/badblocks
%{_root_sbindir}/blkid
%{_root_sbindir}/debugfs
%{_root_sbindir}/dumpe2fs
%{_root_sbindir}/e2fsck
%{_root_sbindir}/e2image
%{_root_sbindir}/e2label
%{_root_sbindir}/e2mmpstatus
%{_root_sbindir}/e2undo
%{_root_sbindir}/findfs
%{_root_sbindir}/fsck
%{_root_sbindir}/fsck.ext2
%{_root_sbindir}/fsck.ext3
%{_root_sbindir}/fsck.ext4
%{_root_sbindir}/fsck.ext4dev
%{_root_sbindir}/logsave
%{_root_sbindir}/mke2fs
%{_root_etcdir}/mke2fs.conf
%{_root_sbindir}/mkfs.ext2
%{_root_sbindir}/mkfs.ext3
%{_root_sbindir}/mkfs.ext4
%{_root_sbindir}/mkfs.ext4dev
%{_root_sbindir}/resize2fs
%{_root_sbindir}/tune2fs
%{_sbindir}/filefrag
%{_sbindir}/mklost+found
%{_sbindir}/e2freefrag

%{_root_libdir}/libblkid.so.*
%{_root_libdir}/libcom_err.so.*
%{_root_libdir}/libe2p.so.*
%{_root_libdir}/libext2fs.so.*
%{_root_libdir}/libss.so.*
%{_root_libdir}/libuuid.so.*

%{_libdir}/e2initrd_helper

%{_bindir}/chattr
%{_bindir}/lsattr
%{_bindir}/uuidgen
%{_mandir}/man1/chattr.1*
%{_mandir}/man1/lsattr.1*
%{_mandir}/man1/uuidgen.1*

%{_mandir}/man5/e2fsck.conf.5*
%{_mandir}/man5/mke2fs.conf.5*

%{_mandir}/man8/badblocks.8*
%{_mandir}/man8/blkid.8*
%{_mandir}/man8/debugfs.8*
%{_mandir}/man8/dumpe2fs.8*
%{_mandir}/man8/e2fsck.8*
%{_mandir}/man8/findfs.8*
%{_mandir}/man8/fsck.ext2.8*
%{_mandir}/man8/fsck.ext3.8*
%{_mandir}/man8/fsck.ext4.8*
%{_mandir}/man8/fsck.ext4dev.8*
%{_mandir}/man8/e2image.8*
%{_mandir}/man8/e2label.8*
%{_mandir}/man8/e2mmpstatus.8*
%{_mandir}/man8/e2undo.8*
%{_mandir}/man8/fsck.8*
%{_mandir}/man8/logsave.8*
%{_mandir}/man8/mke2fs.8*
%{_mandir}/man8/mkfs.ext2.8*
%{_mandir}/man8/mkfs.ext3.8*
%{_mandir}/man8/mkfs.ext4.8*
%{_mandir}/man8/mkfs.ext4dev.8*
%{_mandir}/man8/mklost+found.8*
%{_mandir}/man8/resize2fs.8*
%{_mandir}/man8/tune2fs.8*
%{_mandir}/man8/filefrag.8*
%{_mandir}/man8/e2freefrag.8*

%files devel
%defattr(-,root,root)
%{_infodir}/libext2fs.info*
%{_bindir}/compile_et
%{_bindir}/mk_cmds

%{_libdir}/libblkid.a
%{_libdir}/libblkid.so
%{_libdir}/libcom_err.a
%{_libdir}/libcom_err.so
%{_libdir}/libe2p.a
%{_libdir}/libe2p.so
%{_libdir}/libext2fs.a
%{_libdir}/libext2fs.so
%{_libdir}/libss.a
%{_libdir}/libss.so
%{_libdir}/libuuid.a
%{_libdir}/libuuid.so

%{_libdir}/pkgconfig/blkid.pc
%{_libdir}/pkgconfig/com_err.pc
%{_libdir}/pkgconfig/e2p.pc
%{_libdir}/pkgconfig/ext2fs.pc
%{_libdir}/pkgconfig/ss.pc
%{_libdir}/pkgconfig/uuid.pc

%{_datadir}/et
%{_datadir}/ss
%{_includedir}/blkid
%{_includedir}/e2p
%{_includedir}/et
%{_includedir}/com_err.h
%{_includedir}/ext2fs
%{_includedir}/ss
%{_includedir}/uuid
%{_mandir}/man1/compile_et.1*
%{_mandir}/man1/mk_cmds.1*
%{_mandir}/man3/com_err.3*
%{_mandir}/man3/libblkid.3*
%{_mandir}/man3/uuid.3*
%{_mandir}/man3/uuid_clear.3*
%{_mandir}/man3/uuid_compare.3*
%{_mandir}/man3/uuid_copy.3*
%{_mandir}/man3/uuid_generate.3*
%{_mandir}/man3/uuid_generate_random.3*
%{_mandir}/man3/uuid_generate_time.3*
%{_mandir}/man3/uuid_is_null.3*
%{_mandir}/man3/uuid_parse.3*
%{_mandir}/man3/uuid_time.3*
%{_mandir}/man3/uuid_unparse.3*

%files -n uuidd
%defattr(-,root,root)
# if you want to run via init
# /etc/init.d/uuidd
%{_mandir}/man8/uuidd.8*
%attr(6755, uuidd, uuidd) %{_sbindir}/uuidd
%dir %attr(2775, uuidd, uuidd) /var/lib/libuuid

