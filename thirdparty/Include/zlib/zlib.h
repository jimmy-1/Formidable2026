/* zlib.h -- interface of the 'zlib' general purpose compression library
  version 1.2.13, October 13th, 2022
  Copyright (C) 1995-2022 Jean-loup Gailly and Mark Adler
  Simplified header for Formidable2026 project
*/

#ifndef ZLIB_H
#define ZLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#define ZLIB_VERSION "1.2.13"
#define ZLIB_VERNUM 0x12d0
#define ZLIB_VER_MAJOR 1
#define ZLIB_VER_MINOR 2
#define ZLIB_VER_REVISION 13

/* Constants */
#define Z_NO_FLUSH      0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH    2
#define Z_FULL_FLUSH    3
#define Z_FINISH        4
#define Z_BLOCK         5
#define Z_TREES         6

/* Return codes */
#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO        (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)
#define Z_VERSION_ERROR (-6)

/* Compression levels */
#define Z_NO_COMPRESSION         0
#define Z_BEST_SPEED             1
#define Z_BEST_COMPRESSION       9
#define Z_DEFAULT_COMPRESSION  (-1)

/* Compression strategy */
#define Z_FILTERED            1
#define Z_HUFFMAN_ONLY        2
#define Z_RLE                 3
#define Z_FIXED               4
#define Z_DEFAULT_STRATEGY    0

/* Types */
typedef unsigned char  Byte;
typedef unsigned int   uInt;
typedef unsigned long  uLong;
typedef Byte  *Bytef;
typedef void  *voidpf;
typedef void  *voidp;
typedef uLong uLongf;

#ifdef _WIN32
#  ifdef ZLIB_DLL
#    define ZEXTERN __declspec(dllexport)
#  else
#    define ZEXTERN __declspec(dllimport)
#  endif
#  define ZEXPORT __cdecl
#else
#  define ZEXTERN extern
#  define ZEXPORT
#endif

/* Basic functions */
ZEXTERN int ZEXPORT compress(Bytef *dest, uLongf *destLen,
                             const Bytef *source, uLong sourceLen);

ZEXTERN int ZEXPORT compress2(Bytef *dest, uLongf *destLen,
                              const Bytef *source, uLong sourceLen,
                              int level);

ZEXTERN uLong ZEXPORT compressBound(uLong sourceLen);

ZEXTERN int ZEXPORT uncompress(Bytef *dest, uLongf *destLen,
                               const Bytef *source, uLong sourceLen);

ZEXTERN int ZEXPORT uncompress2(Bytef *dest, uLongf *destLen,
                                const Bytef *source, uLong *sourceLen);

/* Utility functions */
ZEXTERN uLong ZEXPORT crc32(uLong crc, const Bytef *buf, uInt len);
ZEXTERN uLong ZEXPORT adler32(uLong adler, const Bytef *buf, uInt len);

ZEXTERN const char * ZEXPORT zlibVersion(void);

#ifdef __cplusplus
}
#endif

#endif /* ZLIB_H */
