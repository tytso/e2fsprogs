TOPSRC=..\..

!include $(TOPSRC)\powerquest\MCONFIG

OBJS= 	alloc.obj \
	alloc_tables.obj \
	badblocks.obj \
	bb_compat.obj \
	bb_inode.obj \
	bitmaps.obj \
	bitops.obj \
	block.obj \
	bmove.obj \
	brel_ma.obj \
	check_desc.obj \
	closefs.obj \
	cmp_bitmaps.obj \
	dblist.obj \
	dblist_dir.obj \
	dirblock.obj \
	dir_iterate.obj \
	dupfs.obj \
	expanddir.obj \
	freefs.obj \
	get_pathname.obj \
	getsize.obj \
	icount.obj \
	initialize.obj \
	inline.obj \
	inode.obj \
	irel_ma.obj \
	ismounted.obj \
	link.obj \
	lookup.obj \
	mkdir.obj \
	namei.obj \
	native.obj \
	newdir.obj \
	openfs.obj \
	read_bb.obj \
	read_bb_file.obj \
	rs_bitmap.obj \
	rw_bitmaps.obj \
	swapfs.obj \
	unlink.obj \
	valid_blk.obj \
	version.obj

ALL: $(OBJS)
