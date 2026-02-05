/*
 * LAME MP3 Encoder Interface
 * Copyright (C) 1999-2011 The LAME Project
 * Simplified header for Formidable2026 project
 */

#ifndef LAME_LAME_H
#define LAME_LAME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#  ifdef LAME_BUILDING_DLL
#    define CDECL __cdecl
#    define LAME_EXPORT __declspec(dllexport)
#  else
#    define CDECL __cdecl
#    define LAME_EXPORT __declspec(dllimport)
#  endif
#else
#  define CDECL
#  define LAME_EXPORT
#endif

/* Opaque structure for LAME encoder */
typedef struct lame_global_struct lame_global_flags;
typedef lame_global_flags *lame_t;

/* VBR modes */
typedef enum vbr_mode_e {
    vbr_off = 0,
    vbr_mt,
    vbr_rh,
    vbr_abr,
    vbr_mtrh,
    vbr_default = vbr_mtrh
} vbr_mode;

/* MPEG modes */
typedef enum MPEG_mode_e {
    STEREO = 0,
    JOINT_STEREO,
    DUAL_CHANNEL,
    MONO,
    NOT_SET,
    MAX_INDICATOR
} MPEG_mode;

/* Initialize and configure */
LAME_EXPORT lame_global_flags * CDECL lame_init(void);
LAME_EXPORT int CDECL lame_close(lame_global_flags *);

/* Input settings */
LAME_EXPORT int CDECL lame_set_num_samples(lame_global_flags *, unsigned long);
LAME_EXPORT int CDECL lame_set_in_samplerate(lame_global_flags *, int);
LAME_EXPORT int CDECL lame_set_num_channels(lame_global_flags *, int);

/* Output settings */
LAME_EXPORT int CDECL lame_set_out_samplerate(lame_global_flags *, int);
LAME_EXPORT int CDECL lame_set_brate(lame_global_flags *, int);
LAME_EXPORT int CDECL lame_set_quality(lame_global_flags *, int);
LAME_EXPORT int CDECL lame_set_mode(lame_global_flags *, MPEG_mode);
LAME_EXPORT int CDECL lame_set_VBR(lame_global_flags *, vbr_mode);
LAME_EXPORT int CDECL lame_set_VBR_quality(lame_global_flags *, float);

/* Initialize encoder parameters */
LAME_EXPORT int CDECL lame_init_params(lame_global_flags *);

/* Encode samples */
LAME_EXPORT int CDECL lame_encode_buffer(
        lame_global_flags*  gfp,
        const short int     buffer_l[],
        const short int     buffer_r[],
        const int           nsamples,
        unsigned char*      mp3buf,
        const int           mp3buf_size);

LAME_EXPORT int CDECL lame_encode_buffer_interleaved(
        lame_global_flags*  gfp,
        short int           pcm[],
        int                 num_samples,
        unsigned char*      mp3buf,
        int                 mp3buf_size);

/* Flush remaining samples */
LAME_EXPORT int CDECL lame_encode_flush(
        lame_global_flags*  gfp,
        unsigned char*      mp3buf,
        int                 size);

/* Version info */
LAME_EXPORT const char* CDECL get_lame_version(void);
LAME_EXPORT const char* CDECL get_lame_short_version(void);

#ifdef __cplusplus
}
#endif

#endif /* LAME_LAME_H */
