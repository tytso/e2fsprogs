TOPSRC=..

!include $(TOPSRC)\powerquest\MCONFIG

ALL:: libecho.exe

libecho.exe: libecho.c

clean:: 
	$(RM) libecho.exe

!include $(TOPSRC)\powerquest\MAKEFILE.STD
