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
	(cd e2fsck; $(MAKE) install)
	(cd debugfs; $(MAKE) install)
	(cd misc ; $(MAKE) install)

install-libs:
	(cd lib/et; $(MAKE) install)
	(cd lib/ss; $(MAKE) install)
	(cd lib/ext2fs; $(MAKE) install)
	(cd lib/e2p; $(MAKE) install)

clean:
	rm -f $(PROGS) \#* *.s *.o *.a *~ core MAKELOG
	(cd lib/et; $(MAKE) clean)
	(cd lib/ss; $(MAKE) clean)
	(cd lib/ext2fs; $(MAKE) clean)
	(cd lib/e2p; $(MAKE) clean)
	(cd e2fsck; $(MAKE) clean)
	(cd debugfs; $(MAKE) clean)
	(cd misc ; $(MAKE) clean)

really-clean: clean
	rm -f .depend
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


