#
# Makefile for lib/ss
#

include ../../MCONFIG

ifdef BUILD_DLL_SHLIBS
DLL_ADDRESS = 0x66880000
DLL_JUMPSIZE = 0x1000
DLL_GOTSIZE  = 0x1000
DLL_VERSION = 1.0
DLL_IMAGE = libss
DLL_STUB = libss
DLL_LIBS = -L../.. -lcom_err
DLL_MYDIR = ss
DLL_INSTALL_DIR = $(SHLIBDIR)
endif

RM=rm -f
MV=mv
LN=ln -s
TAGS=etags
COMPILE_ET=../et/compile_et
MK_CMDS=../ss/mk_cmds

DEFS= -DWAIT_USES_INT -DHAS_STDLIB_H -DHAS_UNISTD_H -DPOSIX_SIGNALS

# hard coded .. is so that ss/ss_err.h works
# hard coded ../et is so com_err.h works
CFLAGS= -I. -I.. -I../et $(OPT) $(WFLAGS) $(DEFS)

# hard coded for target install
srcdir=	.

# for the library

LIB=	libss.a

# with ss_err.o first, ss_err.h should get rebuilt first too.  should not
# be relying on this, though.
OBJS=	ss_err.o \
	std_rqs.o \
	invocation.o help.o \
	execute_cmd.o listen.o parse.o error.o prompt.o \
	request_tbl.o list_rqs.o pager.o requests.o \
	data.o

SRCS=	invocation.c help.c \
	execute_cmd.c listen.c parse.c error.c prompt.c \
	request_tbl.c list_rqs.c pager.c requests.c \
	data.c  \
	ss_err.h
# ss_err.h here, so that make depend catches it.

ifdef BUILD_DLL_SHLIBS
include ../Makefile.dll-lib
endif

CODE= $(SRCS) $(MKCMDSFILES)

MKCMDSOBJS=	mk_cmds.o utils.o options.o ct.tab.o cmd_tbl.lex.o

MKCMDSFILES=	mk_cmds.c utils.c options.c ct.y cmd_tbl.lex.l

MKCMDSCSRCS=	mk_cmds.c utils.c options.c ct.tab.c cmd_tbl.lex.c


HFILES=	ss.h ss_internal.h copyright.h

# for 'tags' and dependencies

CFILES=	$(SRCS) $(MKCMDSCSRCS) test_ss.c

# for building archives

FILES=	$(SRCS) $(MKCMDSFILES) $(HFILES) \
	ss_err.et std_rqs.ct Makefile \
	test_ss.c ss mit-sipb-copyright.h copyright.h

#
# stuff to build
#

.c.o:
	$(CC) $(CFLAGS) -c $*.c
ifdef BUILD_DLL_SHLIBS
	(export JUMP_DIR=`pwd`/jump; $(CC) -B$(JUMP_PREFIX) $(CFLAGS) \
		-o jump/$*.o -c $*.c)
endif

all::	mk_cmds libss.a # libss_p.a lint

dist:	archives

install::

install-libs:: all
	$(INSTALLLIB) libss.a $(LIBDIR)/libss.a
	$(CHMOD) 644 $(LIBDIR)/libss.a
	$(RANLIB) $(LIBDIR)/libss.a
	$(CHMOD) $(LIBMODE) $(LIBDIR)/libss.a

install-libs:: $(HFILES) copyright.h
	@rm -rf $(INCLDIR)/ss
	@mkdir $(INCLDIR)/ss
	for i in $(HFILES) copyright.h; do \
		$(INSTALLINC) $(srcdir)/$$i $(INCLDIR)/ss/$$i; \
	done

install-libs:: copyright.h
	$(INSTALLINC) $(srcdir)/copyright.h $(INCLDIR)/ss/mit-sipb-copyright.h

install-tree::

std_rqs.c: std_rqs.ct
	$(MK_CMDS) std_rqs.ct

ss_err.c ss_err.h: ss_err.et
	$(COMPILE_ET) ss_err.et

dep depend .depend: ss_err.h
	$(CPP) -M $(CFLAGS) *.c >.depend

ct.tab.c ct.tab.h: ct.y
	rm -f ct.tab.* y.*
	yacc -d $(srcdir)/ct.y
	mv -f y.tab.c ct.tab.c
	mv -f y.tab.h ct.tab.h

# install_library_target(ss,$(OBJS),$(SRCS),)
all:: libss.a

libss.a: $(OBJS)
	$(RM) $@.bak
	-$(MV) $@ $@.bak
	$(ARCHIVE) $@ $(OBJS)
	$(RANLIB) $@
	$(RM) ../$@
	$(LN) ss/$@ ../$@


clean::	
	$(RM) ../libss.a libss.a mk_cmds
	$(RM) *.o *~ \#* *.bak core 

really-clean:: clean
	$(RM) .depend ss_err.h ss_err.c

install-libs::
	$(INSTALLLIB) libss.a $(LIBDIR)/libss.a
	$(CHMOD) 644 $(LIBDIR)/libss.a
	$(RANLIB)    $(LIBDIR)/libss.a
	$(CHMOD) 444 $(LIBDIR)/libss.a


libss.o:	$(OBJS)
	$(LD) -r -s -o $@ $(OBJS)
	$(CHMOD) -x $@

mk_cmds: mk_cmds.sh
	./config_script mk_cmds.sh $(AWK) > mk_cmds
	chmod +x mk_cmds

include .depend
