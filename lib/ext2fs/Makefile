all:: libext2fs.a

include ../../MCONFIG

OBJS= ext2_err.o openfs.o freefs.o closefs.o bitmaps.o rw_bitmaps.o inode.o \
	unix_io.o block.o namei.o newdir.o mkdir.o check_desc.o \
	get_pathname.o bitops.o link.o alloc.o expanddir.o inline.o \
	initialize.o badblocks.o read_bb.o bb_inode.o read_bb_file.o llseek.o

HFILES= bitops.h ext2_err.h ext2fs.h io.h

ifdef BUILD_PROFILE_LIBS
all:: libext2fs_p.a
endif

ifdef BUILD_DLL_SHLIBS
DLL_ADDRESS = 0x66900000
DLL_JUMPSIZE = 0x1000
DLL_GOTSIZE  = 0x1000
DLL_VERSION = 1.0
DLL_IMAGE = libe2fs
DLL_STUB = libext2fs
DLL_LIBS = -L../.. -lcom_err
DLL_MYDIR = ext2fs
DLL_INSTALL_DIR = $(SHLIBDIR)

include ../Makefile.dll-lib
endif

COMPILE_ET=../et/compile_et

CFLAGS_NO=	$(WFLAGS) -I..
CFLAGS=		$(OPT) $(CFLAGS_NO)
LDFLAGS=	$(OPT)

RM=rm -f
MV=mv
LN=ln -s

DISTFILES= Makefile *.c *.h image

.c.o:
	$(CC) $(CFLAGS) -c $*.c
ifdef BUILD_PROFILE_LIBS
	$(CC) $(CFLAGS_NO) -pg -o profiled/$*.o -c $*.c
endif
#	$(CC) $(CFLAGS_NO) -checker -g -o checker/$*.o -c $*.c
ifdef BUILD_DLL_SHLIBS
	(export JUMP_DIR=`pwd`/jump; $(CC) -B$(JUMP_PREFIX) $(CFLAGS) \
		-o jump/$*.o -c $*.c)
endif

libext2fs.a: $(OBJS)
	$(RM) $@.bak
	-$(MV) $@ $@.bak
	$(ARCHIVE) $@ $(OBJS)
	$(RANLIB) $@
	$(RM) ../$@
	$(LN) ext2fs/$@ ../$@

libext2fs_p.a: $(OBJS)
	$(RM) $@.bak
	-$(MV) $@ $@.bak
	(cd profiled; $(ARCHIVE) ../$@ $(OBJS))
	$(RANLIB) $@
	$(RM) ../$@
	$(LN) ext2fs/$@ ../$@

libext2fs_chk.a: $(OBJS)
	$(RM) $@.bak
	-$(MV) $@ $@.bak
	(cd checker; $(ARCHIVE) ../$@ $(OBJS))
	$(RANLIB) $@
	$(RM) ../$@
	$(LN) ext2fs/$@ ../$@

ext2_err.c ext2_err.h: ext2_err.et
	$(COMPILE_ET) ext2_err.et

install-libs:: all
	$(INSTALLLIB) libext2fs.a $(LIBDIR)/libext2fs.a
	$(CHMOD) 644 $(LIBDIR)/libext2fs.a
	$(RANLIB) $(LIBDIR)/libext2fs.a
	$(CHMOD) $(LIBMODE) $(LIBDIR)/libext2fs.a

install-libs:: $(HFILES)
	@rm -rf $(INCLDIR)/ext2fs
	@mkdir $(INCLDIR)/ext2fs
	for i in $(HFILES); do \
		$(INSTALLINC) $$i $(INCLDIR)/ext2fs/$$i; \
	done

install-tree::

install::

clean::
	rm -f \#* *.s *.o *.a *~ *.bak core profiled/* checker/*
	rm -f ../libext2fs.a ../libext2fs_p.a ../libext2fs_chk.a

really-clean:: clean
	rm -f .depend ext2_err.c ext2_err.h

dep depend .depend: ext2_err.h
	$(CC) -M $(CFLAGS) *.c >.depend

include .depend
