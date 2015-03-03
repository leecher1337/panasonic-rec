//vim:fileencoding=utf8
/*
 dvd-vr.c     Identify and optionally copy the individual programs
              from a DVD-VR format disc

 Copyright © 2007-2010 Pádraig Brady <P@draigBrady.com>

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/*

Notes:

    Individual recordings (programs) are extracted,
    honouring any splits and/or deletes.
    Merged programs are not handled yet though as
    I would need to fully parse the higher level program set info.
    Note the VOBs output from this program can be trivially
    concatenated with the unix cat command for example
    (note there will be timestamp jumps which may be problematic).

    While extracting the DVD data, this program instructs the system
    to not cache the data so that existing cached data is not affected.

    We output the data from this program rather than just outputting offsets
    for use with dd for example, because we may support disjoint VOBUs
    (merged programs) in future. Also in future we may transform the NAV info
    slightly in the VOBs? Anyway it gives us greater control over the system cache
    as described above.

    It might be useful to provide a FUSE module using this logic,
    to present the logical structure of a DVD-VR, maybe even present as DVD-Video?

    Doesn't parse play list index
    Doesn't parse still image info
    Doesn't parse chapters
    Doesn't fixup MPEG time data


Requirements:

    gcc >= 2.95
    glibc >= 2.3.3 on linux
    Tested on linux, CYGWIN and Mac OS X
*/

#define _GNU_SOURCE           /* for posix_fadvise(), futimes() */
#define _FILE_OFFSET_BITS 64  /* for implicit large file support */

#include <inttypes.h>
#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <locale.h>
#include <getopt.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <errno.h>
#include <limits.h>

#if !defined(MB_LEN_MAX) || MB_LEN_MAX<16
/* 1 char could be converted to 2 multibyte chars
 * (for example combining accents), with each taking up to
 * 6 bytes in UTF-8 for example */
# undef MB_LEN_MAX
# define MB_LEN_MAX 16
#endif

#define TYPE_SIGNED(t) (! ((t) 0 < (t) -1))
#define TYPE_MAX(t) \
  ((t) (! TYPE_SIGNED (t) \
        ? (t) -1 \
        : ~ (~ (t) 0 << (sizeof (t) * CHAR_BIT - 1))))
#define OFF_T_MAX TYPE_MAX(off_t)

#define STREQ(x,y) (strcmp(x,y)==0)
#ifndef MAX
# define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
# define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

/* For a discussion of this macro see:
 * http://www.pixelbeat.org/programming/gcc/static_assert.html */
#define ASSERT_CONCAT_(a, b) a##b
#define ASSERT_CONCAT(a, b) ASSERT_CONCAT_(a, b)
#define STATIC_ASSERT(e,m) enum { ASSERT_CONCAT(assert_line_, __LINE__) = 1/(!!(e)) }

#if defined(__CYGWIN__) || defined(_WIN32) /* windos doesn't like : in filenames */
#define TIMESTAMP_FMT "%F_%H-%M-%S"
#else
#define TIMESTAMP_FMT "%F_%T" /* keep : in filenames for backward compat */
#endif
const char* base_name = TIMESTAMP_FMT;

FILE* stdinfo; /* Where we write disc info */

#include <langinfo.h>
#ifdef HAVE_ICONV
#include <iconv.h>
#endif
const char* disc_charset;
const char* sys_charset;

/*********************************************************************************
 *                          support routines
 *********************************************************************************/
static size_t my_strnlen(const char* s, size_t n)
{
    size_t len = 0;
    while (n-- && *s++) len++;
    return len;
}

/* Mac OS X doesn't provide strndup :( */
static char* my_strndup(const char *s, size_t n)
{
    size_t len = my_strnlen(s, n);
    char* ret = malloc(len+1);
    if (ret) {
        memcpy(ret, s, len);
        ret[len] = '\0';
    }
    return ret;
}

#ifndef NDEBUG
void hexdump(const void* data, int len)
{
    int i;
    const unsigned char* bytes=data;
    for (i=0; i<len; i++) {
        printf("%02X ",bytes[i]);
        if ((i+1)%16 == 0) printf("\n");
    }
    if (len%16) putchar('\n');
}

#endif//NDEBUG

typedef enum {
    PERCENT_START,
    PERCENT_UPDATE,
    PERCENT_END
} percent_control_t;

/* Only use display_char!=0 to set non default progress chars like errors etc. */
static
void percent_display(percent_control_t percent_control, unsigned int percent, int display_char)
{
    static int point;
    #define POINTS 20
    #define DEFAULT_PROGRESS_CHAR '.'
    static char chars[POINTS+1];

    switch (percent_control) {
    case PERCENT_START: {
        point=0;
        fprintf(stderr, "[%*s]\r",POINTS,"");
        memset(chars, ' ', POINTS);
        *(chars+POINTS)='\0';
        break;
    }
    case PERCENT_UPDATE: {
        int newpoint=percent/(100/POINTS);
        int i;
        if (display_char && (display_char != DEFAULT_PROGRESS_CHAR))
            for (i=point; i<=newpoint && i<POINTS; i++)
                chars[i]=display_char;
        for (i=0; i<newpoint; i++)
            if (chars[i] == ' ')
                chars[i] = DEFAULT_PROGRESS_CHAR;
        fprintf(stderr, "\r[%s]",chars);
        point=newpoint;
        break;
    }
    case PERCENT_END: {
        fprintf(stderr, "\r %*s \r",POINTS,"");
        break;
    }
    }
    fflush(stderr);
}

/* Set access and modfied times of filename
   to the specified broken down time */
static int touch(const char* filename, struct tm* tm)
{
    time_t ut = mktime(tm);
    struct timeval tv[2]={ {.tv_sec=ut, .tv_usec=0}, {.tv_sec=ut, .tv_usec=0} };
    return utimes(filename, tv);
}

typedef void (*process_func_t)(uint8_t* buf, unsigned int bs, void* context);

/*
  Copy data between file descriptors while not
  putting more than blocks*block_size in the system cache.
  Therefore you will probably want to call this function repeatedly.

  I tested 3 methods for streaming large amounts of data to/from disk.
  All 3 took the same time as the bottleneck is the reading and writing to disk.
  On x86 at least there is no significant difference between the AUTO and ALLOC_ALIGN
  methods, the latter of which allocates the userspace buffer aligned on a page.
  There was a noticeable reduction in CPU usage when MMAP_WRITE was used,
  but the CPU usage is insignificant anyway due to the disc speeds we will
  generally be dealing with. I also noticed that the MMAP method was more stable
  giving consistent timings in all benchmark runs. However to ease portability worries
  I use the AUTO method below, which will also allow us to modify the MPEG frames if
  required. For reference the timings for extracting a 338 MiB VOB
  from a VRO on the same hard disk were:

      MMAP_WRITE
        real    0m30.650s
        user    0m0.007s
        sys     0m1.130s
      AUTO/ALLOC_ALIGN
        real    0m31.776s
        user    0m0.075s
        sys     0m1.803s
 */

