#
# Makefile for lib/et
#

all:: compile_et libcom_err.a

include ../../MCONFIG

OBJS= error_message.o et_name.o init_et.o com_err.o
SRCS = error_message.c et_name.c init_et.c com_err.c

HFILES= com_err.h

ifdef BUILD_PROFILE_LIBS
all:: libcom_err_p.a
endif

ifdef BUILD_DLL_SHLIBS
DLL_ADDRESS = 0x66800000
DLL_JUMPSIZE = 0x1000
DLL_GOTSIZE  = 0x1000
DLL_VERSION = 1.0
DLL_IMAGE = libet
DLL_STUB = libcom_err
DLL_MYDIR = et
DLL_INSTALL_DIR = $(SHLIBDIR)

include ../Makefile.dll-lib
endif

RM=rm -f
MV=mv
LN=ln -s
TAGS=etags

DEFS= -DHAS_STDLIB_H

CFLAGS_NO= $(WFLAGS) $(DEFS)
CFLAGS= $(CFLAGS_NO) $(OPT)

#
# what to build...
#

.c.o:
	$(CC) $(CFLAGS) -c $*.c
ifdef BUILD_PROFILE_LIBS
	$(CC) $(CFLAGS_NO) -g -pg -o profiled/$*.o -c $*.c
endif
ifdef BUILD_DLL_SHLIBS
	(export JUMP_DIR=`pwd`/jump; $(CC) -B$(JUMP_PREFIX) $(CFLAGS) \
		-o jump/$*.o -c $*.c)
endif


compile_et: compile_et.sh
	./config_script compile_et.sh $(AWK) > compile_et
	chmod +x compile_et

libcom_err.a: $(OBJS)
	$(RM) $@.bak
	-$(MV) $@ $@.bak
	$(ARCHIVE) $@ $(OBJS)
	$(RANLIB) $@
	$(RM) ../$@
	$(LN) et/$@ ../$@

libcom_err_p.a: $(OBJS)
	$(RM) $@.bak
	-$(MV) $@ $@.bak
	(cd profiled ; $(ARCHIVE) ../$@ $(OBJS))
	$(RANLIB) $@
	$(RM) ../$@
	$(LN) et/$@ ../$@

clean::
	$(RM) compile_et libcom_err.a libcom_err_p.a
	$(RM) $(OBJS) profiled/*
	$(RM) *~ \#* *.bak *.otl *.aux *.toc *.PS *.dvi *.ps TAGS *.ln
	$(RM) ../libcom_err.a ../libcom_err_p.a

really-clean:: clean
	$(RM) .depend

install-libs:: libcom_err.a
	$(INSTALLLIB) libcom_err.a $(LIBDIR)/libcom_err.a
	$(CHMOD) 644 $(LIBDIR)/libcom_err.a
	$(RANLIB)    $(LIBDIR)/libcom_err.a
	$(CHMOD) $(LIBMODE) $(LIBDIR)/libcom_err.a

install-libs:: $(HFILES)
	@rm -rf $(INCLDIR)/et
	@mkdir $(INCLDIR)/et
	for i in $(HFILES); do \
		$(INSTALLINC) $$i $(INCLDIR)/et/$$i; \
	done

install-tree::

install::

## 

com_err.ps : com_err.dvi
com_err.dvi: com_err.texinfo

libcom_err.o:	$(LIBOBJS)
	ld -r -s -o libcom_err.o $(LIBOBJS)
	chmod -x libcom_err.o


TAGS:	$(SRCS)
	$(TAGS) $(SRCS)

dep depend .depend: compile_et
	$(CPP) -M $(CFLAGS) *.c >.depend

include .depend
