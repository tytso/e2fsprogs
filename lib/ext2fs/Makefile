include ../../MCONFIG

COMPILE_ET=../et/compile_et

CFLAGS_NO=	$(WFLAGS) -I..
CFLAGS=		$(OPT) $(CFLAGS_NO)
LDFLAGS=	$(OPT)

ARCHIVE=ar r
RANLIB=ranlib
RM=rm -f
MV=mv
LN=ln -s

OBJS= ext2_err.o openfs.o freefs.o closefs.o bitmaps.o inode.o unix_io.o \
	block.o namei.o newdir.o mkdir.o \
	get_pathname.o bitops.o link.o alloc.o expanddir.o inline.o \
	initialize.o badblocks.o read_bb.o bb_inode.o read_bb_file.o

HFILES= bitops.h ext2_err.h ext2fs.h io.h

DISTFILES= Makefile *.c *.h image

.c.o:
	$(CC) $(CFLAGS) -c $*.c
	$(CC) $(CFLAGS_NO) -pg -o profiled/$*.o -c $*.c
#	$(CC) $(CFLAGS_NO) -checker -g -o checker/$*.o -c $*.c

all: libext2fs.a libext2fs_p.a 

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

install:: all
	$(INSTALLLIB) libext2fs.a ${DESTDIR}$(LIBDIR)/libext2fs.a
	$(CHMOD) 644 ${DESTDIR}$(LIBDIR)/libext2fs.a
	$(RANLIB) ${DESTDIR}$(LIBDIR)/libext2fs.a
	$(CHMOD) $(LIBMODE) ${DESTDIR}$(LIBDIR)/libext2fs.a

install:: $(HFILES)
	@rm -rf ${DESTDIR}$(INCLDIR)/ext2fs
	@mkdir ${DESTDIR}$(INCLDIR)/ext2fs
	for i in $(HFILES); do \
		$(INSTALLINC) $$i ${DESTDIR}$(INCLDIR)/ext2fs/$$i; \
	done

clean:
	rm -f \#* *.s *.o *.a *~ *.bak core profiled/* checker/*

really-clean: clean
	rm -f .depend ext2_err.c ext2_err.h

dep depend .depend: ext2_err.h
	$(CC) -M $(CFLAGS) *.c >.depend

include .depend
