There seem to be at least 2 types of Filesystems in use on Panasonic DVR recorders:
The oder ones use UDF, the newer ones seem to have a custom file system called
MEIHDFS-V2.0. So if the harddisk in such a DVR-Recorder dies, it is
difficult to recover data from it, because the filesystem is entirely
undocumented and proprietary.
But there may be hope! This tool is an attempt to parse the filesystem
structures and extract the files.

I made 2 different tools, one is for UDF variant and one for MEIHDFS2.

# UDF

For further information on how to use it, please refer to the [README](https://github.com/leecher1337/panasonic-rec/blob/master/udf/README.txt) file. 

For the impatient ones, assuming you have a disk image image.dd and a recovery destination directory f:\dump, just do:

`udf_dump image.dd f:\dump`

After successful extraction, you can do this to extract the programs:

`dvd-vr VR_MANGR.IFO VR_MOVIE.VRO`

Windows Binary: [udf_dump.zip](http://dose.0wnz.at/scripts/cpp/udf_dump.zip)
For Linux, just type make to compile each program.


# MEIHDFS

For further information on how to use it and for technical details about the filesystem structure, please refer to the [README](https://github.com/leecher1337/panasonic-rec/blob/master/meihdfs/README.txt) file. 
For the impatient ones, assuming you have a disk image image.dd and a recovery destination directory f:\dump, just do:

`extract_meihdfs image.dd f:\dump`

After successful extraction, you can do this to extract the programs:

`for /r "F:\dump\DVD_RTAV\" %I in (*.VRO) do dvd-vr -n [ts]-[label] "%~dI%~pI%~nI.IFO" "%~I"`

Windows Binary: [meihdfs.zip](http://dose.0wnz.at/scripts/cpp/meihdfs.zip)
For Linux, just type make to compile each program.

# Other Recorders
For Pioneer DVR recorders, you can have a look at my [pioneer-rec](https://github.com/leecher1337/pioneer-rec/) project.
