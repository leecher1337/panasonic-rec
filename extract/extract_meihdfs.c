/*
 * arch-tag: Dumping program for MEIHDFS-V2.0 filesystem
 *
 * Tool to read the content of MEIHDFS-V2.0 filesystem
 * used by various Panasonic DVD recorders, including PANASONIC DMR-EX768EP-K
 *
 * Copyright (C) 2015 <leecher@dose.0wnz.at>
 * Based on initial research by:
 * Copyright (C) 2012 Honza Maly <hkmaly@matfyz.cz>
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

#define _LARGEFILE64_SOURCE
#ifdef WIN32
#define __USE_MINGW_ANSI_STDIO 1
#endif
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <utime.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <stdint.h>
#include "meihdfs1.h"

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifdef WIN32
#define mkdir(x,y) mkdir(x)
#endif


int search_hdr(int fdd, off64_t *offset)
{
	char buffer[32];

	for (*offset=0; lseek64(fdd, *offset, SEEK_SET)!=(off64_t)-1; *offset+=0x10000)
	{
		if(read(fdd, buffer, sizeof(buffer))!=sizeof(buffer))
		{
			fprintf(stderr, "Read error @%10llX: %s\n", *offset, strerror(errno));
			return -1;
		}
		printf ("\rSearching MEIHDFS header...%10llX", *offset);
		fflush(stdout);
		if(memcmp(buffer+8, "MEIHDFS-V2.0\0\0\0\0HDD\0", 20) == 0)
		{
			printf (" FOUND!\n");
			return 0;
		}
	}
	fprintf(stderr, "\nHeader could not be found!\n");
	return -2;
}

int dump_file(int fdd, off64_t start, inode *inode, char *outfile)
{
	int fdf;
	time_t ttime;
	struct tm *btime;
	off64_t fsize, origfsize, written;
	int r, j, size;
	char buffer[ASIZE];

	if ((fdf = open(outfile, O_WRONLY|O_CREAT|O_TRUNC|O_BINARY|O_LARGEFILE, 0666)) == -1)
	{
		fprintf (stderr, "Cannot create file %s: %s\n", outfile, strerror(errno));
		return -1;
	}
	
	ttime = inode->time1 + TIME_OFFSET;
	btime = gmtime(&ttime);
	fsize = origfsize = ((off64_t)inode->hsize << 32) + inode->size;	// Or are the higher bits of size elsewhere?
	printf("%4i-%02i-%02i %02i:%02i:%02i %6lld%s %s\n", btime->tm_year + 1900, 
		btime->tm_mon+1, btime->tm_mday, btime->tm_hour, 
		btime->tm_min, btime->tm_sec, fsize<1024?fsize:(fsize<1024*1024?fsize/1024:fsize/1024/1024),
		fsize<1024?" ":(fsize<1024*1024?"k":"M"), outfile);
	for(j = 0, written=0;j < INODE_RUNS && inode->runs[j].start; j++)
	{
		if (lseek64(fdd, start + (off64_t)inode->runs[j].start * ASIZE + (off64_t)inode->runs[j].offset * BCNT * 4, SEEK_SET) == (off64_t)-1)
		{
			fprintf (stderr, "Seek error: %s\n", strerror(errno));
			close(fdf);
			return -1;
		}
		for (size = inode->runs[j].len; size>0 && fsize>0; size -= BCNT * 4, written+=r)
		{
			r = (size > BCNT * 4) ? (BCNT * BSIZE) : ((size * BSIZE) / 4);
			if (fsize<r) r=fsize;
			printf("\rCopying run %02i starting at block %08X with len %08X [%03d%%]", j, inode->runs[j].start, 
				inode->runs[j].len, (int)((double)written/(double)origfsize*100));
			fflush(stdout);
			if(read(fdd, buffer, r) < 0)
			{
				fprintf(stderr, "Error reading block %i: %s\n", inode->runs[j].start, strerror(errno));
				close(fdf);
				return -1;
			}
			fsize-=r;
			if(write(fdf, buffer, r)!=r)
			{
			    fprintf(stderr, "Error writing file %s: %s\n",outfile, strerror(errno));
				close(fdf);
				return -1;
			}
		}
	}
	close(fdf);
	printf ("\r%-79s\r", " ");
	return 0;
}

#define INODE_OFFSET(tbl,idx) \
	(((off64_t)tbl[idx/ITBL_SZ].entries[idx%ITBL_SZ].hoffset<<32)+tbl[idx/ITBL_SZ].entries[idx%ITBL_SZ].offset)
#define ITABLES	6

int read_itbl(int fdd, off64_t start, itbl *itble)
{
	/* I don't know yet how they can be found, so they are just hardcoded atm */
	off64_t itbl_offsets[] = {
		ITBL_START, ITBL_START+0x1000, ITBL_START+0x2000, 
		ITBL_START+0xF000, ITBL_START+0x10000, ITBL_START+0x11000};
	int i;

	/* Read INODE directories (don't know how they are referenced yet :( ) */
	for (i=0; i<ITABLES; i++)
	{
		if (lseek64(fdd, start + itbl_offsets[i], SEEK_SET) == (off64_t)-1 ||
			read(fdd, &itble[i], sizeof(itble[0])) != sizeof(itble[0]))
		{
			fprintf (stderr, "Cannot read Inode directory @%10llX: %s\n", 
				start + itbl_offsets[i], strerror(errno));
			return -1;
		}
	}
	return 0;
}

