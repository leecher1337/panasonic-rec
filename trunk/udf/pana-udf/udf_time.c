/* 
  $Id: udf_time.c,v 1.10 2008/04/24 07:28:00 rocky Exp $

  Copyright (C) 2005, 2008 Rocky Bernstein <rocky@gnu.org>
  Copyright (C) 1993, 1994, 1995, 1996, 1997 Free Software Foundation, Inc.

  Modified From part of the GNU C Library.
  Contributed by Paul Eggert.

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

/* Some history from the GNU/Linux kernel from which this is also taken...
   dgb 10/02/98: ripped this from glibc source to help convert
                 timestamps to unix time

       10/04/98: added new table-based lookup after seeing how ugly the
                 gnu code is
  
   blf 09/27/99: ripped out all the old code and inserted new table from
                 John Brockmeyer (without leap second corrections)
                 rewrote udf_stamp_to_time and fixed timezone
                 accounting in udf_timespec_to_stamp.
*/

/*
 * We don't take into account leap seconds. This may be correct or incorrect.
 * For more NIST information (especially dealing with leap seconds), see:
 *  http://www.boulder.nist.gov/timefreq/pubs/bulletin/leapsecond.htm
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef NEED_TIMEZONEVAR
#define timezonevar 1
#endif

#include "udf_private.h"
#include <cdio/udf.h>

/**
   Imagine the below enum values as #define'd or constant values
   rather than distinct values of an enum.
*/
enum {
  HOURS_PER_DAY    =   24,
  SECS_PER_MINUTE  =   60,
  MAX_YEAR_SECONDS =   69,
  DAYS_PER_YEAR    =  365,  /* That is, in most of the years. */
  EPOCH_YEAR       = 1970,
  SECS_PER_HOUR	   = (60 * SECS_PER_MINUTE),
  SECS_PER_DAY	   = SECS_PER_HOUR * HOURS_PER_DAY
} debug_udf_time_enum;
  
#ifndef __isleap
/* Nonzero if YEAR is a leap year (every 4 years,
   except every 100th isn't, and every 400th is).  */
#define	__isleap(year)	\
  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))
#endif

/* How many days come before each month (0-12).  */
static const unsigned short int __mon_yday[2][13] =
  {
    /* Normal years.  */
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, DAYS_PER_YEAR },
    /* Leap years.  */
    { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, DAYS_PER_YEAR+1 }
  };

#define SPY(y,l,s) (SECS_PER_DAY * (DAYS_PER_YEAR*y+l)+s) /* Seconds per year */

static time_t year_seconds[MAX_YEAR_SECONDS]= {
  /*1970*/ SPY( 0, 0,0), SPY( 1, 0,0), SPY( 2, 0,0), SPY( 3, 1,0), 
  /*1974*/ SPY( 4, 1,0), SPY( 5, 1,0), SPY( 6, 1,0), SPY( 7, 2,0), 
  /*1978*/ SPY( 8, 2,0), SPY( 9, 2,0), SPY(10, 2,0), SPY(11, 3,0), 
  /*1982*/ SPY(12, 3,0), SPY(13, 3,0), SPY(14, 3,0), SPY(15, 4,0), 
  /*1986*/ SPY(16, 4,0), SPY(17, 4,0), SPY(18, 4,0), SPY(19, 5,0), 
  /*1990*/ SPY(20, 5,0), SPY(21, 5,0), SPY(22, 5,0), SPY(23, 6,0), 
  /*1994*/ SPY(24, 6,0), SPY(25, 6,0), SPY(26, 6,0), SPY(27, 7,0), 
  /*1998*/ SPY(28, 7,0), SPY(29, 7,0), SPY(30, 7,0), SPY(31, 8,0), 
  /*2002*/ SPY(32, 8,0), SPY(33, 8,0), SPY(34, 8,0), SPY(35, 9,0), 
  /*2006*/ SPY(36, 9,0), SPY(37, 9,0), SPY(38, 9,0), SPY(39,10,0), 
  /*2010*/ SPY(40,10,0), SPY(41,10,0), SPY(42,10,0), SPY(43,11,0), 
  /*2014*/ SPY(44,11,0), SPY(45,11,0), SPY(46,11,0), SPY(47,12,0), 
  /*2018*/ SPY(48,12,0), SPY(49,12,0), SPY(50,12,0), SPY(51,13,0), 
  /*2022*/ SPY(52,13,0), SPY(53,13,0), SPY(54,13,0), SPY(55,14,0), 
  /*2026*/ SPY(56,14,0), SPY(57,14,0), SPY(58,14,0), SPY(59,15,0), 
  /*2030*/ SPY(60,15,0), SPY(61,15,0), SPY(62,15,0), SPY(63,16,0), 
  /*2034*/ SPY(64,16,0), SPY(65,16,0), SPY(66,16,0), SPY(67,17,0), 
  /*2038*/ SPY(68,17,0)
};

