TOPSRC=..

!include $(TOPSRC)\powerquest\MCONFIG

OBJS= extent.obj \
	ext2_block_move.obj \
	ext2_inode_move.obj \
	resize2fs.obj \
	sim_progress.obj

ALL: $(OBJS)