int dump_dir(int fdd, off64_t start, off64_t dir_offset, itbl *itble, directory *dir, char *outdir)
{
	int i, j, page_len;
	char file[PATH_MAX], buffer[ISIZE];
	directory *idir;
	inode *inod;
	off64_t offset;
	ssize_t rd=-1;
	struct utimbuf utb={0};
    dir_page *page, lpage;

    page = (dir_page*)&dir->d7;
    for (j=0, page_len=DIR_ENTRIES_FIRST; j<dir->item_len; j++)
    {
        if (j)
        {
            /* Seek to next directory entry */
    		if (lseek64(fdd, (offset = dir_offset + j * ISIZE), SEEK_SET) == (off64_t)-1 ||
    			(rd=read(fdd, &lpage, sizeof(lpage))) != sizeof(lpage))
    		{
    			fprintf (stderr, "Cannot read directory page %d @%10llX [rd=%d]: %s\n", 
    				j, offset, rd, strerror(errno));
    			return -1;
    		}
            page=&lpage; page_len=DIR_ENTRIES_OTHER;
        }
    	for (i=0; i<page_len; i++)
	    {
    		/* Skip deleted entries */
    		if (!page->entries[i].inode_id || page->entries[i].inode_id == -1) continue;

    		/* Seek to given INODE */
    		if (lseek64(fdd, (offset = start + INODE_OFFSET(itble,page->entries[i].inode_id) * ISIZE),
    			 SEEK_SET) == (off64_t)-1 ||
    			(rd=read(fdd, buffer, sizeof(buffer))) != sizeof(buffer))
    		{
    			fprintf (stderr, "Dir entry %d: Cannot read INODE %d @%10llX [rd=%d]: %s\n", 
    				i, page->entries[i].inode_id, offset, rd, strerror(errno));
    			return -1;
    		}

    		sprintf(file, "%s/%s", outdir, page->entries[i].filename);

    		/* Dump inode to filesystem */
    		switch (page->entries[i].type)
    		{
    		case TYPE_FILE:
    			inod = (inode*)buffer;
    			if (inod->magic != INODE_MAGIC)
    			{
    				fprintf (stderr, "Dir entry %d: INODE %d is not a file inode (magic=%08X)\n", 
    					i, page->entries[i].inode_id, inod->magic);
    				return -1;
    			}
    			if (!inod->runs[0].start)
    			{
    				itbl itbl1[ITABLES]={0};
    				int j;

	    			// This is an incomplete inode search backup inode tables if there are other inode ptrs in there
    				for (j=1; read_itbl(fdd, start + j*(off64_t)GSIZE*(off64_t)ASIZE, itbl1)>=0; j++)
    				{
    					if (INODE_OFFSET(itbl1,page->entries[i].inode_id) != INODE_OFFSET(itble,page->entries[i].inode_id))
    					{
    						if (lseek64(fdd, (offset = start + INODE_OFFSET(itbl1,page->entries[i].inode_id) * ISIZE), 
	    						SEEK_SET) != (off64_t)-1 &&
    							read(fdd, buffer, sizeof(buffer)) == sizeof(buffer) && inod->runs[0].start)
	    						break;
    					}
    				}
    			}
    			dump_file(fdd, start, inod, file);
    			utb.actime=utb.modtime=inod->time1 + TIME_OFFSET;
    			break;
    		case TYPE_DIRECTORY:
    			idir = (directory*)buffer;
    			if (idir->magic != DIRECTORY_MAGIC)
    			{
    				fprintf (stderr, "Dir entry %d: INODE %d is not a directory (magic=%08X)\n", i, dir->entries[i].inode_id, idir->magic);
    				return -1;
    			}
    			mkdir(file,0775);
    			dump_dir(fdd, start, offset, itble, idir, file);
    			utb.actime=utb.modtime=idir->time1 + TIME_OFFSET;
    			break;
    		}
    		utime(file, &utb);
        }
	}
	return 0;
}

int main(int argc, char **argv)
{
	int fdd;
	off64_t start, offset;
	itbl itbl[ITABLES]={0};
	directory root;
	int ret;

	printf ("extract_meihdfs V1.3 - (c) leecher@dose.0wnz.at, 2015\n\n");
	if (argc<3)
	{
		printf ("Usage: %s <Image> <Output dir>\n", argv[0]);
		return -1;
	}

	fdd = open(argv[1], O_RDONLY|O_LARGEFILE|O_BINARY);
	if(fdd == -1)
	{
		fprintf(stderr, "Error opening image %s:%s\n", argv[1], strerror(errno));
		return -1;
	}

	/* Search header, read INODE directories */
	if (search_hdr(fdd, &start)<0 || read_itbl(fdd, start, itbl)<0)
	{
		close(fdd);
		return -1;
	}

	/* Seek to INODE 0 (root directory) and read it */
	if (lseek64(fdd, (offset = start + INODE_OFFSET(itbl,0) * ISIZE), SEEK_SET) == (off64_t)-1 ||
		read(fdd, &root, sizeof(root)) != sizeof(root))
	{
		fprintf (stderr, "Cannot read root directory @%10llX: %s\n", offset, strerror(errno));
		close(fdd);
		return -1;
	}
	if (root.magic != ROOTDIR_MAGIC)
	{
		fprintf (stderr, "Rootdirectory @%10llX doesn't have valid rootdir magic (magic = %08X).\n", offset, root.magic);
		close(fdd);
		return -1;
	}

	ret = dump_dir(fdd, start, offset, itbl, &root, argv[2]);
	close(fdd);
	return ret;
}
