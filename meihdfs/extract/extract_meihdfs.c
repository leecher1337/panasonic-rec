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

typedef struct
{
	int fdd;		// File descriptor of disk file
	off64_t start;	// Start address within file
	int ver;		// Filesystem version
} EXTRINST;

#define FILETIME(tim) (tim + (pInst->ver<3?TIME_OFFSET:0))

int search_hdr(EXTRINST *pInst)
{
	char buffer[512];

	for (; lseek64(pInst->fdd, pInst->start, SEEK_SET)!=(off64_t)-1; pInst->start+=0x10000)
	{
		if(read(pInst->fdd, buffer, sizeof(buffer))!=sizeof(buffer))
		{
			fprintf(stderr, "Read error @%10llX: %s\n", pInst->start, strerror(errno));
			return -1;
		}
		printf ("\rSearching MEIHDFS header...%10llX", pInst->start);
		fflush(stdout);
		if(memcmp(buffer+8, "MEIHDFS-V2.", 11) == 0 || memcmp(buffer+8, "HDFS2.", 6) == 0)
		{
			printf (" FOUND!\n");
			return (pInst->ver = buffer[8]=='M'?buffer[19]-'0':buffer[14]-'0');
		}
	}
	fprintf(stderr, "\nHeader could not be found!\n");
	return -2;
}

int list_file(EXTRINST *pInst, inode *inode, char *outfile)
{
	time_t ttime;
	struct tm *btime;
	off64_t fsize;

	ttime = FILETIME(inode->time1);
	btime = gmtime(&ttime);
	fsize = ((off64_t)inode->hsize << 32) + inode->size;	// Or are the higher bits of size elsewhere?
	printf("%4i-%02i-%02i %02i:%02i:%02i %20lld %s\n", btime->tm_year + 1900, 
		btime->tm_mon+1, btime->tm_mday, btime->tm_hour, 
		btime->tm_min, btime->tm_sec, fsize, outfile);
}

