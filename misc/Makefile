include ../MCONFIG

CFLAGS=		$(OPT) $(WFLAGS) -I../lib
LDFLAGS=	$(OPT)
SPROGS=		mke2fs badblocks tune2fs dumpe2fs fsck
USPROGS=	mklost+found
SMANPAGES=	tune2fs.8 mklost+found.8 mke2fs.8 dumpe2fs.8 badblocks.8 \
			fsck.8

UPROGS=		chattr lsattr
UMANPAGES=	chattr.1 lsattr.1

TUNE2FS_OBJS=	tune2fs.o
MKLPF_OBJS=	mklost+found.o
MKE2FS_OBJS=	mke2fs.o
CHATTR_OBJS=	chattr.o
LSATTR_OBJS=	lsattr.o
DUMPE2FS_OBJS=	dumpe2fs.o
BADBLOCKS_OBJS=	badblocks.o
FSCK_OBJS=	fsck.o

LIBS= -L../lib -lext2fs -le2p -lcom_err 
DEPLIBS= ../lib/libext2fs.a ../lib/libe2p.a ../lib/libcom_err.a 

all: $(SPROGS) $(UPROGS) $(USPROGS)

tune2fs: $(TUNE2FS_OBJS) $(DEPLIBS)
	$(CC) $(LDFLAGS) -o tune2fs $(TUNE2FS_OBJS) $(LIBS)

mklost+found: $(MKLPF_OBJS)
	$(CC) $(LDFLAGS) -o mklost+found $(MKLPF_OBJS)

mke2fs: $(MKE2FS_OBJS) $(DEPLIBS)
	$(CC) $(LDFLAGS) -o mke2fs $(MKE2FS_OBJS) $(LIBS)

mke2fs.static: $(MKE2FS_OBJS) $(DEPLIBS)
	$(CC) $(LDFLAGS) -static -o mke2fs.static $(MKE2FS_OBJS) $(LIBS)

chattr: $(CHATTR_OBJS) $(DEPLIBS)
	$(CC) $(LDFLAGS) -o chattr $(CHATTR_OBJS) $(LIBS)

lsattr: $(LSATTR_OBJS) $(DEPLIBS)
	$(CC) $(LDFLAGS) -o lsattr $(LSATTR_OBJS) $(LIBS)

dumpe2fs: $(DUMPE2FS_OBJS) $(DEPLIBS)
	$(CC) $(LDFLAGS) -o dumpe2fs $(DUMPE2FS_OBJS) $(LIBS)

badblocks: $(BADBLOCKS_OBJS) $(DEPLIBS)
	$(CC) $(LDFLAGS) -o badblocks $(BADBLOCKS_OBJS) $(LIBS)

install:: $(SPROGS) $(UPROGS)
	for i in $(SPROGS); do \
		$(INSTALLBIN) $$i $(SBINDIR)/$$i; \
	done
	for i in $(USPROGS); do \
		$(INSTALLBIN) $$i $(USRSBINDIR)/$$i; \
	done
	ln -sf mke2fs $(SBINDIR)/mkfs.ext2
	for i in $(UPROGS); do \
		$(INSTALLBIN) $$i $(USRBINDIR)/$$i; \
	done

install:: $(SMANPAGES) $(UMANPAGES)
	for i in $(SMANPAGES); do \
		$(INSTALLMAN) $$i $(SMANDIR)/$$i; \
	done
	for i in $(UMANPAGES); do \
		$(INSTALLMAN) $$i $(UMANDIR)/$$i; \
	done

install-tree:: $(SPROGS) $(UPROGS)
	for i in $(SPROGS) $(UPROGS); do \
		rm -f ../bin/$$i; \
		cp $$i ../bin; \
		strip ../bin/$$i; \
		chmod 555 ../bin/$$i; \
	done
	ln -sf mke2fs ../bin/mkfs.ext2

clean:
	rm -f $(SPROGS) $(USPROGS) $(UPROGS) mke2fs.static \
		\#* *.s *.o *.a *~ core

really-clean: clean
	rm -f .depend 

dep depend .depend:
	$(CPP) $(CFLAGS) -M *.c >.depend

include .depend
