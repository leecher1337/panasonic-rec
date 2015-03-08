/*
  $Id: udf_private.h,v 1.12 2008/04/18 16:02:10 karl Exp $

  Copyright (C) 2005, 2006, 2008 Rocky Bernstein <rocky@gnu.org>

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

#ifndef __CDIO_UDF_PRIVATE_H__
#define __CDIO_UDF_PRIVATE_H__

#if defined(HAVE_CONFIG_H) && !defined(LIBCDIO_CONFIG_H)
#include "config.h"
#endif

#include <cdio/types.h>
#include <cdio/ecma_167.h>
#include "cdio/udf.h"

/* Implementation of opaque types */

struct udf_s {
  ssize_t               i_position; /* Position in file if positive. */
  int                   stream;  /* Stream pointer if stream */
  uint32_t              i_part_start; /* start of Partition Descriptor */
  uint32_t              fsd_offset;   /* lba of fileset descriptor */
};

struct udf_dirent_s
{
  char	            *psz_name;
  bool	             b_dir;    /* true if this entry is a directory. */
  bool               b_parent; /* True if has parent directory (e.g. not root
				  directory). If not set b_dir will probably
				  be true. */
  udf_t             *p_udf;
  uint32_t           i_part_start;
  uint32_t           i_loc, i_loc_end;
  uint64_t           dir_left;
  uint8_t           *sector;
  udf_fileid_desc_t *fid;

 /* This field has to come last because it is variable in length. */
  udf_file_entry_t   fe;
};

bool udf_get_lba(const udf_file_entry_t *p_udf_fe, 
                 /*out*/ uint32_t *start, /*out*/ uint32_t *end);

#endif /* __CDIO_UDF_PRIVATE_H__ */


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
