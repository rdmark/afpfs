#
#	Makefile
#	
#	Dependencies
#	
#	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
#	Licensed under the MIT License - see LICENSE file for details.
#	SPDX-License-Identifier: MIT
#	
#	Thu Oct 24 17:01:04 IST 1996
#

obj/%.o				: %.c
	$(CC) $< -c $(CFLAGS) -o $@

kernel/%.o			: %.c
	$(CC) $< -c $(CFLAGSKERNEL) -o $@


# try to avoid platform dependencies
# *** should compile with warnings on (-Wall)
CFLAGS = -D_POSIX_SOURCE -fstrict-prototypes
CFLAGSKERNEL = $(CFLAGS) -DMODULE -D__KERNEL__ -O2

# compile with debug code and symbolic debugging information
CFLAGS += -DDEBUG
CFLAGSKERNEL += -DDEBUG


all				: afpfs afpmount afptest

clean				:
	rm -rf kernel/* obj/*

distribution			: all clean \
				  README.html BUGS CHANGES afptest afpfs afpmount
	(cd ..; tar cfz afpfs.tar.gz \
		afpfs/afptest afpfs/afpfs afpfs/afpmount \
		afpfs/README.html afpfs/BUGS afpfs/CHANGES \
		)


kernel/				:
	mkdir kernel

obj/				:
	mkdir obj


# kernel module

afpfs				: kernel/ \
				  kernel/afpfs.o kernel/file.o kernel/inode.o \
				  kernel/afp.o kernel/asp.o kernel/atp.o \
				  kernel/timer.o kernel/kernel.o kernel/mac.o
	$(CC) -o $@ -r -nostdlib kernel/*.o -lgcc


# mount program
afpmount			: afpmount.c obj/ \
				  obj/afp.o obj/asp.o obj/atp.o \
				  obj/nbp.o obj/timer.o obj/mac.o obj/rtmp.o
	$(CC) -o $@ afpmount.c obj/*.o

# test application
afptest				: afptest.c obj/ \
				  obj/mac.o obj/timer.o \
				  obj/nbp.o obj/afp.o obj/asp.o obj/atp.o \
				  obj/rtmp.o obj/aep.o
	$(CC) -o $@ afptest.c obj/*.o

