TOPSRC=..
LIBNAME=RESIZE.LIB
OBJFILE=RESIZE.LST

OBJS= extent.obj \
	ext2_block_move.obj \
	ext2_inode_move.obj \
	resize2fs.obj \
	sim_progress.obj

ALL:: $(OBJS)

!include $(TOPSRC)\powerquest\MCONFIG


!include $(TOPSRC)\powerquest\MAKEFILE.STD
