include ./MCONFIG

all: libs 
	(cd e2fsck; $(MAKE))
	(cd debugfs ; $(MAKE))
	(cd misc ; $(MAKE))

libs:
	(cd lib/et; $(MAKE))
	(cd lib/ss; $(MAKE))
	(cd lib/ext2fs; $(MAKE))
	(cd lib/e2p; $(MAKE))

install:
	(cd lib/et; $(MAKE) install)
	(cd lib/ss; $(MAKE) install)
	(cd lib/ext2fs; $(MAKE) install)
	(cd lib/e2p; $(MAKE) install)
	(cd e2fsck; $(MAKE) install)
	(cd debugfs; $(MAKE) install)
	(cd misc ; $(MAKE) install)

install-tree:
	(cd lib/et; $(MAKE) install-tree)
	(cd lib/ss; $(MAKE) install-tree)
	(cd lib/ext2fs; $(MAKE) install-tree)
	(cd lib/e2p; $(MAKE) install-tree)
	(cd e2fsck; $(MAKE) install-tree)
	(cd debugfs; $(MAKE) install-tree)
	(cd misc ; $(MAKE) install-tree)

install-libs:
	(cd lib/et; $(MAKE) install-libs)
	(cd lib/ss; $(MAKE) install-libs)
	(cd lib/ext2fs; $(MAKE) install-libs)
	(cd lib/e2p; $(MAKE) install-libs)

install-dirs:
	install -d $(ETCDIR)
	install -d $(INCLDIR)
	install -d $(LIBDIR)
	install -d $(SBINDIR)
	install -d $(SHLIBDIR)
	install -d $(SMANDIR)
	install -d $(UMANDIR)
	install -d $(USRBINDIR)
	install -d $(USRSBINDIR)

bin-tree:
	rm -rf dest
	mkdir dest
	$(MAKE) DESTDIR=`pwd`/dest install-dirs
	$(MAKE) DESTDIR=`pwd`/dest install
	mkdir dest/install-utils dest/usr/man/cat1 dest/usr/man/cat8
	cp install-utils/convfstab dest/install-utils
	cp install-utils/remove_preformat_manpages dest/install-utils
	(cd dest; export MANPATH=`pwd`/usr/man; \
		../install-utils/compile_manpages)

clean:
	rm -f $(PROGS) \#* *.s *.o *.a *~ core MAKELOG 
	rm -rf dest
	(cd lib/et; $(MAKE) clean)
	(cd lib/ss; $(MAKE) clean)
	(cd lib/ext2fs; $(MAKE) clean)
	(cd lib/e2p; $(MAKE) clean)
	(cd e2fsck; $(MAKE) clean)
	(cd debugfs; $(MAKE) clean)
	(cd misc ; $(MAKE) clean)

really-clean:
	rm -f $(PROGS) \#* *.s *.o *.a *~ core MAKELOG 
	rm -f .depend bin/* shlibs/*.so.*
	(cd lib/et; $(MAKE) really-clean)
	(cd lib/ss; $(MAKE) really-clean)
	(cd lib/ext2fs; $(MAKE) really-clean)
	(cd lib/e2p; $(MAKE) really-clean)
	(cd e2fsck; $(MAKE) really-clean)
	(cd debugfs; $(MAKE) really-clean)
	(cd misc ; $(MAKE) really-clean)

dep depend:
	(cd lib/et; cp /dev/null .depend; $(MAKE) depend)
	(cd lib/ss; cp /dev/null .depend; $(MAKE) depend)
	(cd lib/ext2fs; cp /dev/null .depend; $(MAKE) depend)
	(cd lib/e2p; cp /dev/null .depend; $(MAKE) depend)
	(cd debugfs; cp /dev/null .depend; $(MAKE) depend)
	(cd e2fsck; cp /dev/null .depend; $(MAKE) depend)
	(cd misc ; cp /dev/null .depend; $(MAKE) depend)

world: 
	@date
	$(MAKE) depend 
	@date
	$(MAKE) all
	@date
	(cd e2fsck/images; ./test_script)
	@date


