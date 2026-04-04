#
#	Makefile
#
#	Dependencies
#	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>
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
CFLAGSKERNEL = $(CFLAGS) -I/usr/src/linux/src/export-osfmach3/osfmach3_ppc/include -DMODULE -D__KERNEL__ -O2

# compile with debug code and symbolic debugging information
CFLAGS += -DDEBUG
CFLAGSKERNEL += -DDEBUG


all				: afpfs afpmount afptest

clean				:
	rm -rf kernel/* obj/*

distribution			: README.html BUGS CHANGES afptest afpfs afpmount
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
afpmount			: afpmount.c \
				  obj/afp.o obj/asp.o obj/atp.o \
				  obj/nbp.o obj/timer.o obj/mac.o


# test application
afptest				: obj/afptest.o obj/mac.o obj/timer.o \
				  obj/nbp.o obj/afp.o obj/asp.o obj/atp.o \
				  obj/rtmp.o obj/aep.o
	$(CC) -o $@ obj/*.o

