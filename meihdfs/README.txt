The problem
===========
Panasonic DVR-Recorders seem to have a custom file system called
MEIHDFS-V2.0. So if the harddisk in such a DVR-Recorder dies, it is
difficult to recover data from it, because the filesystem is entirely
undocumented and proprietary.
But there may be hope! This tool is an attempt to parse the filesystem
structures and extract the files.

Create a HDD image
==================
First you have to remove the harddisk from the DVR device. 
After removing the HDD, you can mount it to your PC, i.e. with an USB
Adapter or (preferably) directly to the IDE interface.

If the PC recognizes the drive, you may have luck and it is only partly 
damaged and data can be rescued. However if you want to make sure, you
may want to consult a data recovery company to do a professional rescue
of the harddisk image, because the harddisk may die during dumping!

If you just want to dump a sane harddrive, you can skip this step.
Otherwise, the next thing to do is to dump the contens of the disk to an image
file so you need to have at least the size of the HDD
available on one of your harddisks to make an image. 
You may need up to the same size for the extracted movie data then, 
but this can also be on another drive then. 
There are many tools available for dumping a harddisk to an image file,
i.e. Datarescue's drdd:

http://www.datarescue.com/photorescue/v3/drdd.htm

As of 06/2025, the download links seem to be broken, so you may want to use 
the archive.org mirror instead:

https://web.archive.org/web/20250114205844/https://www.datarescue.com/freefiles/drdd.exe.zip

However due to the way how windows handles bad drives, it's usually a pain
dumping them on Windows, i.e. if the drive detaches itself from the bus 
during read und is connected via a USB-Adapter, most dumper programs including
drdd just continue dumping and writing lots of faulty sectors even if the 
sectors would be readable after the drive reattached itself to the bus after 
reset.
Therefore I recommend using a linux BootCD/system and dd_rescue.

After creating the image, you are set to use my utility to recover your data.

Extracting the filesystem files
===============================
Now the strategy on how to recover your remaining movie data largely depends
on the way your data on the HDD has been damaged.
If the file allocation structures are intact, you have a good chance for 
a painless recovery using this tool.
I assume that the name of the image you created in the previous step is 
image.dd
If you just want to dump a sane harddrive without having an image, use 
the physical address of the drive, i.e. on Linux /dev/sdX or on 
Windows \\.\PhysicalDriveX
You can get a list of the attached physical drives with the following 
command:

wmic diskdrive list brief

You need to have an empty destination directory where the VOB files will
be recovered to. Assuming your destination directory is f:\dump just issue:

extract_meihdfs image.dd f:\dump

This will extract a DVD-RAM directory structure like it can be found on
the harddisk:

/
  RTAV.MNG
  DVD_RTAV/
    0001.IFO
    0001.BUP
    0001.VRO
    0002.IFO
    ...
    HDD2.IFO
    HDD2.BUP
    MTV2.IFO
    MTV2.BUP

The RTAV.MNG file contains some unknown management information.
The .IFO files contain metadata that describe the content of the .VRO 
files. So the .VRO files contain the video data and the .IFO contain
i.e. the program name, number of programs, video format info, etc.
The .BUP files are BackUP files for the .IFO files, so they normally
contain the same data as the .IFO files.

The HDD2.IFO also contains some unknown management information, also
not needed for extraction.
The MTV2.IFO contains a list of all programs that are contained in the
various .IFO files in the directory, so this is an "index" for the
recorder to have an overview over the available programs. However for
extraction for you as a user, this is also not needed.

Extracting the movies from the files
====================================
After having dumped the directory structure mentioned above, you need
to extract the various programs from the recordings.
This can be done with the dvd-vr tool.
This tool is a modified version of the dvd-vr program from pixelbeat:
http://www.pixelbeat.org/programs/dvd-vr/
The parser was adapted to match the structure of the Panasonic recorder.
So you need to run this too with the .IFO and .VRO file as parameter
for each .IFO and .VRO pair, i.e.:

dvd-vr 0001.IFO 0001.VRO

This will extract the programs from the file which can then finally
be used as videos.
If you just want to display the info from the .IFO file, omit the
second parameter.
You can use a loop to process all files, i.e. in Windows batch
assuming that your dump directory is f:\dump:

for /r "F:\dump\DVD_RTAV\" %I in (*.VRO) do dvd-vr "%~dI%~pI%~nI.IFO" "%~I"

or in Linux assuming dump directory ~/dump:

for i in ~/dump/DVD_RTAV/*.VRO; do ./dvd-vr ${i%.*}.IFO $i; done

Extracting movies from severely damaged HDDs
============================================
If the extract tool cannot find the MEIHDFS-header or - more likely - 
your image is just too damaged to hold a valid filesystem structure, 
you may want to try a different approach. This one was described
by Stefan Haliner in the avsforum. He basically made a python script that
extracts all consecutive mpeg-blocks from an image and assembles them 
together. You can fetch his script here:

https://github.com/haliner/dvr-recover

However this tool requires you to install the python framework, so if you
want a simple executable to do dumping, use my pioneer_rec tool
and read the README.txt section about extraction there:

https://github.com/leecher1337/pioneer-rec

Acknowledgements
================
As already mentioned, the dvd-vr tool is derived from  
http://www.pixelbeat.org/programs/dvd-vr/
using my own research about the .IFO structures of the Panasonic
recorder.

The extract tool is based on research done by Honza Maly 
(http://hkmaly.comli.com/) and my own research.

Technical details
=================
I gathered the structure of the on-disk filesystem through looking at the
dump of a drive with a hex-editor. The data representation on the disk
is little endian. Most information can be found in meihdfs1.h header file.
Various recorders have different filesystem start offsets, because on
some recorders there is also firmware information stored in the beginning
of the harddisk. The start of the filesystem is indicated by a 
MEIHDFS V2.0 header (maybe meaning: 
"Matsushita Electric Industrial Hard Disk File System V2.0"?):

Offset       0  1  2  3  4  5  6  7   8  9  A  B  C  D  E  F
016000000   95 FE 00 00 00 00 00 00  4D 45 49 48 44 46 53 2D   .þ      MEIHDFS-
016000010   56 32 2E 30 00 00 00 00  48 44 44 00 00 00 00 00   V2.0    HDD
016000020   00 00 00 00 00 00 00 00  99 EF 00 00 8F 18 03 00           .ï
016000030   04 00 00 00 01 00 00 00  02 00 00 00 03 00 00 00
016000040   04 00 00 00 01 00 01 00  02 00 01 00 03 00 01 00
016000050   04 00 01 00 01 00 02 00  02 00 02 00 03 00 02 00
016000060   04 00 02 00 01 00 03 00  02 00 03 00 03 00 03 00
016000070   04 00 03 00 00 00 00 00  00 00 00 00 00 00 00 00

The DWORD at offset 0x2C seems to indicate the end of the disk in blocks.
hkmaly found out that the block size of the filesystem is 0xC0000 bytes.
The superblock (header+mgmt info) seems to be repeated all over 
the disk every 0x10000 blocks.
Management information block size is 0x1000 bytes.

Based on the discovery of hkmaly about the layout of a file inode entry
(see below), I found that there is an inode table pointing to
the inodes of the filesystem. I assume that it is always located 
at offset 0xC600 starting from the MEIHDFS-Header:

Offset       0  1  2  3  4  5  6  7   8  9  A  B  C  D  E  F
0160C6000   F2 B5 00 00 3C 17 00 00  00 00 00 00 00 00 00 00   òµ  <           
0160C6010   EB 03 C0 00 00 00 00 00  01 00 01 00 01 04 C0 00   ë À           À 
0160C6020   00 00 00 00 01 00 01 00  DA 03 C0 00 00 00 00 00           Ú À     
0160C6030   01 00 01 00 EC 03 C0 00  00 00 00 00 01 00 01 00       ì À         
0160C6040   FC 03 C0 00 00 00 00 00  01 00 01 00 E7 03 C0 00   ü À         ç À 
0160C6050   00 00 00 00 01 00 01 00  F6 03 C0 00 00 00 00 00           ö À     
0160C6060   01 00 01 00 CF 03 C0 00  00 00 00 00 01 00 01 00       Ï À         
0160C6070   D1 03 C0 00 00 00 00 00  01 00 01 00 00 04 C0 00   Ñ À           À 
0160C6080   00 00 00 00 01 00 01 00  C5 03 C0 00 00 00 00 00           Å À     
0160C6090   01 00 01 00 C3 03 C0 00  00 00 00 00 01 00 01 00       Ã À         
0160C60A0   CB 03 C0 00 00 00 00 00  01 00 01 00 C9 03 C0 00   Ë À         É À 
...

The inode list starts at offset 0x10 and an entry in the list is 0xC bytes
in size. The first QWORD in such an entry is a pointer to the management
info block relative to the file system start where the corresponding sector
resides. In the example above the first inode entry pointer is 0xC003EB.
Assuming that the MEIHDFS starts at 0x16000000, the byte offset on the disk is
0xC003EB * 0x1000 + 0x16000000 = 0xC163EB000

The destination of the inode entry pointer can be of various types.
Inode 0 usually points to a root directory info:

Offset       0  1  2  3  4  5  6  7   8  9  A  B  C  D  E  F
C163EB000   F2 B5 00 00 00 00 00 00  00 00 00 00 01 00 00 00   òµ              
C163EB010   02 00 00 00 00 00 00 00  00 00 00 00 01 00 FF 41                 ÿA
C163EB020   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C163EB030   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
...
C163EB400   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C163EB410   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C163EB420   01 00 00 00 02 00 08 00  44 56 44 5F 52 54 41 56           DVD_RTAV
C163EB430   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C163EB440   09 00 00 00 01 00 08 00  52 54 41 56 2E 4D 4E 47           RTAV.MNG
C163EB450   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   

It can be identified by 0x41FF0001 magic at offset 0xC.
The directory entries start at offset 0x420, each entry consisting
of 0x20 bytes:
The first DWORD is the INODE number, followed by a WORD that
contains the file type (1=file, 2=directory), followed by a 
WORD that contains the length of the filename, the rest is
the filename, NULL-padded.
The inode can be looked up in the inode table mentioned above.
A directory entry looks similar, but has 0x41C20001 magic.

hkmaly also found out that file information can be identified by the
0x81C00001 magic at offset 0x1C of a managment block and that the
sector number is at offset 0x04 in such a sector:

Offset       0  1  2  3  4  5  6  7   8  9  A  B  C  D  E  F
C16405000   40 46 00 00 09 00 00 00  00 00 00 00 01 00 00 00   @F              
C16405010   21 30 00 00 00 00 00 00  01 00 00 00 01 00 C0 81   !0            À
C16405020   00 00 00 00 00 00 00 00  F8 1A 0C 40 F8 1A 0C 40           ø  @ø  @
C16405030   F8 1A 0C 40 00 00 00 00  00 00 00 00 00 00 00 00   ø  @            
C16405040   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C16405050   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C16405060   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C16405070   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C16405080   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C16405090   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C164050A0   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C164050B0   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C164050C0   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C164050D0   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C164050E0   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C164050F0   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00                   
C16405100   1F 02 00 00 00 00 00 00  00 06 00 00 00 00 00 00                   
...

At offset 0x10, there is a QWORD identifying the total file
size in bytes.
At offset 0x28, there are 3 timestamps of the file consisting
of the number of seconds since 01-01-1980.
At offset 0x100 comes a block list that contains pointers to 
the blocks (0xC0000 bytes per block) the file consists of.
Each entry consists of a DWORD that is the block pointer, 
followed by a DWORD that contains the offset within the block
where the chunk starts in units of 0x600 bytes followed by a DWORD
that specifies the number of 0x600 byte blocks the chunk consists
of.
This example shows inode #9 which contains a file with size
12321 bytes. Assuming a MEIHDFS start at 0x16000000, the data
can be found at byte offset 0x21F*0xC0000 + 0x16000000 = 0x2F740000
on disk.

For documentation of the DVD-RAM format, look at the dvd-vr sourcecode.
Unfortunately, the standard isn't public, therefore information about
it had to be gathered using reverse engineering.

Porting
=======
The applications compile fine on Linux and on Windows using MinGW.
Just use make.

Contact
=======
I'd love to get some feedback on this if it helped you!
Contact me at leecher@dose.0wnz.at