#ifdef HAVE_TIMEZONE_VAR
extern long timezone;
#endif

time_t *
udf_stamp_to_time(time_t *dest, long int *dest_usec, 
		  const udf_timestamp_t src)
{
  int yday;
  uint8_t type = src.type_tz >> 12;
  int16_t offset;
  
  if (type == 1) {
    offset = src.type_tz << 4;
    /* sign extent offset */
    offset = (offset >> 4);
    if (offset == -2047) /* unspecified offset */
      offset = 0;
  }
  else
    offset = 0;
  
  if ((src.year < EPOCH_YEAR) ||
      (src.year >= EPOCH_YEAR+MAX_YEAR_SECONDS))
    {
      *dest = -1;
      *dest_usec = -1;
      return NULL;
    }
  *dest = year_seconds[src.year - EPOCH_YEAR];
  *dest -= offset * SECS_PER_MINUTE;
  
  yday = ((__mon_yday[__isleap (src.year)]
	   [src.month-1]) + (src.day-1));
  *dest += src.second + 
    ( SECS_PER_MINUTE *
      ( ( (yday* HOURS_PER_DAY) + src.hour ) * 60 + src.minute ) );

  *dest_usec = src.microseconds
    + (src.centiseconds * 10000)
    + (src.hundreds_of_microseconds * 100);
  return dest;
}

#ifdef HAVE_STRUCT_TIMESPEC
/*!
  Convert a UDF timestamp to a time_t. If microseconds are desired,
  use dest_usec. The return value is the same as dest. */
udf_timestamp_t *
udf_timespec_to_stamp(const struct timespec ts, udf_timestamp_t *dest)
{
  long int days, rem, y;
  const unsigned short int *ip;
  int16_t offset = 0;
  int16_t tv_sec;

#ifdef HAVE_TIMEZONE_VAR  
  offset = -timezone;
#endif
  
  if (!dest)
    return dest;
  
  dest->type_tz = 0x1000 | (offset & 0x0FFF);
  
  tv_sec       = ts.tv_sec + (offset * SECS_PER_MINUTE);
  days         = tv_sec / SECS_PER_DAY;
  rem          = tv_sec % SECS_PER_DAY;
  dest->hour   = rem / SECS_PER_HOUR;
  rem         %= SECS_PER_HOUR;
  dest->minute = rem / SECS_PER_MINUTE;
  dest->second = rem % SECS_PER_MINUTE;
  y            = EPOCH_YEAR;
  
#define DIV(a,b) ((a) / (b) - ((a) % (b) < 0))
#define LEAPS_THRU_END_OF(y) (DIV (y, 4) - DIV (y, 100) + DIV (y, 400))
  
  while (days < 0 || days >= (__isleap(y) ? DAYS_PER_YEAR+1 : DAYS_PER_YEAR)) {
    long int yg = y + days / DAYS_PER_YEAR - (days % DAYS_PER_YEAR < 0);
    
    /* Adjust DAYS and Y to match the guessed year.  */
    days -= ((yg - y) * DAYS_PER_YEAR
	     + LEAPS_THRU_END_OF (yg - 1)
	     - LEAPS_THRU_END_OF (y - 1));
    y = yg;
  }
  dest->year = y;
  ip = __mon_yday[__isleap(y)];
  for (y = 11; days < (long int) ip[y]; --y)
    continue;
  days -= ip[y];
  dest->month = y + 1;
  dest->day   = days + 1;
  
  dest->centiseconds = ts.tv_nsec / 10000000;
  dest->hundreds_of_microseconds = ( (ts.tv_nsec / 1000)
				     - (dest->centiseconds * 10000) ) / 100;
  dest->microseconds = ( (ts.tv_nsec / 1000) 
			 - (dest->centiseconds * 10000)
			 - (dest->hundreds_of_microseconds * 100) );
  return dest;
}
#endif

/*!
  Return the modification time of the file.
 */
time_t
udf_get_modification_time(const udf_dirent_t *p_udf_dirent)
{
  if (p_udf_dirent) {
    time_t ret_time;
    long int usec;
    udf_stamp_to_time(&ret_time, &usec, p_udf_dirent->fe.modification_time);
    return ret_time;
  }
  return 0;
}

/*!
  Return the access time of the file.
 */
time_t
udf_get_access_time(const udf_dirent_t *p_udf_dirent)
{
  if (p_udf_dirent) {
    time_t ret_time;
    long int usec;
    udf_stamp_to_time(&ret_time, &usec, p_udf_dirent->fe.access_time);
    return ret_time;
  }
  return 0;
}

/*!
  Return the attribute (most recent create or access) time of the file
 */
time_t
udf_get_attribute_time(const udf_dirent_t *p_udf_dirent)
{
  if (p_udf_dirent) {
    time_t ret_time;
    long int usec;
    udf_stamp_to_time(&ret_time, &usec, p_udf_dirent->fe.attribute_time);
    return ret_time;
  }
  return 0;
}

