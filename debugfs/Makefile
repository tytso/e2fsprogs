include ../MCONFIG

CFLAGS=		$(OPT) -Wall -I../lib
LDFLAGS=	$(OPT)
PROGS=		debugfs
BINDIR=		$(USRSBINDIR)
MANPAGES=	debugfs.8
MANDIR=		$(SMANDIR)

MK_CMDS=	../lib/ss/mk_cmds

DEBUG_OBJS= debug_cmds.o debugfs.o util.o ncheck.o icheck.o

LIBS= -L../lib -lss -lcom_err -lext2fs
DEPLIBS= ../lib/libss.a ../lib/libcom_err.a ../lib/libext2fs.a

debugfs: $(DEBUG_OBJS) $(DEPLIBS)
	cc $(LDFLAGS) -o debugfs $(DEBUG_OBJS) $(LIBS)

debug_cmds.c debug_cmds.h: debug_cmds.ct
	$(MK_CMDS) debug_cmds.ct

install:: $(PROGS)
	for i in $(PROGS); do \
		$(INSTALLBIN) $$i $(BINDIR)/$$i; \
	done

install:: $(MANPAGES)
	for i in $(MANPAGES); do \
		$(INSTALLMAN) $$i $(MANDIR)/$$i; \
	done

clean:
	rm -f debugfs \#* *.s *.o *.a *~ debug_cmds.c core

really-clean: clean
	rm -f debug_cmds.c .depend 

dep depend .depend:
	$(CPP) $(CFLAGS) -M *.c >.depend

include .depend
