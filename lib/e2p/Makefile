# Makefile for the second extended file system utility functions
#
# Copyright (C) 1993 Remy Card (card@masi.ibp.fr)
#
# This file can be redistributed under the terms of the GNU General
# Public License

include ../../MCONFIG

CFLAGS_NO=	$(WFLAGS) -I..
CFLAGS=		$(OPT) $(CFLAGS_NO)
LDFLAGS=	$(OPT)

ARCHIVE=ar r
RANLIB=ranlib
RM=rm -f
MV=mv
LN=ln -s

OBJS=		fgetflags.o fsetflags.o fgetversion.o fsetversion.o \
		getflags.o getversion.o iod.o ls.o pe.o pf.o ps.o \
		setflags.o setversion.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c
	$(CC) $(CFLAGS_NO) -pg -o profiled/$*.o -c $*.c
#	$(CC) $(CFLAGS_NO) -checker -g -o checker/$*.o -c $*.c
 
all: libe2p.a libe2p_p.a

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

install:

clean:
	rm -f \#* *.s *.o *.a *~ *.bak core profiled/* checker/*

really-clean: clean
	rm -f .depend

dep depend .depend:
	$(CC) -M $(CFLAGS) *.c >.depend

include .depend
