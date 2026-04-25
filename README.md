# AppleShare network file system for Unix

From the first release on January 24, 1984, a complete and well-designed networking system has been integral to the Macintosh. AppleTalk, as it later became known, was revolutionary, as personal computers before then had never known networking capabilities. It grew to encompass file servers, laser printers, ethernet, and token rings; with the advent of System 7, every Macintosh became a file server. On other personal computers, networking was patched on to boot loaders (such as DOS) and graphical user-interface shells. The hardware, however, was usually incompatible and the software incomprehensible. Even now, nearing the end of the second millenium, no computer system in existence has networking that matches the ubiquity and utter ease of use of AppleTalk. It is fair to say that if not for the vision of Apple's designers, personal networking would not be what it is today. This software is dedicated to Gursharan Sidhu and the other fine engineers at Apple who have made computing fun for all of us.

## afpfs

`afpfs` (AppleTalk Filing Protocol File System) is a kernel module for the Unix operating system that allows you to mount AppleShare file servers over a network. In practical terms, this means that you can mount disks that you are sharing from your networked Macintoshes as if they were connected directly to your Unix machine. Roughly speaking, `afpfs` is the opposite of 'netatalk', which lets you mount Unix file systems on your Macintosh; or, in other words yet, `afpfs` is to AppleTalk as NFS is to TCP/IP.

In addition, the various protocol layers are completely independent APIs and available to authors of other AppleTalk-based software. In particular, `afpfs` provides interfaces to the workstation sides of the following AppleTalk protocols: AEP, ATP, ASP, AFP, and NBP.

### Requirements

To use `afpfs`, you need root access to a machine running either MkLinux or the original Intel-based Linux operating system. I have been developing it on current kernels, and GNU C and libc, and I do not know under which older versions it will not run. You must have compiled AppleTalk (DDP) support into your kernel. Your kernel must support the ELF binary format and loadable modules-- I have not tried compiling it directly into the kernel, but I suppose it should be possible without too much effort.

Your machine must be networked to other AppleShare servers. This includes any Macintosh with File Sharing turned on, and any Unix machine with the \`netatalk' afpd daemon.

For some reason, `afpfs` appears to require [netatalk](http://www.umich.edu/~rsug/netatalk/index.html). This is not intentional, and I will be looking into why this is so.

### Status

**The 1.0 beta 1 release is now available for both MkLinux and Intel-based Linux!** I am actively soliciting feedback and bug reports-- since this is my first Unix kernel-level project, I especially welcome comments, criticisms, and suggestions from more seasoned hackers.

`afpfs` is available in source code form, and as precompiled binaries for PowerPC-based MkLinux and Intel-based Linux. Note that the MkLinux binary supports current kernels (I use 2.0.33), whereas the i386 Linux binary does not. If someone would provide me with a compiled version for current i386 Linux kernels, I'll be happy to put it up here. Refer to the \`BUGS' and \`CHANGES' files in the distributions for more information.

### Installation

If you are installing from the source code, 'untar' the distribution and 'make' it. This will create the required binaries.

#### afptest

This is a user-level program to test the software. It does not require the kernel module nor the afpmount program, and will not crash your machine if something goes wrong. Invoke it by typing:

    afptest [-u username] [server][@zone]

where `username` is the identity you want to log in with (or as a guest, if not specified), `server` is the name of the file server as it appears in the Chooser, and `zone` the name of the zone the server is in (but see BUGS regarding servers in zones). If you do not specify a server, it will find one for you! For example, I (quite often) use

    afptest -u 'Ben Hekster' Centris

You should probably try this first before installing the kernel module.

#### afpfs

This is the actual kernel module that implements the AFP file system. To install the module into your kernel, use:

    insmod afpfs

as root. To remove the module, unmount all `afpfs` volumes, and use:

    rmmod afpfs

#### afpmount

This is the program used to mount AFP volumes. A separate mount program is needed because otherwise there would be no way of passing parameters to AFPFS. To mount an AFP volume, use:

    afpmount [-u username [-p password]] mount-point server[@zone] volume

where `mount-point` is the path to a local directory under which to mount the volume, `volume` is the name of the Macintosh volume to mount, `server` is the name of the Macintosh file server you want to mount, and `zone` is optionally the name of a zone in which the file server is located. Note that specifying passwords on the command line is not recommended (for security reasons), and `afpmount` will prompt you for it if you don't. For example, I use

    afpmount -u 'Ben Hekster' /mnt/centris Centris Public

**Tip:** Mounting file systems is normally a privileged operation reserved to the system administrator. By making afpmount suid root, the administrator can allow other users to mount AppleShare volumes of their own.

To unmount the volume, use `umount(8)`. For example,

    umount /mnt/centris

### Future Directions

There are still many ways to complete and extend the quite basic functionality of `afpfs` as it is now. Here are some of the more serious known limitations, and when I am intending to correct them:

#### 1.1

- Support for Macintosh resource forks and file attributes

  This is the most serious current limitation. Without it, Macintosh file systems cannot be usefully backed up through `afpfs`. Files created through `afpfs` always have a bland 'document' icon and cannot be dragged-and-dropped.

  Support for resource forks and file attributes will take the form of support for the AppleShare and AppleDouble file format standards. Some file attributes will be set on files created through `afpfs`-- for example, the file type and creator may be guessed by looking at the new file's extension.

- AppleShare Internet Protocol (ASIP/DSI)

  TCP/IP transport support was recently added to AppleShare, allowing the mounting of Macintosh file systems over the Internet. This is an exciting technology and is planned to be incorporated in the next release. This implies brining the AFP protocol support up to 2.2.

#### 1.2

- identity mapping

  `afpfs`does not attempt to match Macintosh users (as defined in the "Users and Groups" control panel) with corresponding users on the Unix system (as defined in the password file).

  Currently, the user who mounts the Macintosh file system is made owner of all its files and directories. Note that this does not give a user more privileges than he would have had by mounting that file system in the usual way (from another Macintosh)-- he is still constrained by the permissions granted by the Macintosh file server.

- symbolic links (Macintosh aliases)

  This means that a symbolic link created through `afpfs` would appear as an alias on the Macintosh file server, and correspondingly, every alias would appear as a symlink. This is much trickier than you might imagine but seems like tons of fun to do.

#### 1.3 and beyond

- Socket rationalization
  The AppleTalk protocol stack APIs that are used in `afpfs` are available to other developers who want to make use of them, more or less in the form defined in Apple's Inside AppleTalk, first edition. I am considering supporting a `socket(2)` form also, which would allow BSD-style programming with AppleTalk sockets.

- Solaris release

  I could extend the use of `afpfs` to a much wider audience by providing a Solaris port. The main stumbling block here is that I am unfamiliar with Sun's undocumented Virtual File System layer.

- Performance enhancements

  The performance of the current implementation can certainly be improved. I am thinking along the lines of block caching, read-ahead, write-behind, and elimination of unnecessary copying.

- Kernel release

  If there is a need for it, I may look into retrofitting it to compile directly into the kernel rather than as a loadable module.
