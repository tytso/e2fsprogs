#
# Makefile for e2fsck
#

include ../MCONFIG

MK_CMDS=	../lib/ss/mk_cmds
CFLAGS=		$(PROF) $(OPT) $(MTRACE) $(MCHECK) $(WFLAGS) -I../lib
LDFLAGS=	$(PROF) $(OPT)
PROGS=		e2fsck flushb
MANPAGES=	e2fsck.8

LIBS= -L../lib -lss -lcom_err -lext2fs $(CHECKLIB)
DEPLIBS= ../lib/libss.a ../lib/libcom_err.a ../lib/libext2fs.a

#
# Flags for using Checker
#	Note: The optimization flags must include -g
#
#MCHECK=	-checker
#LIBS= -L../lib -lss -lcom_err -le2fs $(CHECKLIB)
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
	$(CC) $(LDFLAGS) -static -o e2fsck $(OBJS) $(LIBS) 

flushb: flushb.o
	$(CC) $(LDFLAGS) -o flushb flushb.o $(CHECKLIB)

install:: $(PROGS)
	$(INSTALLBIN) e2fsck $(SBINDIR)/e2fsck
	$(INSTALLBIN) flushb $(USRSBINDIR)/flushb
	ln -sf e2fsck $(SBINDIR)/fsck.ext2

install:: $(MANPAGES)
	for i in $(MANPAGES); do \
		$(INSTALLMAN) $$i $(SMANDIR)/$$i; \
	done

install-tree:: $(PROGS)
	for i in $(PROGS); do \
		rm -f ../bin/$$i; \
		cp $$i ../bin; \
		strip ../bin/$$i; \
		chmod 555 ../bin/$$i; \
	done
	ln -sf e2fsck ../bin/fsck.ext2

clean:
	rm -f $(PROGS) \#* *\# *.s *.o *.a *~ core

really-clean: clean
	rm -f .depend

dep depend .depend:
	$(CPP) $(CFLAGS) -M *.c >.depend

include .depend
