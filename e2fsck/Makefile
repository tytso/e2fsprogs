#
# Makefile for e2fsck
#

include ../MCONFIG

MK_CMDS=	../lib/ss/mk_cmds
CFLAGS=		$(PROF) $(OPT) $(MTRACE) $(MCHECK) $(WFLAGS) -I../lib
LDFLAGS=	$(PROF) $(OPT)
PROGS=		e2fsck flushb
MANPAGES=	e2fsck.8
BINDIR=		$(SBINDIR)
MANDIR=		$(SMANDIR)

LIBS= -L../lib -lss -lcom_err -lext2fs $(CHECKLIB)
DEPLIBS= ../lib/libss.a ../lib/libcom_err.a ../lib/libext2fs.a

#
# Flags for using Checker
#	Note: The optimization flags must include -g
#
#MCHECK=	-checker
#LIBS= -L../lib -lss -lcom_err -lext2fs $(CHECKLIB)
#DEPLIBS= ../lib/libss.a ../lib/libcom_err.a ../lib/libext2fs.a
#CHECKLIB= /usr/lib/libchecker.o

#
# Flags for doing mtrace --- uncomment to produce mtracing e2fsck
# 	Note:  The optimization flags must include -g
#
#MTRACE=	-DMTRACE
#MTRACE_OBJ= mtrace.o
#OPT= -g

#
# Flags for doing mcheck --- uncomment to produce mchecking e2fsck
# 	Note:  The optimization flags must include -g
#
#MCHECK= -DMCHECK

#
# Flags for profiling --- uncomment to produce profiling e2fsck
#
#PROF=		-pg
#LIBS= -L../lib -lss -lcom_err_p -lext2fs_p 
#DEPLIBS= ../lib/libss.a ../lib/libcom_err_p.a ../lib/libext2fs_p.a

OBJS= e2fsck.o pass1.o pass1b.o pass2.o pass3.o pass4.o pass5.o \
	badblocks.o util.o dirinfo.o ehandler.o $(MTRACE_OBJ)

all: $(PROGS)

#e2fsck: $(OBJS)  $(DEPLIBS)
#	cc $(LDFLAGS) -o e2fsck $(OBJS) $(LIBS) 

e2fsck: $(OBJS)  $(DEPLIBS)
	cc $(LDFLAGS) -static -o e2fsck $(OBJS) $(LIBS) 

flushb: flushb.o
	cc $(LDFLAGS) -o flushb flushb.o $(CHECKLIB)

install:: $(PROGS)
	for i in $(PROGS); do \
		$(INSTALLBIN) $$i $(BINDIR)/$$i; \
	done
	ln -sf e2fsck $(BINDIR)/fsck.ext2

install:: $(MANPAGES)
	for i in $(MANPAGES); do \
		$(INSTALLMAN) $$i $(MANDIR)/$$i; \
	done

clean:
	rm -f $(PROGS) \#* *\# *.s *.o *.a *~ core

really-clean:
	rm -f .depend

dep depend .depend:
	$(CPP) $(CFLAGS) -M *.c >.depend

include .depend
