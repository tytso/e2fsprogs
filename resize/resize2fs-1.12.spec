Summary: ext2 filesystem resizer
Name: resize2fs
Version: 1.12
Release: 0
Vendor: PowerQuest
Copyright: Licensed to Registered Users of Partition Magic(tm)
Group: Utilities/System
Requires: e2fsprogs
Source: e2fsprogs-ALL-1.12.tar.gz
Icon: pq.gif
BuildRoot: /tmp/e2fsprogs-root

%description
This program will allow you to enlarge or shrink an ext2 filesystem.

Resize2fs is Copyright 1998 by Theodore Ts'o and PowerQuest, Inc.  All
rights reserved.  Resize2fs may not be redistributed without the prior
consent of PowerQuest.  This version of resize2fs is available to licensed
users of Partition Magic(tm).

%prep
%setup -n e2fsprogs-1.12

%build
CFLAGS="$RPM_OPT_FLAGS" ./configure
rm -rf e2fsck misc debugfs lib/ss lib/e2p lib/uuid tests

make libs progs docs

rm -f resize/resize2fs
mv resize/resize2fs.static resize/resize2fs

%install
export PATH=/sbin:$PATH
make install DESTDIR="$RPM_BUILD_ROOT"
make install-libs DESTDIR="$RPM_BUILD_ROOT"

%clean
rm -rf $RPM_BUILD_ROOT

%files

%attr(-, root, root) /usr/man/man8/resize2fs.8
%attr(-, root, root) /usr/sbin/resize2fs