static int stream_data(int src_fd, int dst_fd, uint32_t blocks, uint16_t block_size,
                       process_func_t process_func, void* process_context)
{
#define AUTO
#define BLOCKS_PER_OP 1

#if defined AUTO
    uint8_t buf[block_size*BLOCKS_PER_OP];  /* Not page aligned by default */
#elif defined ALLOC_ALIGN
    /* There are portability issue with this.
     * One may need to use MAP_ANONYMOUS rather than MAP_ANON.
     * Also one may need to use MAP_FILE and operate on /dev/zero instead.
     *
     * Also see posix_memalign().
     * Also see pagealign_alloc in gnulib.
     */
    static int8_t* buf;
    if (!buf) {
        buf = mmap(NULL,block_size*BLOCKS_PER_OP,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    }
    if (buf == MAP_FAILED) {
        fprintf(stderr, "Error: Failed allocating mmap aligned buf [%s]\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if ((size_t)buf & (sysconf(_SC_PAGE_SIZE)-1)) {
        fprintf(stderr, "Warning: mmap buffer not aligned\n");
    }
#endif


    unsigned int block;
    for (block=0; block<blocks; block+=BLOCKS_PER_OP) {
        int trans_blocks = MIN(blocks-block, BLOCKS_PER_OP);
        int trans_size = trans_blocks * block_size;

        int bytes_read = read(src_fd, buf, trans_size);
        if (bytes_read != trans_size) {
#ifndef NDEBUG
            if (bytes_read<0) /* otherwise file truncated */
                fprintf(stderr, "Error reading from SRC [%s]\n", strerror(errno));
#endif //NDEBUG
            return -1;
        }
        if (process_func) {
            int pblock;
            for (pblock=0; pblock<trans_blocks; pblock++) {
                process_func(buf+(pblock*block_size), block_size, process_context);
            }
        }
        if (write(dst_fd, buf, trans_size) != trans_size) {
            fprintf(stderr, "Error writing to DST [%s]\n", strerror(errno));
            return -2;
        }
    }

#ifdef POSIX_FADV_DONTNEED
    /* Don't fill cache with SRC.
    Note be careful to invalidate only what we've written
    so that we don't dump any readahead cache. */
    uint32_t bytes = blocks * block_size;
    off_t offset = lseek(src_fd, 0, SEEK_CUR);
    /* Note src is already guaranteed seekable, but offset may
     * be 0 for example if /dev/zero is specified for testing. */
    if (offset >= bytes) {
        int ret = posix_fadvise(src_fd, offset-bytes, bytes, POSIX_FADV_DONTNEED);
        if (ret) {
            fprintf(stderr, "Warning: posix_fadvise failed [%s]\n", strerror(ret));
        }
    }

    /* Don't fill cache with DST.
    Note this slows the operation down by 20% when both source
    and dest are on the same hard disk at least. I guess
    this is due to implicit syncing in posix_fadvise()? */
    offset = lseek(dst_fd, 0, SEEK_CUR);
    if (offset != (off_t)-1) { /* seekable */
        int ret = posix_fadvise(dst_fd, 0, 0, POSIX_FADV_DONTNEED);
        if (ret) {
            fprintf(stderr, "Warning: posix_fadvise failed [%s]\n", strerror(ret));
        }
    }
#endif //POSIX_FADV_DONTNEED

   return 0;
}

#ifdef MMAP_WRITE
static int stream_data(int src_fd, int dst_fd, uint32_t blocks, uint16_t block_size)
{
    int8_t* buf;
    off_t offset = lseek(src_fd, 0, SEEK_CUR);
    off_t pa_offset = offset & ~(sysconf(_SC_PAGE_SIZE) - 1); /* 4097 -> 4096 */
    off_t offset_align = offset - pa_offset;;
    buf = mmap(NULL, block_size*blocks+offset_align,
               PROT_READ, MAP_PRIVATE, src_fd, pa_offset);
    if (buf == MAP_FAILED) {
        fprintf(stderr, "Error mmaping file [%s]\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
#ifdef MADV_SEQUENTIAL
    if (madvise(buf, block_size*blocks+offset_align, MADV_SEQUENTIAL)) {
        fprintf(stderr, "Warning: madvise failed [%s]\n", strerror(errno));
    }
#endif

    if (write(dst_fd,buf+offset_align,blocks*block_size) != blocks*block_size) {
        fprintf(stderr, "Error writing to DST [%s]\n", strerror(errno));
        return -2;
    }
    offset = lseek(src_fd, blocks*block_size, SEEK_CUR); /* This won't seek head I presume */
    if (offset == (off_t)-1) {
        fprintf(stderr, "Error seeking in src [%s]\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

#ifdef MADV_DONTNEED
    if (madvise(buf, blocks*block_size, MADV_DONTNEED)) {
        fprintf(stderr, "Warning: madvise failed [%s]\n", strerror(errno));
    }
#endif

    return 0;
}
#endif //MMAP_WRITE

static const char* get_charset(void)
{
    const char* codeset = nl_langinfo(CODESET);
#ifdef __CYGWIN__
    /* Cygwin 1.5 does not support locales and nl_langinfo (CODESET)
       always returns "US-ASCII". This is fixed in v1.7 I think?  */
    if (codeset && STREQ(codeset, "US-ASCII")) {

        /* parse LANG=ja_JP.SJIS -> SJIS */
        const char* locale = getenv ("LANG");
        if (locale && *locale) {
            const char *dot = strchr (locale, '.');

            if (dot) {
                const char *modifier;

                dot++;
                if (!(modifier = strchr (dot, '@'))) {
                    return dot;
                } else {
                    static char buf[32];
                    size_t len = modifier - dot;
                    if (len < sizeof (buf)) {
                        memcpy (buf, dot, len);
                        *(buf+len) = '\0';
                        return buf;
                    }
                }
            }
        }
        return "UTF-8";
    }
#endif
    return codeset;
}

static bool text_convert(const char *src, size_t srclen, char *dst, size_t dstlen)
{
    bool ret=false;
#ifdef HAVE_ICONV
    iconv_t cd = iconv_open (sys_charset, disc_charset);
    if (cd != (iconv_t)-1) {
        if (iconv (cd, (ICONV_CONST char**)&src, &srclen, &dst, &dstlen) != (size_t)-1) {
            if (iconv (cd, NULL, NULL, &dst, &dstlen) != (size_t)-1) { /* terminate string */
                ret=true;
            }
        } else {
            fprintf(stderr, "Error converting text from %s to %s\n",
                    disc_charset, sys_charset);
        }
        iconv_close (cd);
    } else {
        fprintf(stderr, "Error converting text from %s to %s. Not supported\n",
                disc_charset, sys_charset);
    }
#else
    /* avoid warnings (__attribute__ ((unused)) is too verbose/non standard) */
    (void)src; (void)dst; (void)srclen; (void)dstlen;
    fprintf(stderr, "Error converting text. libiconv missing\n");
#endif
    return ret;
}

/*********************************************************************************
 *                          Internal structures
 *********************************************************************************/

typedef struct {
    int aspect;
    int width;
    int height;
} p_video_attr_t;
p_video_attr_t* ifo_video_attrs;

typedef enum {
    SCRAMBLED_UNSET=-1,
    UNSCRAMBLED=0,
    SCRAMBLED=1,
    PARTIALLY_SCRAMBLED=2
} scrambled_t;

typedef struct {
    int video_attr;
    scrambled_t scrambled;
} p_program_attr_t;
p_program_attr_t* ifo_program_attrs;

/*********************************************************************************
 *                          The DVD-VR structures
 *********************************************************************************/

#undef PACKED
#if defined(__GNUC__)
# if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 95)
#  define PACKED __attribute__ ((packed))
# endif
#endif
#if !defined(PACKED)
# error "Your compiler doesn't support __attribute__ ((packed))"
#endif

/* DVD structures are in network byte order (big endian) */
#include <netinet/in.h>
#undef NTOHS
#undef NTOHL
#define NTOHS(x) x=ntohs(x) /* 16 bit */
#define NTOHL(x) x=ntohl(x) /* 32 bit */

#define DVD_SECTOR_SIZE 2048

typedef struct {
    struct {
        /* Suffix numbers are decimal offsets */
        /* 0 */
        char     id[12];
        uint32_t vmg_ea;         /* end address */
        uint8_t  zero_16[12];
        uint32_t vmgi_ea;        /* includes playlist info after this structure */
        uint16_t version;        /* specification version */
        /* 34 */                 /* Different from DVD-Video from here */
        uint8_t  zero_34[30];
        uint8_t  data_64[3];
        uint8_t  txt_encoding;   /* as per VideoTextDataUsage.pdf */
        uint8_t  data_68[30];
        /* 98 */
        char     disc_info1[64]; /* format name, or copy of disc_info2. */
        char     disc_info2[64]; /* format name, time or user label.. */
        uint8_t  zero_226[30];
        /* 256 */
        uint32_t pgit_sa;        /* program info table start address */
        uint32_t info_260_sa;    /* ? start address */
        uint8_t  zero_264[3];
        struct {
            uint8_t supported;   /* Encrypted Title Key Status */
            uint8_t title_key[8];/* This needs to be decrypted using media key */
        } cprm;
        uint8_t  zero_276[28];
        /* 304 */
        uint32_t def_psi_sa;     /* default program set info start address */
        uint32_t info_308_sa;    /* ? start address */
        uint32_t info_312_sa;    /* user defined program set info start address? */
        uint32_t info_316_sa;    /* ? start address */
        uint8_t  zero_320[32];
        uint32_t txt_attr_sa;    /* extra attributes for programs (chan id etc.) */
        uint32_t info_356_sa;    /* ? start address */
        uint8_t  zero_360[152];
    } PACKED mat;
} PACKED rtav_vmgi_t; /*Real Time AV (from DVD_RTAV dir)*/
STATIC_ASSERT(sizeof(rtav_vmgi_t) == 512,""); /* catch any miscounting above */

typedef struct {
    uint8_t audio_attr[3];
} PACKED audio_attr_t;

typedef struct {
    uint8_t pgtm[5];
} PACKED pgtm_t;

typedef struct {
    uint32_t ptm;
    uint16_t ptm_extra; /* extra to DSI pkts */
} PACKED ptm_t;

typedef struct {
    uint16_t vob_attr;
    pgtm_t   vob_timestamp;
    uint8_t  data1;
    uint8_t  vob_format_id;
    ptm_t    vob_v_s_ptm;
    ptm_t    vob_v_e_ptm;
} PACKED vvob_t; /* Virtual VOB */
typedef struct {
    uint8_t  data[12];
} PACKED adj_vob_t;
typedef struct {
    uint16_t nr_of_time_info;
    uint16_t nr_of_vobu_info;
    uint16_t time_offset;
    uint32_t vob_offset;
} PACKED vobu_map_t;
typedef struct {
    uint8_t  data[7];
} PACKED time_info_t;
typedef struct {
    uint8_t vobu_info[3];
} PACKED vobu_info_t;

typedef struct {
    uint16_t zero1;
    uint8_t  nr_of_pgi;
    uint8_t  nr_of_vob_formats;
    uint32_t pgit_ea;
} PACKED pgiti_t; /* info for ProGram Info Table */

typedef struct {
    uint16_t video_attr;
    uint8_t  nr_of_audio_streams;
    uint8_t  data1;
    audio_attr_t  audio_attr0;
    audio_attr_t  audio_attr1;
    uint8_t  data2[50];
} PACKED vob_format_t;

typedef struct {
    uint16_t nr_of_programs;
} PACKED pgi_gi_t; /* global info for ProGram Info */

typedef struct  {
    uint8_t  data1;
    uint8_t  nr_of_psi;
    uint16_t nr_of_programs;  /* Num programs on disc */
} PACKED psi_gi_t; /* global info for Program Set Info */

typedef struct  {
    uint8_t  data1;
    uint8_t  data2;
    uint16_t nr_of_programs;  /* Num programs in program set */
    char     label[64];       /* ASCII. Might not be NUL terminated */
    char     title[64];       /* Could be same as label, NUL, or another charset */
    uint16_t prog_set_id;     /* On LG V1.1 discs this is program set ID */
    uint16_t first_prog_id;   /* ID of first program in this program set */
    char     data3[6];
} PACKED psi_t;

static const char* parse_txt_encoding(uint8_t txt_encoding)
{
/* from the VideoTextDataUsage.pdf available at dvdforum.org we have:
     01h : ISO 646
     10h : JIS Roman[14]*and JIS Kanji1990[168]*
     11h : ISO 8859-1
     12h : JIS Roman[14]*and JIS Katakana[13]*including Shift JIS Kanji
   Also Nero generates discs with 00h, so I'll assume this is ASCII.
*/

    const char* charset="Unknown";

    switch (txt_encoding) {
    case 0x00: charset="ASCII"; break;
    case 0x01: charset="ISO646-JP"; break; /* ?? */
    case 0x10: charset="JIS_C6220-1969-RO"; break; /* ?? */
    case 0x11: charset="ISO_8859-1"; break;
    case 0x12: charset="SHIFT_JIS"; break;
    }

    if (STREQ("Unknown", charset)) {
        fprintf(stdinfo, "text encoding: %s", charset);
        fprintf(stdinfo, ". (%02X). Please report this number and actual text encoding.\n", txt_encoding );
        charset="ISO_8859-15"; /* Shouldn't give an error at least */
    }

    return charset;
}


static bool parse_audio_attr(audio_attr_t audio_attr0)
{
    int coding   = (audio_attr0.audio_attr[0] & 0xE0)>>5;
    int channels = (audio_attr0.audio_attr[1] & 0x0F);
    /* audio_attr0.audio_attr[2] = 7 for my camcorder. Is this 192Kbit? */
    /* audio_attr0.audio_attr[2] = 9 for Masato Nunokawa's disc? */

    if (channels < 8) {
        fprintf(stdinfo, "audio_channs: %d\n",channels+1);
    } else if (channels == 9) {
        /* According to Masato Nunokawa's disc */
        fprintf(stdinfo, "audio_channs: 2 (mono)\n");
    } else {
        return false;
    }

    const char* coding_name="Unknown";
    switch (coding) {
    case 0: coding_name="Dolby AC-3"; break;
    case 2: coding_name="MPEG-1"; break;
    case 3: coding_name="MPEG-2ext"; break;
    case 4: coding_name="Linear PCM"; break;
    }
    fprintf(stdinfo, "audio_coding: %s",coding_name);
    if (STREQ("Unknown", coding_name)) {
        fprintf(stdinfo, ". (%d). Please report this number and actual audio encoding.\n", coding );
    } else {
        putc('\n', stdinfo);
    }

    return true;
}

static bool parse_video_attr(uint16_t video_attr, p_video_attr_t* p_video_attr)
{
    int resolution  = (video_attr & 0x0038) >>  3;
    int aspect      = (video_attr & 0x0C00) >> 10;
    int tv_sys      = (video_attr & 0x3000) >> 12;
    int compression = (video_attr & 0xC000) >> 14;

    p_video_attr->aspect = p_video_attr->width = p_video_attr->height = -1;

    int vert_resolution  = 0;
    int horiz_resolution = 0;
    const char* tv_system = "Unknown";
    switch (tv_sys) {
    case 0:
        tv_system = "NTSC";
        vert_resolution=480;
        break;
    case 1:
        tv_system = "PAL";
        vert_resolution=576;
        break;
    }
    fprintf(stdinfo, "tv_system   : %s", tv_system);
    if (STREQ("Unknown", tv_system)) {
        fprintf(stdinfo, ". (%d). Please report this number and actual TV system.\n", tv_sys );
    } else {
        putc('\n', stdinfo);
    }

    switch (resolution) {
    case 0: horiz_resolution=720; break;
    case 1: horiz_resolution=704; break;
    case 2: horiz_resolution=352; break;
    case 3: horiz_resolution=352; vert_resolution/=2; break;
    case 4: horiz_resolution=544; break; /* this is a google inspired guess. */
    case 5: horiz_resolution=480; break; /* from Aaron Binns' disc */
    }
    if (horiz_resolution && vert_resolution) {
        fprintf(stdinfo, "resolution  : %dx%d\n", horiz_resolution, vert_resolution);
        p_video_attr->width = horiz_resolution;
        p_video_attr->height = vert_resolution;
    } else if (!horiz_resolution) {
        fprintf(stdinfo, "resolution  : Unknown (%d). Please report this number and actual resolution.\n", resolution );
    }

    const char* aspect_ratio = "Unknown";
    switch (aspect) {
    case 0: aspect_ratio="4:3"; break;
    case 1: aspect_ratio="16:9"; break;
    }
    fprintf(stdinfo, "aspect_ratio: %s", aspect_ratio );
    if (STREQ("Unknown", aspect_ratio)) {
        fprintf(stdinfo, ". (%d). Please report this number and actual aspect ratio.\n", aspect );
    } else {
        putc('\n', stdinfo);
        p_video_attr->aspect = aspect + 2; /* DVD-Video aspect encoding */
    }

    const char* mode = "Unknown";
    switch (compression) {
    case 0: mode="MPEG1"; break;
    case 1: mode="MPEG2"; break;
    }
    fprintf(stdinfo, "video_format: %s", mode );
    if (STREQ("Unknown", mode)) {
        p_video_attr->aspect = -1; /* Don't adjust aspect later for unknown formats */
        fprintf(stdinfo, ". (%d). Please report this number and actual compression format.\n", compression );
    } else {
        putc('\n', stdinfo);
    }

    return true;
}

static bool parse_pgtm(pgtm_t pgtm, struct tm* tm)
{
    bool ret=false;

    uint16_t year  = ((pgtm.pgtm[0]       ) <<8 | (pgtm.pgtm[1]     )) >> 2;
    uint8_t  month =  (pgtm.pgtm[1] & 0x03) <<2 | (pgtm.pgtm[2] >> 6);
    uint8_t  day   =  (pgtm.pgtm[2] & 0x3E) >>1;
    uint8_t  hour  =  (pgtm.pgtm[2] & 0x01) <<4 | (pgtm.pgtm[3] >> 4);
    uint8_t  min   =  (pgtm.pgtm[3] & 0x0F) <<2 | (pgtm.pgtm[4] >> 6);
    uint8_t  sec   =  (pgtm.pgtm[4] & 0x3F);
    if (year) {
        tm->tm_year=year-1900;
        tm->tm_mon=month-1;
        tm->tm_mday=day;
        tm->tm_hour=hour;
        tm->tm_min=min;
        tm->tm_sec=sec;
        tm->tm_isdst=-1; /*Auto calc DST offset.*/

        char date_str[32];
        strftime(date_str,sizeof(date_str),"%F %T",tm); //locale = %x %X
        fprintf(stdinfo, "date : %s\n",date_str);
        ret=true;
    } else {
        fprintf(stdinfo, "date : not set\n");
    }
    return ret;
}

#ifndef NDEBUG
/* This is basically a simplification of find_program_text_info() */
static void print_psi(psi_gi_t* psi_gi)
{
    putc('\n', stdinfo);
    int ps;
    uint16_t program_count = 0;
    for (ps=0; ps<psi_gi->nr_of_psi; ps++) {
        psi_t *psi = (psi_t*)(((char*)(psi_gi+1)) + (ps * sizeof(psi_t)));

        uint16_t first_prog_num = ntohs(psi->first_prog_id); /* assuming this is first to play? */
        uint16_t start_prog_num = program_count+1;
        uint16_t num_progs_in_set = ntohs(psi->nr_of_programs);
        program_count += num_progs_in_set;
        fprintf(stdinfo, "Programs in Program set %d:", ps+1);
        int program_id;
        for (program_id = start_prog_num;
             program_id < start_prog_num+num_progs_in_set;
             program_id++) {
            const char* fmt = (program_id==first_prog_num ? " (%d)" : " %d");
            fprintf(stdinfo, fmt, program_id);
        }
        putc('\n', stdinfo);
    }
    putc('\n', stdinfo);
}
#endif//NDEBUG

/*
 * FIXME: This assumes the programs occur linearly within
 * the default program sets. This has been accurate for all
 * discs I've seen so far at least. Note I've noticed a
 * couple of "SONY_MOBILE" discs with no labels at all.
 */
static psi_t* find_program_text_info(psi_gi_t* psi_gi, int program)
{
    int ps;
    uint16_t program_count = 0;
    for (ps=0; ps<psi_gi->nr_of_psi; ps++) {
        psi_t *psi = (psi_t*)(((char*)(psi_gi+1)) + (ps * sizeof(psi_t)));
        uint16_t start_prog_num;
        /*
        start_prog_num = ntohs(psi->first_prog_id);

        We need to maintain program count as first_prog_id is often not stored,
        as is the case for LG and "CIRRUS LOGIC" V1.1 discs for example (it's 0 or 0xFFFF).
        Also I noticed a Sony disc that had programs sets with 2 programs in them, which
        sometimes set the first_prog_id to the second program in the set.  Perhaps
        this field identifies the prog to start playing, as the first program in those
        sets was a single VOBU that was generated due to a split.
        Perhaps I should name the programs label.ps_id(001) when > 1 ps. */
        start_prog_num = program_count+1;
        uint16_t num_progs_in_set = ntohs(psi->nr_of_programs);
        /* TODO: Perhaps have an option to merge all programs
           in a program set to a vob using this info. That would assume
           though that the programs were adjacent. */
        program_count += num_progs_in_set;
        uint16_t end_prog_num = start_prog_num + num_progs_in_set - 1;
        if ((program >= start_prog_num) && (program <= end_prog_num)) {
            return psi;
        }
    }
    return (psi_t*)NULL;
}

/*
 * This function controls the storage used by the actual
 * encoding conversion routines. Note a len must be passed
 * since the text fields are sometimes not NUL terminated.
 *
 * A string in the local encoding is returned which must be free()
 */
static char* text_field_convert(const char* field, unsigned int len)
{
    unsigned int conv_max_len=len*MB_LEN_MAX+1/*NUL*/;
    char* field_local=malloc(conv_max_len);
    if (!field_local) {
        fprintf(stderr, "Error allocating space for text conversion\n");
        return NULL;
    }
    if (*field) {
        char field_copy[len+1]; /* Copy as may not be NUL terminated */
        field_copy[len] = '\0';
        (void) strncpy(field_copy, field, len);
        size_t srclen = strlen(field_copy) + 1; /* convert NUL also */
        if (!text_convert(field_copy, srclen, field_local, conv_max_len)) {
            free(field_local);
            field_local=NULL;
        }
    } else {
        *field_local='\0';
    }

    return field_local;
}

/* Filter redundant info */
static bool disc_info_redundant(const char* info)
{
    const char* info_exclude_list[] = {
        "DVD VR",
        "DVD-VR",
        " ",
        "" /* must be last */
    };
    const char** info_to_exclude = info_exclude_list;
    while (**info_to_exclude) {
        if (STREQ(info, *info_to_exclude)) {
            return true;
        }
        info_to_exclude++;
    }
    return false;
}

static void print_disc_info(rtav_vmgi_t* rtav_vmgi_ptr)
{
    char* txt_local;

    txt_local = text_field_convert(rtav_vmgi_ptr->mat.disc_info2,
                                   sizeof(rtav_vmgi_ptr->mat.disc_info2));
    if (txt_local && *txt_local && !disc_info_redundant(txt_local)) {
        fprintf(stdinfo, "info  : %s\n", txt_local);
    }
    free(txt_local);

    if (strncmp(rtav_vmgi_ptr->mat.disc_info1,
                rtav_vmgi_ptr->mat.disc_info2,
                sizeof(rtav_vmgi_ptr->mat.disc_info1))) {
        /* If there is a unique disc_info1 here, then there is
         * no disc_info2 above on the discs I've seen so far */
        txt_local = text_field_convert(rtav_vmgi_ptr->mat.disc_info1,
                                    sizeof(rtav_vmgi_ptr->mat.disc_info1));
        if (txt_local && *txt_local && !disc_info_redundant(txt_local)) {
            fprintf(stdinfo, "info  : %s\n", txt_local);
        }
        free(txt_local);
    }
}

static char* mb_clean_name(const char* src)
{
    size_t src_size = strlen (src) + 1;
    wchar_t *str_wc = NULL;
    size_t src_chars = mbstowcs (NULL, src, 0);
    if (src_chars == (size_t) -1)
        return NULL;
    src_chars += 1; /* make space for NUL */
    str_wc = malloc (src_chars * sizeof (wchar_t));
    if (str_wc == NULL)
        return NULL;
    if (mbstowcs (str_wc, src, src_chars) <= 0) {
        free(str_wc);
        return NULL;
    }
    str_wc[src_chars - 1] = L'\0';

    wchar_t* wc = str_wc;
    while (*wc) {
        size_t good = wcscspn(wc, L" /:?\\");
        if (good)
            wc+=good;
        else
            *wc++=L'-';
    }

    char* newstr = malloc (src_size);
    if (newstr == NULL) {
        free(str_wc);
        return NULL;
    }
    (void) wcstombs(newstr, str_wc, src_size);
    free(str_wc);

    return newstr;
}

/* Must pass a string on the heap which may be modified inplace,
 * or may be reallocated. */
static char* clean_name(char* src, bool mb_src)
{
    if (mb_src && (MB_CUR_MAX > 1)) {
        char* cleaned = mb_clean_name(src);
        free(src);
        return cleaned;
    }

    char* c = src;
    while (*c) {
        size_t good = strcspn(c, " /:?\\");
        if (good)
            c+=good;
        else
            *c++='-';
    }
    return src;
}

static char* get_label_base(const psi_t* psi)
{
    char* title_local = text_field_convert(psi->title, sizeof(psi->title));
    if (title_local && *title_local &&
        strncmp(title_local, psi->label, sizeof(psi->label))) { /* if title != label */
        title_local = clean_name(title_local, true);
        if (!title_local) {
          fprintf(stderr, "Error generating file name from title\n");
          return NULL;
        } else {
          return title_local;
        }
    }
    free(title_local);

    const char* label=psi->label; /* ASCII */
    if (*label && !STREQ(label, " ")) {
        char* label_local = my_strndup(label, sizeof(psi->label));
        label_local = clean_name(label_local, false);
        return label_local;
    }

    return NULL;
}

static void print_label(const psi_t* psi)
{
    const char* label=psi->label; /* ASCII */

    char* title_local = text_field_convert(psi->title, sizeof(psi->title));
    if (title_local && *title_local &&
        strncmp(title_local, label, sizeof(psi->label))) { /* if title != label */
        fprintf(stdinfo, "title: %s\n", title_local);
    }
    free(title_local);

    if (*label && !STREQ(label, " ")) {
        fprintf(stdinfo, "label: %.*s\n", (int)sizeof(psi->label), label);
    }
}

/*********************************************************************************
 * MPEG2 processing routines
 *********************************************************************************/

/*
This is to both apply the aspect ratio from the IFO to the sequence header (0xB3)
and to reset the size of any sequence display extension packets (0xB5) to
the size of the video, as processing of this is not handled well by players.

For e.g., for 16:9 recordings my camcorder leaves the aspect in the sequence
header at 4:3 but sets the pan scan width and height appropriately in the
sequence display extension. ffmpeg for a short time used this to compute the
aspect, but because many MPEG streams incorrectly set the pan scan widths and
heights, it was changed back to ignoring these when determining the aspect ratio.
See: http://svn.ffmpeg.org/ffmpeg?view=rev&revision=15183
Specifically for widescreen PAL movies my DVD-VR camcorder set:
  aspect = 2 (4:3)
  width x height = 720 x 576
  pan scan width x height = 540 x 576
*/

#define MPEG_HEADER_LEN 4
#define SEQUENCE_ID 0xB3
#define SEQUENCE_EXTENSION_ID 0xB5
#define VIDEO_STREAM_0 0xE0 /* I've only seen E0 on dvd-vr discs (E0-F possible) */
#define SEQUENCE_LEN 4 /* length of data we need to parse from sequence packet */
#define SEQUENCE_EXTENSION_LEN 5 /* length of data we need to parse from sequence extension packet */
#define VIDEO_STREAM_LEN 3 /* length of data we need to parse from video stream packet */

/* Return offset to header or -1 if not found */
static int find_mpeg_header(const uint8_t* buf, const unsigned int bs, const uint8_t type)
{
    unsigned int offset=0;
    uint32_t header = 0x00000100 + type;
    NTOHL(header);
    while (offset <= bs - sizeof (header)) {
        if (*(uint32_t*)(buf+offset) == header)
            return offset;
        offset++;
    }
    return -1;
}

static int sequence_offset;
static uint8_t sequence_aspect;

/* reset cached values for each program */
static void init_mpeg2_cache(void)
{
    sequence_offset = -1;
    sequence_aspect = -1;
}

static p_video_attr_t get_sequence_aspect(const uint8_t* buf)
{
    p_video_attr_t s_video_attr;
    s_video_attr.width = s_video_attr.height = -1;
    uint8_t aspect_byte = *(buf + sequence_offset + MPEG_HEADER_LEN + 3);
    s_video_attr.aspect = aspect_byte >> 4;
    return s_video_attr;
}

static void set_sequence_aspect(uint8_t* buf, const unsigned int offset, p_video_attr_t s_video_attr)
{
    uint8_t aspect_byte = *(buf + offset + MPEG_HEADER_LEN + 3);
    aspect_byte = (aspect_byte & 0x0F) | ((uint8_t) s_video_attr.aspect) << 4;
    *(buf + sequence_offset + MPEG_HEADER_LEN + 3) = aspect_byte;
}

static p_video_attr_t get_sequence_display_extension_sizes(const uint8_t* buf, const unsigned int offset)
{
    p_video_attr_t e_video_attr;
    e_video_attr.aspect=-1;

    uint8_t type = *(buf + offset + MPEG_HEADER_LEN);
    int skip_colour=(type&0x01) ? 3 : 0;
    const uint8_t* display_size = buf + offset + MPEG_HEADER_LEN + skip_colour + sizeof (type);
    uint16_t horiz_disp_size  = *(display_size) << 6;
            horiz_disp_size += *(display_size+1) >> 2;
    uint16_t vert_disp_size = (*(display_size+1) & 0x01) << 13;
            vert_disp_size += *(display_size+2) << 5;
            vert_disp_size += *(display_size+3) >> 3;

    e_video_attr.width = horiz_disp_size;
    e_video_attr.height = vert_disp_size;

    return e_video_attr;
}

static void set_sequence_display_extension_sizes(uint8_t* buf, const unsigned int offset, p_video_attr_t e_video_attr)
{
    uint16_t horiz_disp_size = e_video_attr.width;
    uint16_t vert_disp_size = e_video_attr.height;

    uint8_t type = *(buf + offset + MPEG_HEADER_LEN);
    int skip_colour=(type&0x01) ? 3 : 0;
    uint8_t* display_size = buf + offset + MPEG_HEADER_LEN + skip_colour + sizeof (type);

    /* One could precalc this per program, but frequency is low so not worth the effort */
    *(uint32_t*)display_size = htonl(0x00020000);
    *(display_size)   |= (horiz_disp_size >> 6);
    *(display_size+1) |= (horiz_disp_size << 2);
    *(display_size+1) |= ((vert_disp_size >> 13) & 0x01);
    *(display_size+2) |= (vert_disp_size >> 5);
    *(display_size+3) |= (vert_disp_size << 3);
}

static void check_mpeg_encryption(uint8_t* buf, const unsigned int bs, const unsigned int program)
{
    /* Note we'll warn below if we've not seen any video stream (E0 doesn't match).
     * Note also I've only seen AC-3 audio on 0xBD and it has also been
     * encrypted on discs I've seen.  */
    if (ifo_program_attrs[program].scrambled != PARTIALLY_SCRAMBLED) {
        int pes_offset = find_mpeg_header(buf, bs-VIDEO_STREAM_LEN, VIDEO_STREAM_0);
        if (pes_offset >= 0) {
            /* extension header is always available for 0xBD and 0xE? types */
            uint8_t scramble_byte = *(buf + pes_offset + MPEG_HEADER_LEN + 2);
            bool scrambled;
            if ((scramble_byte & 0xC0) == 0x80) { /* MPEG2 */
                scrambled = scramble_byte & 0x30;
            } else {
                scrambled = false; /* assuming MPEG1 doesn't support encryption */
            }
            if (ifo_program_attrs[program].scrambled != SCRAMBLED_UNSET &&
                ifo_program_attrs[program].scrambled != scrambled) {
                ifo_program_attrs[program].scrambled = PARTIALLY_SCRAMBLED;
            } else {
                ifo_program_attrs[program].scrambled = scrambled;
            }
        }
    }
}

static void fix_mpeg2_aspect(uint8_t* buf, const unsigned int bs, const unsigned int program)
{
    static int sector;
    bool found_sequence_header = false;
    const bool look_harder = false; /* Should never need to be set to true as far as I can see */

    p_video_attr_t ifo_video_attr = ifo_video_attrs[ifo_program_attrs[program].video_attr];
    if (ifo_video_attr.aspect < 2) {
        sector++;
        return;
    }

    p_video_attr_t s_video_attr = { .aspect=ifo_video_attr.aspect, .width=-1, .height=-1 };

    if (sequence_offset == -1) {
        if ((sequence_offset = find_mpeg_header(buf, bs-SEQUENCE_LEN, SEQUENCE_ID)) >= 0) {
            found_sequence_header = true;
#ifndef NDEBUG
            fprintf(stdinfo,"Found SH  @ %d+%d\n", sector, sequence_offset);
#endif
            sequence_aspect = get_sequence_aspect(buf).aspect;
            if (sequence_aspect != ifo_video_attr.aspect) {
                set_sequence_aspect(buf, sequence_offset, s_video_attr);
            }
        }
    } else {
        if (find_mpeg_header(buf+sequence_offset, MPEG_HEADER_LEN, SEQUENCE_ID)==0) {
            found_sequence_header = true;
#ifndef NDEBUG
            fprintf(stdinfo,"Found SH  @ %d+%d\n", sector, sequence_offset);
#endif
            if (sequence_aspect!=ifo_video_attr.aspect)
                set_sequence_aspect(buf, sequence_offset, s_video_attr);
        } else if (look_harder) {
        /* I can't see why the sequence headers would be at arbitrary offsets in each sector,
         * and I've analyzed about 10 different VROs and they all have the same offsets.
         * So I think that doing this will only redundantly scan every byte of sectors
         * without sequence headers. */
            int curr_offset = find_mpeg_header(buf, bs-SEQUENCE_LEN, SEQUENCE_ID);
            if (curr_offset >= 0) {
                found_sequence_header = true;
                sequence_offset = curr_offset;
#ifndef NDEBUG
                fprintf(stdinfo,"Found SH @ %d+%d\n", sector, sequence_offset);
#endif
                set_sequence_aspect(buf, sequence_offset, s_video_attr);
            } else {
            }
        }
    }

    /* As an optimization, only look for sequence display extension, if there
     * is a sequence header in this sector. */
    if (found_sequence_header || look_harder) {
        if (ifo_video_attr.width <= 0 || ifo_video_attr.height <= 0) {
            sector++;
            return;
        }
        int extension_offset = look_harder ? 0 : sequence_offset + MPEG_HEADER_LEN + SEQUENCE_LEN;
        int next_offset;
        while ((next_offset = find_mpeg_header(buf + extension_offset,
                                               bs - extension_offset - SEQUENCE_EXTENSION_LEN,
                                               SEQUENCE_EXTENSION_ID)) >= 0) {
            extension_offset += next_offset;
            uint8_t type = *(buf + extension_offset + MPEG_HEADER_LEN);
            if ((type&0xF0) == 0x20) {
                p_video_attr_t e_video_attr = get_sequence_display_extension_sizes(buf, extension_offset);
#ifndef NDEBUG
                fprintf(stdinfo, "Found SDE @ %d+%d (%d x %d)\n", sector, extension_offset, e_video_attr.width, e_video_attr.height);
#endif
                e_video_attr.width = ifo_video_attr.width;
                e_video_attr.height = ifo_video_attr.height;
                set_sequence_display_extension_sizes(buf, extension_offset, e_video_attr);
#ifndef NDEBUG
                e_video_attr = get_sequence_display_extension_sizes(buf, extension_offset);
                fprintf(stdinfo, "New   SDE @ %d+%d (%d x %d)\n", sector, extension_offset, e_video_attr.width, e_video_attr.height);
#endif
                break; /* Should be only 1 of these per sector */
            } else {
#ifndef NDEBUG
                fprintf(stdinfo, "Found SE  @ %d+%d (type=%d)\n", sector, extension_offset, (type&0xF0)>>4);
#endif
            }
            extension_offset++;
        }
    }

    sector++;
}

/* Haven't had a request to do this yet:
   http://forum.doom9.org/archive/index.php/t-102969.html
   Note code there makes incorrect assumptions about offsets I think.  */
static void add_mpeg_nav(uint8_t* buf, const unsigned int bs)
{
    (void) buf; (void) bs;
}

void process_mpeg2(uint8_t* buf, const unsigned int bs, void* program)
{
    fix_mpeg2_aspect(buf, bs, *(const unsigned int*)program);
    add_mpeg_nav(buf, bs);
    check_mpeg_encryption(buf, bs, *(const unsigned int*)program);
}

/*********************************************************************************
 *
 *********************************************************************************/

unsigned long required_program=0; /* process all programs by default */
const char* ifo_name=NULL;
const char* vro_name=NULL;

static void usage(char** argv, int error)
{
    FILE* where = error==EXIT_FAILURE ? stderr : stdout;

    fprintf(where, "Usage: %s [OPTION]... VR_MANGR.IFO [VR_MOVIE.VRO]\n"
                   "Print info about and optionally extract vob data from DVD-VR files.\n"
                   "\n"
                   "If the VRO file is specified, the component programs are\n"
                   "extracted to the current directory or to stdout.\n"
                   "\n"
                   "  -p, --program=NUM  Only process program NUM rather than all programs.\n"
                   "\n"
                   "  -n, --name=NAME    Specify a basename to use for extracted vob files\n"
                   "                     rather than using one based on the timestamp.\n"
                   "                     If you pass `-' the vob files will be written to stdout.\n"
                   "                     If you pass `[label]' the names will be based on\n"
                   "                     a sanitized version of the title or label.\n"
                   "\n"
                   "      --help         Display this help and exit.\n"
                   "      --version      Output version information and exit.\n"
                   ,argv[0]);
    exit(error);
}

static void get_options(int argc, char** argv)
{
    static struct option const longopts[] =
    {
        /* I'm using capitals for long options
         * without a corresponding short option. */
        {"program", required_argument, NULL, 'p'},
        {"name", required_argument, NULL, 'n'},
        {"help", no_argument, NULL, 'H'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:n:", longopts, NULL)) != -1) {
        switch (opt) {
        case 'p': {
            char* trailing;
            required_program = strtoul(optarg, &trailing, 10);
            if (*trailing) {
                usage(argv, EXIT_FAILURE);
            }
            break;
        }
        case 'n':
            base_name = optarg;
            break;
        case 'V':
            printf("dvd-vr "VERSION);
            printf("\n\nWritten by Pádraig Brady <P@draigBrady.com>\n");
            exit(EXIT_SUCCESS);
            break;
        case 'H':
            usage(argv, EXIT_SUCCESS);
            break;
        default: /* '?',':' */
            usage(argv, EXIT_FAILURE);
            break;
        }
    }
    if (optind >= argc ||   /* no files specified */
        argc > optind+2) {  /* too many files specified */
        usage(argv, EXIT_FAILURE);
    }

    ifo_name=argv[optind++];

    if (optind < argc) {
        vro_name=argv[optind++];
    }

    if (!STREQ(base_name, TIMESTAMP_FMT) && !vro_name) {
        usage(argv, EXIT_FAILURE);
    }
}

int main(int argc, char** argv)
{
    setlocale(LC_ALL,"");
    sys_charset=get_charset();

    get_options(argc, argv);

    if (STREQ(base_name, "-")) {
        stdinfo = stderr;
    } else {
        stdinfo = stdout; /* allow users to grep metadata etc. */
    }

    int fd=open(ifo_name,O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Error opening [%s] (%s)\n", ifo_name, strerror(errno));
        exit(EXIT_FAILURE);
    }

    rtav_vmgi_t* rtav_vmgi_ptr=mmap(0,sizeof(rtav_vmgi_t),PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
    if (rtav_vmgi_ptr == MAP_FAILED) {
        fprintf(stderr, "Failed to MMAP ifo file (%s)\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (strncmp("DVD_RTR_VMG0",rtav_vmgi_ptr->mat.id,sizeof(rtav_vmgi_ptr->mat.id))) {
        fprintf(stderr, "invalid DVD-VR IFO identifier\n");
        exit(EXIT_FAILURE);
    }

    uint32_t vmg_size = NTOHL(rtav_vmgi_ptr->mat.vmg_ea) + 1;
    if (munmap(rtav_vmgi_ptr, sizeof(rtav_vmgi_t)) !=0) {
        fprintf(stderr, "Failed to unmap ifo file (%s)\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    rtav_vmgi_ptr=mmap(0,vmg_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
    if (rtav_vmgi_ptr == MAP_FAILED) {
        fprintf(stderr, "Failed to re MMAP ifo file (%s)\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int vro_fd=-1;
    if (vro_name) {
        vro_fd=open(vro_name,O_RDONLY);
        if (vro_fd == -1) {
            fprintf(stderr, "Error opening [%s] (%s)\n", vro_name, strerror(errno));
            exit(EXIT_FAILURE);
        }
#ifdef POSIX_FADV_SEQUENTIAL
        posix_fadvise(vro_fd, 0, 0, POSIX_FADV_SEQUENTIAL);/* More readahead done */
#endif //POSIX_FADV_SEQUENTIAL
    }

    NTOHS(rtav_vmgi_ptr->mat.version);
    rtav_vmgi_ptr->mat.version &= 0x00FF;
    fprintf(stdinfo, "format: DVD-VR V%d.%d\n",
            rtav_vmgi_ptr->mat.version>>4,rtav_vmgi_ptr->mat.version&0x0F);
    if (rtav_vmgi_ptr->mat.cprm.supported) {
        fprintf(stdinfo, "Encryption: CPRM supported\n");
        /* Note programs may not actually be encrypted.
         * That's indicated per AV pack in 2 PES scrambling control bits */
    }

    disc_charset=parse_txt_encoding(rtav_vmgi_ptr->mat.txt_encoding);

    print_disc_info(rtav_vmgi_ptr);

    NTOHL(rtav_vmgi_ptr->mat.pgit_sa);
    pgiti_t* pgiti = (pgiti_t*) ((char*)rtav_vmgi_ptr + rtav_vmgi_ptr->mat.pgit_sa);
    NTOHL(pgiti->pgit_ea);

    NTOHL(rtav_vmgi_ptr->mat.def_psi_sa);
    psi_gi_t *def_psi_gi = (psi_gi_t*) ((char*)rtav_vmgi_ptr + rtav_vmgi_ptr->mat.def_psi_sa);

#ifndef NDEBUG
    NTOHS(def_psi_gi->nr_of_programs);
    if ((def_psi_gi->nr_of_psi > 1) && (def_psi_gi->nr_of_psi != def_psi_gi->nr_of_programs)) {
        print_psi(def_psi_gi);
    }
    fprintf(stdinfo, "Number of info tables for VRO: %d\n",pgiti->nr_of_pgi);
    fprintf(stdinfo, "Number of vob formats: %d\n",pgiti->nr_of_vob_formats);
    fprintf(stdinfo, "pgit_ea: %08"PRIX32"\n",pgiti->pgit_ea);
#endif//NDEBUG

    if (pgiti->nr_of_pgi == 0) {
        fprintf(stderr, "Error: couldn't find info table for VRO\n");
        exit(EXIT_FAILURE);
    }
    if (pgiti->nr_of_pgi > 1) {
        fprintf(stderr, "Warning: Only processing 1 of the %"PRIu8" VRO info tables\n",
                pgiti->nr_of_pgi);
    }

    vob_format_t* vob_format = (vob_format_t*) (pgiti+1);
    int vob_type;
    int vob_types=pgiti->nr_of_vob_formats;
    ifo_video_attrs=malloc(vob_types * sizeof(p_video_attr_t));
    if (!ifo_video_attrs) {
        fprintf(stderr, "Error allocating space for video type attributes\n");
        exit(EXIT_FAILURE);
    }
    for (vob_type=0; vob_type<vob_types; vob_type++) {
        putc('\n', stdinfo);
        if (vob_types>1) {
            fprintf(stdinfo, "VOB format %d...\n",vob_type+1);
        }
        NTOHS(vob_format->video_attr);
        if (!parse_video_attr(vob_format->video_attr, &ifo_video_attrs[vob_type])) {
            fprintf(stderr, "Error parsing video_attr\n");
        }
        if (!parse_audio_attr(vob_format->audio_attr0)) {
            fprintf(stderr, "Error parsing audio_attr0\n");
        }
        vob_format++;
    }

    pgi_gi_t* pgi_gi = (pgi_gi_t*) vob_format;
    NTOHS(pgi_gi->nr_of_programs);
    fprintf(stdinfo, "\nNumber of programs: %d\n", pgi_gi->nr_of_programs);
    if (required_program && required_program>pgi_gi->nr_of_programs) {
        fprintf(stderr, "Error: couldn't find specified program (%lu)\n", required_program);
        exit(EXIT_FAILURE);
    }
    ifo_program_attrs=malloc(pgi_gi->nr_of_programs * sizeof(p_program_attr_t));
    if (!ifo_program_attrs) {
        fprintf(stderr, "Error allocating space for program attributes\n");
        exit(EXIT_FAILURE);
    }

    struct tm now_tm;
    time_t now=time(0);
    (void) gmtime_r(&now, &now_tm);//used if no timestamp in program
    unsigned int program;
    typedef uint32_t vvobi_sa_t;
    vvobi_sa_t* vvobi_sa=(vvobi_sa_t*)(pgi_gi+1);
    for (program=0; program<pgi_gi->nr_of_programs; program++) {

        if (required_program && program+1!=required_program) {
            vvobi_sa++;
            continue;
        }

        NTOHL(*vvobi_sa);

        putc('\n', stdinfo);
        fprintf(stdinfo, "num  : %d\n", program+1);

        psi_t* psi=find_program_text_info(def_psi_gi, program+1);
        if (psi) {
            print_label(psi);
        } else {
            fprintf(stdinfo, "label: Couldn't find. Please report.\n");
        }

#ifndef NDEBUG
        fprintf(stdinfo, "VVOB info (%d) address: %"PRIu32"\n",program+1,*vvobi_sa);
#endif//NDEBUG
        vvob_t* vvob = (vvob_t*) (((uint8_t*)pgiti) + *vvobi_sa);
        struct tm tm;
        bool ts_ok = parse_pgtm(vvob->vob_timestamp,&tm);
        char vob_base[(sizeof(psi->title)*MB_LEN_MAX)+4/*#123*/+1/*NUL*/];
        if (STREQ(base_name, TIMESTAMP_FMT)) { //use timestamp to give unique filename
            if (ts_ok) {
                strftime(vob_base,sizeof(vob_base),TIMESTAMP_FMT,&tm);
            } else { //use now + program num to give unique name
                strftime(vob_base,sizeof(vob_base),TIMESTAMP_FMT,&now_tm);
                int datelen=strlen(vob_base);
                (void) snprintf(vob_base+datelen, sizeof(vob_base)-datelen, "#%03d", program+1);
            }
        } else if (STREQ(base_name, "[label]")) { //use the label to generate filename
            if (!psi) {
                fprintf(stderr, "Error: Couldn't generate name based on label\n");
                exit(EXIT_FAILURE);
            }
            char* label_base = get_label_base(psi);
            if (!label_base) {
                fprintf(stderr, "Error: Couldn't generate name based on empty label\n");
                exit(EXIT_FAILURE);
            }
            unsigned int ret = snprintf(vob_base, sizeof(vob_base), "%s#%03d", label_base, program+1);
            if (ret >= sizeof(vob_base)) { /* Shouldn't happen.  */
                fprintf(stderr, "Error: label is too long\n");
                exit(EXIT_FAILURE);
            }
            free(label_base);
        } else {
            unsigned int ret = snprintf(vob_base, sizeof(vob_base), "%s#%03d", base_name, program+1);
            if (ret >= sizeof(vob_base)) {
                fprintf(stderr, "Error: Specified basename is too long (>%zu)\n", sizeof(vob_base)-4);
                exit(EXIT_FAILURE);
            }
        }

        int vob_fd=-1;
        char vob_name[sizeof(vob_base)+4];
        if (vro_fd!=-1) {
            if (STREQ(base_name, "-")) {
                vob_fd=fileno(stdout);
            } else {
                (void) snprintf(vob_name,sizeof(vob_name),"%s.vob",vob_base);
                vob_fd=open(vob_name,O_WRONLY|O_CREAT|O_EXCL,0666);
                if (vob_fd == -1 && errno == EEXIST && STREQ(base_name, TIMESTAMP_FMT)) {
                    /* JVC DVD recorder can generate duplicate timestamps at least :( */
                    /* FIXME: The second time ripping a disc will duplicate the first VOB with duplicate timestamp.
                    * Would need to scan all program info first and change format if any duplicate timestamps. */
                    (void) snprintf(vob_name,sizeof(vob_name),"%s#%03d.vob",vob_base, program+1);
                    vob_fd=open(vob_name,O_WRONLY|O_CREAT|O_EXCL,0666);
                }
            }
            if (vob_fd == -1) {
                fprintf(stderr, "Error opening [%s] (%s)\n", vob_name, strerror(errno));
                vvobi_sa++;
                continue;
            }
        }

        if (vob_types>1) {
            fprintf(stdinfo, "vob format: %d\n", vvob->vob_format_id);
        }
        ifo_program_attrs[program].video_attr = vvob->vob_format_id-1;
        ifo_program_attrs[program].scrambled = SCRAMBLED_UNSET;

        NTOHS(vvob->vob_attr);
        int skip=0;
        if (vvob->vob_attr & 0x80) {
            skip+=sizeof(adj_vob_t);
#ifndef NDEBUG
            fprintf(stdinfo, "skipping adjacent VOB info\n");
#endif//NDEBUG
        }
        skip+=sizeof(uint16_t); /* ?? */
        vobu_map_t* vobu_map = (vobu_map_t*) (((uint8_t*)(vvob+1)) + skip);
        NTOHS(vobu_map->nr_of_time_info);
        NTOHS(vobu_map->nr_of_vobu_info);
        NTOHS(vobu_map->time_offset);
        NTOHL(vobu_map->vob_offset);
#ifndef NDEBUG
hexdump(vobu_map, sizeof(vobu_map_t));
        fprintf(stdinfo, "num time infos:   %"PRIu16"\n",vobu_map->nr_of_time_info);
        fprintf(stdinfo, "num VOBUs: %"PRIu16"\n",vobu_map->nr_of_vobu_info);
        fprintf(stdinfo, "time offset:      %"PRIu16"\n",vobu_map->time_offset); /* What units? */
        fprintf(stdinfo, "vob offset:     %"PRIu32"*%d\n",vobu_map->vob_offset,DVD_SECTOR_SIZE);  /* offset in the VRO file of the VOB */
#endif//NDEBUG
        off_t vob_offset = vobu_map->vob_offset;
        if (vob_offset > OFF_T_MAX / DVD_SECTOR_SIZE)
        {
            fprintf(stderr, "Overflow in extracting VOB at offset %"PRIu32"*%d\n",vobu_map->vob_offset,DVD_SECTOR_SIZE);
            exit(EXIT_FAILURE);
        }
        vob_offset *= DVD_SECTOR_SIZE;
        if (vro_fd!=-1) {
            if (lseek(vro_fd, vob_offset, SEEK_SET)==(off_t)-1) {
                fprintf(stderr, "Error seeking within VRO [%s]\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
        }
        vobu_info_t* vobu_info = (vobu_info_t*) (((uint8_t*)(vobu_map+1)) + vobu_map->nr_of_time_info*sizeof(time_info_t));
        int vobus;
        uint64_t tot=0;
        int display_char;
        bool processed_some_video = false;
        int error=0;
        if (vro_fd != -1) {
            percent_display(PERCENT_START, 0, 0);
            init_mpeg2_cache();
        }
        for (vobus=0; vobus<vobu_map->nr_of_vobu_info; vobus++) {
            uint16_t vobu_size = *(uint16_t*)(&vobu_info->vobu_info[1]);
            NTOHS(vobu_size); vobu_size&=0x03FF;
#ifndef NDEBUG
		fprintf(stdinfo, "vobu #%d size: %d\n", vobus, vobu_size);
#endif
            if (vro_fd != -1) {
                off_t curr_offset = lseek(vro_fd, 0, SEEK_CUR);
                if (curr_offset == (off_t)-1) {
                    fprintf(stderr, "Error determining VRO offset [%s]\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                int ret = stream_data(vro_fd, vob_fd, vobu_size, DVD_SECTOR_SIZE, process_mpeg2, &program);
                if (ret == -2) { /* write error */
                    exit(EXIT_FAILURE);
                } else if (ret == -1) { /* read error */
                    display_char='X';
                    error=1;
                    off_t new_offset = lseek(vro_fd, 0, SEEK_CUR);
                    if (new_offset == (off_t)-1) {
                        fprintf(stderr, "Error determining VRO offset [%s]\n", strerror(errno));
                        exit(EXIT_FAILURE);
                    }
                    off_t skip_len = (curr_offset + vobu_size*DVD_SECTOR_SIZE) - new_offset;
                    if (skip_len) {
#ifndef NDEBUG
                        fprintf(stderr, "Warning: Skipping %"PRIdMAX" bytes\n", skip_len);
                        /* Note we mark the whole VOBU as bad not just this skip len */
#endif//NDEBUG
                        if (lseek(vro_fd, skip_len, SEEK_CUR) == (off_t)-1) {
                            fprintf(stderr, "Error skipping in VRO [%s]\n", strerror(errno));
                            exit(EXIT_FAILURE);
                        }
                    }
                } else if (ifo_program_attrs[program].scrambled == SCRAMBLED ||
                           ifo_program_attrs[program].scrambled == PARTIALLY_SCRAMBLED) {
                    display_char='E';
                    processed_some_video = true;
                } else {
                    display_char=0; /* default */
                    processed_some_video = true;
                }

                int percent=((vobus+1)*100)/vobu_map->nr_of_vobu_info;
                percent_display(PERCENT_UPDATE, percent, display_char);
            }
            tot+=vobu_size;
            vobu_info++;
        }
        if (vro_fd != -1) {
            if (!error) {
                percent_display(PERCENT_END, 0, 0);
            } else {
                /* Leave the percent display showing read errors */
                putc('\n', stderr);
            }
            if (vob_fd != fileno(stdout)) {
                close(vob_fd);
                touch(vob_name, &tm);
            }
        }

        fprintf(stdinfo, "size : %'"PRIu64"\n",tot*DVD_SECTOR_SIZE);

        if (ifo_program_attrs[program].scrambled == SCRAMBLED) {
            fprintf(stderr, "Warning: program is encrypted\n");
        } else if (ifo_program_attrs[program].scrambled == PARTIALLY_SCRAMBLED) {
            fprintf(stderr, "Warning: program is partially encrypted\n");
        } else if (ifo_program_attrs[program].scrambled == SCRAMBLED_UNSET && processed_some_video) {
            fprintf(stderr, "Warning: didn't detect a video stream, please report\n");
            fprintf(stderr, "  (preferably with a sample vob file)\n");
        }

        vvobi_sa++;
    }

    free(ifo_program_attrs);
    free(ifo_video_attrs);
    munmap(rtav_vmgi_ptr, vmg_size);
    close(fd);
    if (vro_fd != -1)
        close(vro_fd);

    return EXIT_SUCCESS;
}
