/*
 * arch-tag: Decoded structures of MEIHDFS-V2.0 filesystem
 *
 * Collection of tools to read the content of MEIHDFS-V2.0 filesystem
 * used by various Panasonic DVD recorders, including PANASONIC DMR-EX768EP-K
 *
 * Copyright (C) 2012 Honza Maly <hkmaly@matfyz.cz>
 * Extended  (C) 2015 leecher@dose.0wnz.at
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#pragma pack(1)

#define ISIZE 0x1000	// Size of inode in bytes
#define BSIZE 0x800		// Size of MPEG video stream block in bytes
#define BCNT  0x180		// Allocation is done in units of (BCNT * BSIZE) bytes
#define ASIZE (BSIZE * BCNT)
#define GSIZE 0x10000	// Every (GSIZE * BCNT * BSIZE), the superblock is repeated

#define ITBL_START	(ASIZE + 0x6000)	// Start of INODE Table after superblock header

#define TIME_OFFSET 315532800	// 1980-01-01 0:00:00

typedef uint32_t uint32;
typedef uint16_t uint16;

typedef struct block_run {
    uint32 start;
    uint32 offset;
    uint32 len;
} block_run;

#define INODE_MAGIC				0x81C00001	// At least it LOOKS like magic
#define DIRECTORY_MAGIC			0x41C20001
#define ROOTDIR_MAGIC 			0x41FF0001	// Magic of root directory

#define MPEG_MAGIC	0xBA010000

#define INODE_RUNS 0x500	// Actually computed from ISIZE
#define ITBL_SZ    0x3FE	// Number of inodes in one table

typedef struct inode {
    uint32 generation;	// Ok, not sure what EXACTLY that is, but it DOES match on all stuff done last ... AND it's in superblock.
			// ... except multiple directories can have the same ... ok, I think I miss some piece of puzzle here.
    uint32 inode_id;	// This is the number which is present in directory entry ...
    uint32 i1;		// 1
    uint32 item_len;	// not sure if it can be <>1 for actuall inode ... it can be different for directory

    uint32 size;	// File size in bytes, low part
    uint32 hsize;	// File size in bytes, high part
    uint32 i2;		// no idea what this is either
    uint32 magic;	// INODE_MAGIC

    uint32 i3;		// 0
    uint32 i4;		// 0
    uint32 time1;	// Time is stored in seconds since 1980-01-01 0:00:00
    uint32 time2;	// These three times seems to be always same

    uint32 time3;
    uint32 nothing[51];	// 0

    struct block_run runs[INODE_RUNS];
} inode;

typedef struct {
	uint32 offset;	// In ISIZE units relative to start of first superblock
	uint32 hoffset;	// Offset, high part? 0 in all my samples
	uint16 i2;	// 1
	uint16 i3;	// 1
} itbl_entry;

typedef struct {
	uint32 generation;
	uint32 magic;	// For me it is always 173C, but I don't know if this is variable or magic
	uint32 i1;
	uint32 i2;
	itbl_entry entries[ITBL_SZ];	// Inode list
} itbl;

/*
 * Directory
 */

#define TYPE_FILE		1
#define TYPE_DIRECTORY	2
typedef struct dir_entry {
    uint32 inode_id;
    uint16 type;
    uint16 len;
    char filename[24];
} __attribute__((packed)) dir_entry;

/*
 * Note: The header of directory and inode obviously shares most structure.
 * It also shares it with some other stuff present in same area.
 */

#define DIR_ENTRIES_FIRST  95	// since 420 to 1000
#define DIR_ENTRIES_OTHER 103	// since 320 to 1000 ... actually, the last three looks always empty, but doesn't matter ...
//#define DIR_BEFORE_ENTRIES 31*8	// leecher: for me it is 50
#define DIR_BEFORE_ENTRIES 50*8

typedef struct directory {
    uint32 generation;
    uint32 level;	// 0 - Root, 1 - Subdirectory
    uint32 d2;		// 0
    uint32 item_len;

    uint32 entries_num;	// Actual number of directory entries
    uint32 d3;		// 0
    uint32 d4;		// 0
    uint32 magic;	// DIRECTORY_MAGIC || ROOTDIR_MAGIC ?

    uint32 d5;		// 0
    uint32 d6;		// 0
    uint32 time1;
    uint32 time2;

    uint32 time3;
    uint32 nothing[51];	// 0

    uint16 d7[DIR_BEFORE_ENTRIES];	// Not sure what this is ; every ISIZE block of directory after the first starts directly with this

    dir_entry entries[DIR_ENTRIES_FIRST];
} directory;

#pragma pack()
