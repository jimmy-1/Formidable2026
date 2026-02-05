/**
 * FFmpeg libavcodec - 简化头文件
 * 仅包含 MP3 解码必要的 API
 */

#ifndef AVCODEC_AVCODEC_H
#define AVCODEC_AVCODEC_H

#include "../libavutil/avutil.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AV_INPUT_BUFFER_PADDING_SIZE 64

// 编解码器 ID
enum AVCodecID {
    AV_CODEC_ID_NONE = 0,
    AV_CODEC_ID_MP3 = 0x15001,
    AV_CODEC_ID_AAC = 0x15002,
    AV_CODEC_ID_PCM_S16LE = 65536,
    AV_CODEC_ID_PCM_S16BE,
    AV_CODEC_ID_PCM_U16LE,
    AV_CODEC_ID_PCM_U16BE,
};

// 前向声明
typedef struct AVCodec AVCodec;
typedef struct AVPacket AVPacket;
typedef struct AVFrame AVFrame;
typedef struct AVCodecParserContext AVCodecParserContext;

// AVCodecContext 结构 (简化，仅包含音频解码必需字段)
typedef struct AVCodecContext {
    const struct AVClass* av_class;
    int log_level_offset;
    enum AVMediaType codec_type;
    const AVCodec* codec;
    enum AVCodecID codec_id;
    unsigned int codec_tag;
    void* priv_data;
    void* internal;
    void* opaque;
    int64_t bit_rate;
    int bit_rate_tolerance;
    int global_quality;
    int compression_level;
    int flags;
    int flags2;
    uint8_t* extradata;
    int extradata_size;
    AVRational time_base;
    int ticks_per_frame;
    int delay;
    int width, height;
    int coded_width, coded_height;
    int gop_size;
    enum AVPixelFormat pix_fmt;
    int max_b_frames;
    float b_quant_factor;
    float b_quant_offset;
    int has_b_frames;
    float i_quant_factor;
    float i_quant_offset;
    float lumi_masking;
    float temporal_cplx_masking;
    float spatial_cplx_masking;
    float p_masking;
    float dark_masking;
    int slice_count;
    int* slice_offset;
    AVRational sample_aspect_ratio;
    int me_cmp;
    int me_sub_cmp;
    int mb_cmp;
    int ildct_cmp;
    int dia_size;
    int last_predictor_count;
    int me_pre_cmp;
    int pre_dia_size;
    int me_subpel_quality;
    int me_range;
    int slice_flags;
    int mb_decision;
    uint16_t* intra_matrix;
    uint16_t* inter_matrix;
    int intra_dc_precision;
    int skip_top;
    int skip_bottom;
    int mb_lmin;
    int mb_lmax;
    int bidir_refine;
    int keyint_min;
    int refs;
    int mv0_threshold;
    int color_primaries;
    int color_trc;
    int colorspace;
    int color_range;
    int chroma_sample_location;
    int slices;
    int field_order;
    // 音频相关字段
    int sample_rate;
    int channels;
    enum AVSampleFormat sample_fmt;
    int frame_size;
    int frame_number;
    int block_align;
    int cutoff;
    uint64_t channel_layout;
    uint64_t request_channel_layout;
    int audio_service_type;
    enum AVSampleFormat request_sample_fmt;
    // ... 其他字段省略
} AVCodecContext;

// 媒体类型
enum AVMediaType {
    AVMEDIA_TYPE_UNKNOWN = -1,
    AVMEDIA_TYPE_VIDEO,
    AVMEDIA_TYPE_AUDIO,
    AVMEDIA_TYPE_DATA,
    AVMEDIA_TYPE_SUBTITLE,
    AVMEDIA_TYPE_ATTACHMENT,
    AVMEDIA_TYPE_NB
};

// AVPacket 结构 (简化)
struct AVPacket {
    int64_t pts;
    int64_t dts;
    uint8_t* data;
    int size;
    int stream_index;
    int flags;
    int64_t duration;
    int64_t pos;
    void* opaque;
    void* opaque_ref;
    AVRational time_base;
    // ... 更多字段省略
};

// AVFrame 结构 (简化)
struct AVFrame {
    uint8_t* data[8];
    int linesize[8];
    uint8_t** extended_data;
    int width, height;
    int nb_samples;
    int format;
    int key_frame;
    int64_t pts;
    int64_t pkt_dts;
    AVRational sample_aspect_ratio;
    int sample_rate;
    int channels;
    uint64_t channel_layout;
    // ... 更多字段省略
};

// 编解码器查找函数
const AVCodec* avcodec_find_decoder(enum AVCodecID id);
const AVCodec* avcodec_find_encoder(enum AVCodecID id);

// 编解码器上下文
AVCodecContext* avcodec_alloc_context3(const AVCodec* codec);
void avcodec_free_context(AVCodecContext** avctx);
int avcodec_open2(AVCodecContext* avctx, const AVCodec* codec, void** options);
int avcodec_close(AVCodecContext* avctx);

// 编解码操作
int avcodec_send_packet(AVCodecContext* avctx, const AVPacket* avpkt);
int avcodec_receive_frame(AVCodecContext* avctx, AVFrame* frame);
int avcodec_send_frame(AVCodecContext* avctx, const AVFrame* frame);
int avcodec_receive_packet(AVCodecContext* avctx, AVPacket* avpkt);

// 内存管理
AVPacket* av_packet_alloc(void);
void av_packet_free(AVPacket** pkt);
void av_packet_unref(AVPacket* pkt);
int av_new_packet(AVPacket* pkt, int size);

AVFrame* av_frame_alloc(void);
void av_frame_free(AVFrame** frame);
void av_frame_unref(AVFrame* frame);

// 解析器
AVCodecParserContext* av_parser_init(int codec_id);
int av_parser_parse2(AVCodecParserContext* s,
                     AVCodecContext* avctx,
                     uint8_t** poutbuf, int* poutbuf_size,
                     const uint8_t* buf, int buf_size,
                     int64_t pts, int64_t dts,
                     int64_t pos);
void av_parser_close(AVCodecParserContext* s);

#ifdef __cplusplus
}
#endif

#endif /* AVCODEC_AVCODEC_H */