int dump_file(EXTRINST *pInst, inode *inode, char *outfile)
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
	
	ttime = FILETIME(inode->time1);
	btime = gmtime(&ttime);
	fsize = origfsize = ((off64_t)inode->hsize << 32) + inode->size;	// Or are the higher bits of size elsewhere?
	printf("%4i-%02i-%02i %02i:%02i:%02i %6lld%s %s\n", btime->tm_year + 1900, 
		btime->tm_mon+1, btime->tm_mday, btime->tm_hour, 
		btime->tm_min, btime->tm_sec, fsize<1024?fsize:(fsize<1024*1024?fsize/1024:fsize/1024/1024),
		fsize<1024?" ":(fsize<1024*1024?"k":"M"), outfile);
	for(j = 0, written=0;j < INODE_RUNS && inode->runs[j].start; j++)
	{
		if (lseek64(pInst->fdd, pInst->start + (off64_t)inode->runs[j].start * ASIZE + (off64_t)inode->runs[j].offset * BCNT * 4, SEEK_SET) == (off64_t)-1)
		{
			fprintf (stderr, "Seek error: %s\n", strerror(errno));
			close(fdf);
			return -1;
		}
		for (size = inode->runs[j].len * inode->factor; size>0 && fsize>0; size -= BCNT * 4, written+=r)
		{
			r = (size > BCNT * 4) ? (BCNT * BSIZE) : ((size * BSIZE) / 4);
			printf("\rCopying run %02i starting at block %08X with len %08X [%03d%%]", j, inode->runs[j].start, 
				inode->runs[j].len, (int)((double)written/(double)origfsize*100));
			fflush(stdout);
			if(read(pInst->fdd, buffer, r) < 0)
			{
				fprintf(stderr, "Error reading block %i: %s\n", inode->runs[j].start, strerror(errno));
				close(fdf);
				return -1;
			}
			if (fsize<r) r=fsize;
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
#define ITABLES_V20	6
#define ITABLES_V23 9
#define ITABLES_MAX ITABLES_V23

int read_itbl(int fdd, off64_t start, itbl *itble, int itables)
{
	/* I don't know yet how they can be found, normally they are at these offsets, but not always: */
	//off64_t itbl_offsets[] = {0, 0x1000, 0x2000, 0xF000, 0x10000, 0x11000};
	int i, cnt;
	itbl_entry itbl_zro={0};

	if (lseek64(fdd, start + ITBL_START, SEEK_SET) == (off64_t)-1)
	{
		fprintf (stderr, "Cannot seek to start of Inode directory @%10llX: %s\n", 
			start + ITBL_START, strerror(errno));
		return -1;
	}

	/* Try to find inode tables by pattern matching, as I don't know how they are referenced yet :( */	
	for (i=0, cnt=0; i<0x20000; i+=ISIZE)
	{
		if (read(fdd, &itble[cnt], sizeof(itble[0])) != sizeof(itble[0]))
		{
			fprintf (stderr, "Cannot read Inode directory @%10llX: %s\n", 
				start + ITBL_START + i, strerror(errno));
			return -1;
		}
		if ((cnt>0 && cnt!=3) || (itble[cnt].generation>0 && itble[cnt].generation<=0xFFFF && itble[cnt].i0 && !itble[cnt].i1 && !itble[cnt].i2))
		{
			/* Header match, now validate if there are valid entries */
			int bValid, j;

			if (itble[cnt].generation)
			{
				for (j=0,bValid=0; j<ITBL_SZ; j++)
				{
					if (memcmp(&itble[cnt].entries[j], &itbl_zro, sizeof(itbl_zro)) == 0) continue;
					bValid = itble[cnt].entries[j].offset && itble[cnt].entries[j].i2==1 && itble[cnt].entries[j].i3==1;
				}
			} else bValid = 1;
			if (bValid)
			{
				printf ("Inode table #%d/%d found @%10llX\n", ++cnt, itables, start + ITBL_START + i);
			}
		}
		if (cnt == itables) return 0;
	}

	// It seems that there are HDDs where not all inode tables are present...
	fprintf (stderr, "Warning: Cannot find all inode tables.\n");
	return 0;
}

int dump_dir(EXTRINST *pInst, off64_t dir_offset, itbl *itble, int itables, directory *dir, char *outdir, int list)
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
    		if (lseek64(pInst->fdd, (offset = dir_offset + j * ISIZE), SEEK_SET) == (off64_t)-1 ||
    			(rd=read(pInst->fdd, &lpage, sizeof(lpage))) != sizeof(lpage))
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
			if (page->entries[i].inode_id > itables*ITBL_SZ)
			{
				fprintf(stderr, "Inode %d (#%d @%10llX (pg %d)) exceeds size of available inode tables.\n", page->entries[i].inode_id, i, dir_offset + j * ISIZE, j);
				return -1;
			}
    		if (lseek64(pInst->fdd, (offset = pInst->start + INODE_OFFSET(itble,page->entries[i].inode_id) * ISIZE),
    			 SEEK_SET) == (off64_t)-1 ||
    			(rd=read(pInst->fdd, buffer, sizeof(buffer))) != sizeof(buffer))
    		{
    			fprintf (stderr, "Dir entry %d: Cannot read INODE %d @%10llX [rd=%d]: %s\n", 
    				i, page->entries[i].inode_id, offset, rd, strerror(errno));
    			return -1;
    		}

    		sprintf(file, "%s/%.*s", outdir, page->entries[i].len, page->entries[i].filename);

    		/* Dump inode to filesystem */
    		switch (page->entries[i].type)
    		{
    		case TYPE_FILE:
    			inod = (inode*)buffer;
    			if ((inod->magic & INODE_MAGIC_MASK) != INODE_MAGIC_GEN)
    			{
    				fprintf (stderr, "Dir entry %d: INODE %d is not a file inode (magic=%08X)\n", 
    					i, page->entries[i].inode_id, inod->magic);
    				return -1;
    			}
    			if ((inod->hsize>0 || inod->size>0) && !inod->runs[0].start)
    			{
    				itbl itbl1[ITABLES_MAX]={0};
    				int j;

	    			// This is an incomplete inode search backup inode tables if there are other inode ptrs in there
    				for (j=1; read_itbl(pInst->fdd, pInst->start + j*(off64_t)GSIZE*(off64_t)ASIZE, itbl1, itables)>=0; j++)
    				{
    					if ((offset = INODE_OFFSET(itbl1,page->entries[i].inode_id)) != INODE_OFFSET(itble,page->entries[i].inode_id))
    					{
    						if (lseek64(pInst->fdd, (offset = pInst->start + offset * ISIZE), SEEK_SET) != (off64_t)-1 &&
    							read(pInst->fdd, buffer, sizeof(buffer)) == sizeof(buffer) && inod->runs[0].start)
	    						break;
    					}
    				}
    			}
    			if (!list) dump_file(pInst, inod, file); else list_file(pInst, inod, file);
    			utb.actime=utb.modtime=FILETIME(inod->time1);
    			break;
    		case TYPE_DIRECTORY:
    			idir = (directory*)buffer;
    			if ((idir->magic & DIRECTORY_MAGIC_MASK) != DIRECTORY_MAGIC_GEN)
    			{
    				fprintf (stderr, "Dir entry %d: INODE %d is not a directory (magic=%08X)\n", i, dir->entries[i].inode_id, idir->magic);
    				return -1;
    			}
    			mkdir(file,0775);
    			dump_dir(pInst, offset, itble, itables, idir, file, list);
    			utb.actime=utb.modtime=FILETIME(idir->time1);
    			break;
    		}
    		utime(file, &utb);
			if (page->entries[i].len>sizeof(page->entries[i].filename))
			{
				fprintf (stderr, "Info: filename length exceeds directory entry size, ending directory traversal.\n");
				break;
			}
        }
	}
	return 0;
}

