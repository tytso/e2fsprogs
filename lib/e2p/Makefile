# Makefile for the second extended file system utility functions
#
# Copyright (C) 1993 Remy Card (card@masi.ibp.fr)
#
# This file can be redistributed under the terms of the GNU General
# Public License

all::

include ../../MCONFIG

CFLAGS_NO=	$(WFLAGS) -I..
CFLAGS=		$(OPT) $(CFLAGS_NO)
LDFLAGS=	$(OPT)

RM=rm -f
MV=mv
LN=ln -s

OBJS=		fgetflags.o fsetflags.o fgetversion.o fsetversion.o \
		getflags.o getversion.o iod.o ls.o pe.o pf.o ps.o \
		setflags.o setversion.o

ifdef BUILD_DLL_SHLIBS
DLL_ADDRESS = 0x66980000
DLL_JUMPSIZE = 0x1000
DLL_GOTSIZE  = 0x1000
DLL_VERSION = 1.0
DLL_IMAGE = libe2p
DLL_STUB = libe2p
DLL_MYDIR = e2p
DLL_INSTALL_DIR = $(SHLIBDIR)

include ../Makefile.dll-lib
endif

.c.o:
	$(CC) $(CFLAGS) -c $*.c
ifdef BUILD_PROFILE_LIBS
	$(CC) $(CFLAGS_NO) -pg -o profiled/$*.o -c $*.c
endif
ifdef BUILD_DLL_SHLIBS
	(export JUMP_DIR=`pwd`/jump; $(CC) -B$(JUMP_PREFIX) $(CFLAGS) \
		-o jump/$*.o -c $*.c)
endif
#	$(CC) $(CFLAGS_NO) -checker -g -o checker/$*.o -c $*.c
 
all:: libe2p.a

ifdef BUILD_PROFILE_LIBS
all:: libe2p_p.a
endif

libe2p.a: $(OBJS)
	$(RM) $@.bak
	-$(MV) $@ $@.bak
	$(ARCHIVE) $@ $(OBJS)
	$(RANLIB) $@
	$(RM) ../$@
	$(LN) e2p/$@ ../$@

libe2p_p.a: $(OBJS)
	$(RM) $@.bak
	-$(MV) $@ $@.bak
	(cd profiled; $(ARCHIVE) ../$@ $(OBJS))
	$(RANLIB) $@
	$(RM) ../$@
	$(LN) e2p/$@ ../$@

libe2p_chk.a: $(OBJS)
	$(RM) $@.bak
	-$(MV) $@ $@.bak
	(cd checker; $(ARCHIVE) ../$@ $(OBJS))
	$(RANLIB) $@
	$(RM) ../$@
	$(LN) e2p/$@ ../$@

install::

install-libs::
	$(INSTALLLIB) libe2p.a $(LIBDIR)/libe2p.a
	$(CHMOD) 644 $(LIBDIR)/libe2p.a
	$(RANLIB) $(LIBDIR)/libe2p.a
	$(CHMOD) $(LIBMODE) $(LIBDIR)/libe2p.a

install-tree::

clean::
	rm -f \#* *.s *.o *.a *~ *.bak core profiled/* checker/*
	rm -f ../libe2p.a ../libe2p_p.a

really-clean:: clean
	rm -f .depend

dep depend .depend:
	$(CC) -M $(CFLAGS) *.c >.depend

include .depend
