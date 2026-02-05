/*
 * Copyright (C)2009-2023 D. R. Commander.  All Rights Reserved.
 * TurboJPEG API header - Simplified version for Formidable2026
 */

#ifndef __TURBOJPEG_H__
#define __TURBOJPEG_H__

#if defined(_WIN32) && defined(DLLDEFINE)
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif
#define DLLCALL

#ifdef __cplusplus
extern "C" {
#endif

/* Pixel formats */
enum TJPF {
    TJPF_RGB = 0,
    TJPF_BGR,
    TJPF_RGBX,
    TJPF_BGRX,
    TJPF_XBGR,
    TJPF_XRGB,
    TJPF_GRAY,
    TJPF_RGBA,
    TJPF_BGRA,
    TJPF_ABGR,
    TJPF_ARGB,
    TJPF_CMYK,
    TJPF_UNKNOWN = -1
};

/* Chrominance subsampling options */
enum TJSAMP {
    TJSAMP_444 = 0,
    TJSAMP_422,
    TJSAMP_420,
    TJSAMP_GRAY,
    TJSAMP_440,
    TJSAMP_411,
    TJSAMP_441
};

/* Flags */
#define TJFLAG_BOTTOMUP        2
#define TJFLAG_FASTUPSAMPLE    256
#define TJFLAG_NOREALLOC       1024
#define TJFLAG_FASTDCT         2048
#define TJFLAG_ACCURATEDCT     4096

/* TurboJPEG instance handle */
typedef void *tjhandle;

/* API functions */
DLLEXPORT tjhandle DLLCALL tjInitCompress(void);
DLLEXPORT tjhandle DLLCALL tjInitDecompress(void);

DLLEXPORT int DLLCALL tjCompress2(tjhandle handle, const unsigned char *srcBuf,
    int width, int pitch, int height, int pixelFormat,
    unsigned char **jpegBuf, unsigned long *jpegSize,
    int jpegSubsamp, int jpegQual, int flags);

DLLEXPORT int DLLCALL tjDecompressHeader3(tjhandle handle,
    const unsigned char *jpegBuf, unsigned long jpegSize,
    int *width, int *height, int *jpegSubsamp, int *jpegColorspace);

DLLEXPORT int DLLCALL tjDecompress2(tjhandle handle,
    const unsigned char *jpegBuf, unsigned long jpegSize,
    unsigned char *dstBuf, int width, int pitch, int height,
    int pixelFormat, int flags);

DLLEXPORT int DLLCALL tjDestroy(tjhandle handle);

DLLEXPORT unsigned char* DLLCALL tjAlloc(int bytes);
DLLEXPORT void DLLCALL tjFree(unsigned char *buffer);

DLLEXPORT int DLLCALL tjBufSize(int width, int height, int jpegSubsamp);
DLLEXPORT char* DLLCALL tjGetErrorStr2(tjhandle handle);
DLLEXPORT int DLLCALL tjGetErrorCode(tjhandle handle);

/* Pixel size array */
static const int tjPixelSize[12] = {
    3, 3, 4, 4, 4, 4, 1, 4, 4, 4, 4, 4
};

#ifdef __cplusplus
}
#endif

#endif /* __TURBOJPEG_H__ */