int main(int argc, char **argv)
{
	EXTRINST inst={0};
	off64_t offset;
	itbl itbl[ITABLES_MAX]={0};
	directory root;
	int ret, itables, as=1;

	printf ("extract_meihdfs V1.7 - (c) leecher@dose.0wnz.at, 2016\n\n");
	if (argc<2)
	{
		printf ("Usage: %s [-s<Start>] <Image> <Output dir>\n\n", argv[0]);
		printf ("\t-s\tOptional hex offset where to start searching header\n\ti.e.: -s0xA4000000 \n");
		return -1;
	}

	if (sscanf(argv[as], "-s0x%llx", &inst.start) > 0)
	{
		as++;
		printf ("Using user supplied start offset %08X\n", inst.start);
	}

	inst.fdd = open(argv[as], O_RDONLY|O_LARGEFILE|O_BINARY);
	if(inst.fdd == -1)
	{
		fprintf(stderr, "Error opening image %s:%s\n", argv[as], strerror(errno));
		return -1;
	}

	/* Search header, read INODE directories */
	if (search_hdr(&inst)<0 || read_itbl(inst.fdd, inst.start, itbl, (itables=inst.ver<3?ITABLES_V20:ITABLES_V23))<0)
	{
		close(inst.fdd);
		return -1;
	}

	/* Seek to INODE 0 (root directory) and read it */
	if (lseek64(inst.fdd, (offset = inst.start + INODE_OFFSET(itbl,0) * ISIZE), SEEK_SET) == (off64_t)-1 ||
		read(inst.fdd, &root, sizeof(root)) != sizeof(root))
	{
		fprintf (stderr, "Cannot read root directory @%10llX: %s\n", offset, strerror(errno));
		close(inst.fdd);
		return -1;
	}

	if (root.magic != ROOTDIR_MAGIC)
	{
		fprintf (stderr, "Rootdirectory @%10llX doesn't have valid rootdir magic (magic = %08X).\n", offset, root.magic);
		close(inst.fdd);
		return -1;
	}

	as++;
	ret = dump_dir(&inst, offset, itbl, itables, &root, argc>as?argv[as]:".", argc<=as);
	close(inst.fdd);
	return ret;
}
