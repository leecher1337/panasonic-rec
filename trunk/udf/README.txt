The problem
===========
Older Panasonic DVR-Recorders seem to have a file system based on UDF.
There are a lot of utilities out there that can read UDF, but the UDF
filesstem an Panasonic Harddisks doesn't have volume or partition 
descriptors so images cannot be mounted without little modifications
in such utilities. So I wrote a little dumping utility based on libudf
sourcecode that should be able to dump the UDF filesystem of your
Panasonic recorder.

Create a HDD image
==================
First you have to remove the harddisk from the DVR device. 
After removing the HDD, you can mount it to your PC, i.e. with an USB
Adapter or (preferably) directly to the IDE interface.

If the PC recognizes the drive, you may have luck and it is only partly 
damaged and data can be rescued. However if you want to make sure, you
may want to consult a data recovery company to do a professional rescue
of the harddisk image, because the harddisk may die during dumping!

So the next thing to do is to dump the contens of the disk to an image
file so you need to have at least the size of the HDD
available on one of your harddisks to make an image. 
You may need up to the same size for the extracted movie data then, 
but this can also be on another drive then. 
There are many tools available for dumping a harddisk to an image file,
i.e. Datarescue's drdd:

http://www.datarescue.com/photorescue/v3/drdd.htm

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

You need to have an empty destination directory where the VOB files will
be recovered to. Assuming your destination directory is f:\dump just issue:

udf_dump image.dd f:\dump

This will extract a DVD-RAM directory structure like it can be found on
the harddisk:

/
  DVD_RTAV/
    VR_MANGR.IFO
    VR_MANGR.BUP
    VR_MOVIE.VRO

The .VRO file contains the video data and the .IFO contains
the number of programs, video format info, etc.
The .BUP files are BackUP files for the .IFO files, so they normally
contain the same data as the .IFO files.

These are standard DVD-RAM files, so they can be split using 
appropriate utilities.

Extracting the movies from the files
====================================
After having dumped the directory structure mentioned above, you need
to extract the various programs from the VR_MOVIE.VRO file.
This can be done with the dvd-vr tool from pixelbeat:
http://www.pixelbeat.org/programs/dvd-vr/
I slightly modified the sourcecode so that it compiles on MinGW, but
CONTRARY TO THE MEIHDFS-DUMP DVD-VR VERSION, this is just the original
version of the program.
So you need to run this too with the VR_MANGR.IFO and VR_MOVIE.VRO file
as parameter:

dvd-vr VR_MANGR.IFO VR_MOVIE.VRO

This will extract the programs from the file which can then finally
be used as videos.
If you just want to display the info from the .IFO file, omit the
second parameter.

Extracting movies from severely damaged HDDs
============================================
If the extract tool cannot find the UDF-header or - more likely - 
your image is just too damaged to hold a valid filesystem structure, 
you may want to try a different approach. This one was described
by Stefan Haliner in the avsforum. He basically made a python script that
extracts all consecutive mpeg-blocks from an image and assembles them 
together. You can fetch his script here:

https://github.com/haliner/dvr-recover

However this tool requires you to install the python framework, so if you
want a simple executable to do dumping, use my pioneer_rec tool
and read the README.txt section about extraction there:

https://code.google.com/p/pioneer-rec/

Acknowledgements
================
As already mentioned, the dvd-vr tool is taken from  
http://www.pixelbeat.org/programs/dvd-vr/

Technical details
=================
See UDF specification.
http://www.osta.org/specs/

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
