/**
 * FFmpeg libavutil - 简化头文件
 * 包含基本类型和常用工具
 */

#ifndef AVUTIL_AVUTIL_H
#define AVUTIL_AVUTIL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 时间基准结构
typedef struct AVRational {
    int num; // 分子
    int den; // 分母
} AVRational;

// 采样格式
enum AVSampleFormat {
    AV_SAMPLE_FMT_NONE = -1,
    AV_SAMPLE_FMT_U8,          // unsigned 8 bits
    AV_SAMPLE_FMT_S16,         // signed 16 bits
    AV_SAMPLE_FMT_S32,         // signed 32 bits
    AV_SAMPLE_FMT_FLT,         // float
    AV_SAMPLE_FMT_DBL,         // double
    AV_SAMPLE_FMT_U8P,         // unsigned 8 bits, planar
    AV_SAMPLE_FMT_S16P,        // signed 16 bits, planar
    AV_SAMPLE_FMT_S32P,        // signed 32 bits, planar
    AV_SAMPLE_FMT_FLTP,        // float, planar
    AV_SAMPLE_FMT_DBLP,        // double, planar
    AV_SAMPLE_FMT_S64,         // signed 64 bits
    AV_SAMPLE_FMT_S64P,        // signed 64 bits, planar
    AV_SAMPLE_FMT_NB           // Number of sample formats
};

// 像素格式（部分）
enum AVPixelFormat {
    AV_PIX_FMT_NONE = -1,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUYV422,
    AV_PIX_FMT_RGB24,
    AV_PIX_FMT_BGR24,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_RGBA,
    AV_PIX_FMT_BGRA,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV21,
};

// 声道布局
#define AV_CH_FRONT_LEFT             0x00000001
#define AV_CH_FRONT_RIGHT            0x00000002
#define AV_CH_FRONT_CENTER           0x00000004
#define AV_CH_LOW_FREQUENCY          0x00000008
#define AV_CH_LAYOUT_MONO            (AV_CH_FRONT_CENTER)
#define AV_CH_LAYOUT_STEREO          (AV_CH_FRONT_LEFT|AV_CH_FRONT_RIGHT)

// 错误码
#define AVERROR(e)                   (-(e))
#define AVERROR_EOF                  (-541478725)  // FFERRTAG('E','O','F',' ')
#define AVERROR_EAGAIN               (-11)         // EAGAIN

// 内存函数
void* av_malloc(size_t size);
void* av_mallocz(size_t size);
void* av_realloc(void* ptr, size_t size);
void av_free(void* ptr);
void av_freep(void* ptr);
char* av_strdup(const char* s);

// 日志函数
void av_log(void* avcl, int level, const char* fmt, ...);
#define AV_LOG_QUIET    -8
#define AV_LOG_PANIC     0
#define AV_LOG_FATAL     8
#define AV_LOG_ERROR    16
#define AV_LOG_WARNING  24
#define AV_LOG_INFO     32
#define AV_LOG_VERBOSE  40
#define AV_LOG_DEBUG    48
#define AV_LOG_TRACE    56

// 采样格式工具
int av_get_bytes_per_sample(enum AVSampleFormat sample_fmt);
int av_sample_fmt_is_planar(enum AVSampleFormat sample_fmt);
int av_samples_get_buffer_size(int* linesize, int nb_channels, int nb_samples,
                               enum AVSampleFormat sample_fmt, int align);

#ifdef __cplusplus
}
#endif

#endif /* AVUTIL_AVUTIL_H */
