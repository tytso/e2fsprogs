TOPSRC=..
LIBNAME=E2FSCK.LIB
OBJFILE=E2FSCK.LST

OBJS= super.obj pass1.obj pass1b.obj pass2.obj pass3.obj pass4.obj \
	pass5.obj

!include $(TOPSRC)\powerquest\MCONFIG

ALL:: $(OBJS)

!include $(TOPSRC)\powerquest\MAKEFILE.STD
