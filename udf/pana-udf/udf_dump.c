/*
  $Id: udf_dump.c,v 1.0 2015/03/07 21:30:00 leecher Exp $

  Copyright (C) 2015 <leecher@dose.0wnz.at>
  
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef WIN32
#define __USE_MINGW_ANSI_STDIO 1
#endif
#include <sys/types.h>
#include "cdio/udf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

#define CEILING(x, y) ((x+(y-1))/y)

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifdef WIN32
#define mkdir(x,y) mkdir(x)
#endif

#include "udf_private.h"
static int
dump_file(char *psz_outdir, udf_dirent_t *p_udf_dirent)
{
  char psz_outfile[PATH_MAX];
  const char *psz_local_fname = udf_get_filename(p_udf_dirent);
  int fdf;
  uint64_t i_file_length = udf_get_file_length(p_udf_dirent);
  uint64_t i, i_blocks = CEILING(i_file_length, UDF_BLOCKSIZE), i_remain;
  ssize_t i_read = 0;
  unsigned int perc, last_perc=-1;

  sprintf(psz_outfile, "%s/%s", psz_outdir, psz_local_fname);
  if (udf_is_dir(p_udf_dirent))
    return mkdir(psz_outfile, 0777);

  if ((fdf = open(psz_outfile, O_WRONLY|O_CREAT|O_TRUNC|O_BINARY|O_LARGEFILE, 0666)) == -1) {
    fprintf (stderr, "Cannot create file %s: %s\n", psz_outfile, strerror(errno));
    return -1;
  }

  for (i = 0, i_remain=i_file_length; i < i_blocks ; i++, i_remain-=i_read) {
    char buf[UDF_BLOCKSIZE] = {0};
    i_read = udf_read_block(p_udf_dirent, buf, 1);

    if ((perc=(int)(((long double)i/(long double)i_blocks)*100))!=last_perc)
      printf ("\rWriting file...%d%%", (last_perc=perc));
    fflush(stdout);
    if ( i_read < 0 ) {
      fprintf(stderr, "Error reading UDF file %s at block %llu\n",
              psz_local_fname, i);
      close(fdf);
      return -2;
    }

    if (i_remain<UDF_BLOCKSIZE && i_read>=i_remain) i_read=i_remain;
    if (write (fdf, buf, i_read) != i_read) {
      perror("Error writing data");
      close(fdf);
      return -3;
    }
  }
  close(fdf);
  printf("\r%79s\r", " ");
  return 0;
}

static void 
print_file_info(const udf_dirent_t *p_udf_dirent, const char* psz_dirname)
{
  time_t mod_time = udf_get_modification_time(p_udf_dirent);
  char psz_mode[11]="invalid", *psz_time = ctime(&mod_time);
  const char *psz_fname=udf_get_filename(p_udf_dirent);
  uint64_t fsize = udf_get_file_length(p_udf_dirent);

  /* Print directory attributes*/
  printf("%6llu%s ", fsize<1024?fsize:(fsize<1024*1024?fsize/1024:fsize/1024/1024),
        fsize<1024?" ":(fsize<1024*1024?"k":"M"));
  printf("%.*s %s%s\n",  strlen(psz_time)-1, psz_time, psz_dirname?psz_dirname:"", 
    !udf_is_dir(p_udf_dirent)?(*psz_fname? psz_fname : "/"):(psz_dirname&&*psz_dirname?"":"/"));
}

static udf_dirent_t *
list_files(udf_t *p_udf, udf_dirent_t *p_udf_dirent, const char *psz_path, const char *psz_dest)
{
  char sz_path[PATH_MAX], sz_newpath[PATH_MAX];

  if (!p_udf_dirent) return NULL;
  
  print_file_info(p_udf_dirent, psz_path);

  while (udf_readdir(p_udf_dirent)) {

    if (psz_dest) snprintf(sz_path, sizeof(sz_path), "%s/%s", psz_dest, psz_path?psz_path:"/");
    if (udf_is_dir(p_udf_dirent)) {     
      udf_dirent_t *p_udf_dirent2 = udf_opendir(p_udf_dirent);
      if (psz_dest) mkdir(sz_path, 0777);
      if (p_udf_dirent2) {
        snprintf(sz_newpath, sizeof(sz_newpath), "%s%s/", psz_path, udf_get_filename(p_udf_dirent));
        list_files(p_udf, p_udf_dirent2, sz_newpath, psz_dest);
      }
    } else {
      print_file_info(p_udf_dirent, psz_path);
      if (psz_dest) dump_file(sz_path, p_udf_dirent);
    }
  }
  return p_udf_dirent;
}

int
main(int argc, const char *argv[])
{
  udf_t *p_udf;

  printf ("udf_dump V1.0 - (c) leecher@dose.0wnz.at, 2015\n\n");
  if (argc < 2) 
  {
    printf ("Usage: %s <UDF image> [Dest dir]\n", argv[0]);
    return 1;
  }

  p_udf = udf_open (argv[1]);
  
  if (NULL == p_udf) {
    fprintf(stderr, "Sorry, couldn't open %s as something using UDF\n", 
	    argv[1]);
    return 1;
  } else {
    udf_dirent_t *p_udf_root = udf_get_root(p_udf);
    if (NULL == p_udf_root) {
      fprintf(stderr, "Sorry, couldn't find / in %s\n", 
	      argv[1]);
      return 1;
    }
    
    list_files(p_udf, p_udf_root, "", argc>2?argv[2]:NULL);
  }
  
  udf_close(p_udf);
  return 0;
}

