include ../../MCONFIG

ARCHIVE=ar r
RANLIB=ranlib
RM=rm -f
MV=mv
LN=ln -s
TAGS=etags

CFLAGS_NO=
CFLAGS= $(CFLAGS_NO) $(OPT)

OBJS= error_message.o et_name.o init_et.o com_err.o
SRCS = error_message.c et_name.c init_et.c com_err.c

HFILES= com_err.h

#
# what to build...
#

.c.o:
	$(CC) $(CFLAGS) -c $*.c
	$(CC) $(CFLAGS_NO) -g -pg -o profiled/$*.o -c $*.c

all: compile_et libcom_err.a libcom_err_p.a

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

clean:
	$(RM) compile_et libcom_err.a libcom_err_p.a
	$(RM) $(OBJS) profiled/*
	$(RM) *~ \#* *.bak *.otl *.aux *.toc *.PS *.dvi *.ps TAGS *.ln

really-clean: clean
	$(RM) .depend

install:: libcom_err.a
	$(INSTALLLIB) libcom_err.a $(DESTDIR)$(LIBDIR)/libcom_err.a
	$(CHMOD) 644 $(DESTDIR)$(LIBDIR)/libcom_err.a
	$(RANLIB)    $(DESTDIR)$(LIBDIR)/libcom_err.a
	$(CHMOD) $(LIBMODE) $(DESTDIR)$(LIBDIR)/libcom_err.a

install:: $(HFILES)
	@rm -rf ${DESTDIR}$(INCLDIR)/et
	@mkdir ${DESTDIR}$(INCLDIR)/et
	for i in $(HFILES); do \
		$(INSTALLINC) $$i ${DESTDIR}$(INCLDIR)/et/$$i; \
	done

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
