# /gadi/project/Makefile
#
# Makefile for the extended-2 filesystem editor.
#
# First created   : April 8 1995
#
# Copyright (C) 1995 Gadi Oxman

# ------------------------------------------------------------------------
# EXT2ED, ncurses and readline
# ------------------------------------------------------------------------
#
# EXT2ED requires the ncurses and readline libraries.
#
# Please define OLD_NCURSES below if you are using an old version of ncurses.
# I don't know the exact threshold, but from my experience this flag is needed
# for version 1.8.5 and not needed for 1.9.2c and up. In any case, you will
# notice immediately that this flag is wrong - The main window will be totally
# corrupted.

# FLAGS			=	-DOLD_NCURSES 

# Define the directories below to insure that the compiler will find the
# required ncurses and readline include files / libraries. Be sure that you
# don't mix two two versions of ncurses.

# NCURSES_INCLUDE	=	-I/usr/local/include -I/usr/local/include/ncurses
# NCURSES_LIB		= 	-L/usr/local/lib
# READLINE_INCLUDE	=	-I/usr/include -I/usr/include/readline
# READLINE_LIB		=	-L/usr/lib

# ------------------------------------------------------------------------
# Install Directories 
# ------------------------------------------------------------------------

# The executable will go into BIN_DIR.
# The configuration file, log file, etc will go into VAR_DIR.
# The manual page will go into MAN_DIR.
# The EXT2ED documentation will go into DOC_DIR.

BIN_DIR		=	/usr/bin
VAR_DIR		=	/var/lib/ext2ed
MAN_DIR		=	/usr/man/man8
DOC_DIR		=	/usr/doc/ext2ed

# ------------------------------------------------------------------------

CC		=	gcc
CCFLAGS		=	-O $(FLAGS) -DVAR_DIR=\"$(VAR_DIR)\" $(NCURSES_INCLUDE) $(READLINE_INCLUDE)
LINKFLAGS	=	$(NCURSES_LIB) $(READLINE_LIB)
LIBS		=	-lreadline -lncurses

.c.o:
	$(CC) $(CCFLAGS) $(INCLUDE_DIR) -c $<

OBJS=	main.o init.o general_com.o inode_com.o dir_com.o super_com.o    \
	disk.o win.o group_com.o file_com.o blockbitmap_com.o ext2_com.o \
	inodebitmap_com.o

ext2ed: $(OBJS)
	$(CC) $(OBJS) $(LINKFLAGS) $(LIBS) -o ext2ed

clean:
	rm ext2ed *.o

install: ext2ed
	install -d $(VAR_DIR)
	install -d $(DOC_DIR)
	install -m 755 ext2ed $(BIN_DIR)
	install -m 644 ext2.descriptors $(VAR_DIR)
	install -m 644 ext2ed.conf $(VAR_DIR)
	install -m 644 doc/ext2ed.8 $(MAN_DIR)
	install -m 644 doc/user-guide-0.1.sgml $(DOC_DIR)
	install -m 644 doc/user-guide-0.1.ps $(DOC_DIR)
	install -m 644 doc/Ext2fs-overview-0.1.sgml $(DOC_DIR)
	install -m 644 doc/Ext2fs-overview-0.1.ps $(DOC_DIR)
	install -m 644 doc/ext2ed-design-0.1.sgml $(DOC_DIR)
	install -m 644 doc/ext2ed-design-0.1.ps $(DOC_DIR)
