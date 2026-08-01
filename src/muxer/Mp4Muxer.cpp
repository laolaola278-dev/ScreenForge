// ScreenForge Phase 4-A/6-A — MP4 封装器（FFmpeg libavformat）
// fragmented MP4：frag_keyframe + empty_moov + default_base_moof
// 崩溃安全：每个关键帧一个独立 fragment，异常退出后文件仍可修复
// Phase 6-A：AAC 音频流（libavcodec AAC 编码，48kHz stereo，QPC 时间戳）

#include "Mp4Muxer.h"

#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace sf {

namespace {
constexpr AVRational k100ns = { 1, 10000000 };   // 100ns 时间基（与编码器一致）
constexpr int kAacFrameSamples = 1024;           // AAC 帧 = 1024 samples
}

struct Mp4Muxer::Impl {
    AVFormatContext* fmt = nullptr;
    AVStream*        stream = nullptr;
    int64_t          startPts = AV_NOPTS_VALUE;
    uint64_t         packets = 0;
    uint64_t         bytes = 0;
    uint32_t         fragments = 0;
    bool             open = false;

    // Phase 6-A 音频
    bool             audioOpen = false;
    AVStream*        audioStream = nullptr;
    AVCodecContext*  aacCtx = nullptr;
    std::vector<int16_t> pcmBuf;       // 待编码 PCM（交错）
    int64_t          audioPtsSamples = 0;   // 以 1/48000 为单位
    uint64_t         audioPackets = 0;
};

Mp4Muxer::~Mp4Muxer() {
    if (m_impl && m_impl->open) Abort();
}

bool Mp4Muxer::Initialize(const MuxConfig& cfg) {
    m_impl = std::make_unique<Impl>();
    m_lastError.clear();

    // 1) 分配输出上下文（mp4 muxer）
    if (avformat_alloc_output_context2(&m_impl->fmt, nullptr, "mp4",
                                       cfg.outputPath.c_str()) < 0 || !m_impl->fmt) {
        m_lastError = "avformat_alloc_output_context2 失败";
        return false;
    }

    // 2) fragmented MP4 参数
    if (av_opt_set(m_impl->fmt->priv_data, "movflags",
                   "frag_keyframe+empty_moov+default_base_moof", 0) < 0) {
        m_lastError = "设置 movflags 失败";
        return false;
    }

    // 3) 视频流
    AVStream* st = avformat_new_stream(m_impl->fmt, nullptr);
    if (!st) { m_lastError = "avformat_new_stream 失败"; return false; }
    st->id = 0;
    st->time_base = AVRational{ 1, static_cast<int>(cfg.fps) };
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_H264;
    st->codecpar->width      = cfg.width;
    st->codecpar->height     = cfg.height;
    st->codecpar->format     = AV_PIX_FMT_YUV420P;

    if (!cfg.extradata.empty()) {
        st->codecpar->extradata_size = static_cast<int>(cfg.extradata.size());
        st->codecpar->extradata = static_cast<uint8_t*>(
            av_mallocz(cfg.extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!st->codecpar->extradata) { m_lastError = "extradata 分配失败"; return false; }
        std::memcpy(st->codecpar->extradata, cfg.extradata.data(), cfg.extradata.size());
    }

    // 4) 音频流（Phase 6-A：AAC 48kHz stereo）
    if (cfg.audioEnabled) {
        AVStream* ast = avformat_new_stream(m_impl->fmt, nullptr);
        if (!ast) { m_lastError = "avformat_new_stream(audio) 失败"; return false; }
        ast->id = 1;
        ast->time_base = AVRational{ 1, static_cast<int>(cfg.audioSampleRate) };
        ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        ast->codecpar->codec_id   = AV_CODEC_ID_AAC;
        ast->codecpar->sample_rate = static_cast<int>(cfg.audioSampleRate);
        ast->codecpar->ch_layout = AVChannelLayout{};
        av_channel_layout_default(&ast->codecpar->ch_layout, cfg.audioChannels);
        ast->codecpar->format = AV_SAMPLE_FMT_FLTP;
        ast->codecpar->bit_rate = 128000;
        m_impl->audioStream = ast;

        // AAC 编码器
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!codec) { m_lastError = "avcodec_find_encoder(AAC) 失败"; return false; }
        AVCodecContext* c = avcodec_alloc_context3(codec);
        if (!c) { m_lastError = "avcodec_alloc_context3 失败"; return false; }
        c->sample_fmt = AV_SAMPLE_FMT_FLTP;
        c->sample_rate = static_cast<int>(cfg.audioSampleRate);
        c->bit_rate = 128000;
        av_channel_layout_default(&c->ch_layout, cfg.audioChannels);
        if (avcodec_open2(c, codec, nullptr) < 0) {
            m_lastError = "avcodec_open2(AAC) 失败";
            avcodec_free_context(&c);
            return false;
        }
        m_impl->aacCtx = c;
        m_impl->audioOpen = true;
        m_impl->pcmBuf.reserve(1024 * 2 * 8);
    }

    // 5) 打开输出
    if (avio_open(&m_impl->fmt->pb, cfg.outputPath.c_str(), AVIO_FLAG_WRITE) < 0) {
        m_lastError = "avio_open 失败: " + cfg.outputPath;
        return false;
    }

    // 6) 写头（empty_moov）
    if (avformat_write_header(m_impl->fmt, nullptr) < 0) {
        m_lastError = "avformat_write_header 失败";
        return false;
    }

    m_impl->stream = st;
    m_impl->open = true;
    return true;
}

bool Mp4Muxer::WritePacket(const EncodedPacket& pkt) {
    if (!m_impl || !m_impl->open) return false;
    if (!pkt.data || pkt.size == 0) return false;

    AVStream* st = m_impl->stream;
    AVPacket* avp = av_packet_alloc();
    if (!avp) return false;

    if (av_new_packet(avp, static_cast<int>(pkt.size)) < 0) {
        av_packet_free(&avp);
        return false;
    }
    std::memcpy(avp->data, pkt.data, pkt.size);

    const int64_t pts = av_rescale_q(pkt.pts, k100ns, st->time_base);
    const int64_t dts = av_rescale_q(pkt.dts, k100ns, st->time_base);
    if (m_impl->startPts == AV_NOPTS_VALUE) m_impl->startPts = dts;
    avp->pts = pts - m_impl->startPts;
    avp->dts = dts - m_impl->startPts;
    avp->stream_index = 0;
    if (pkt.keyFrame) avp->flags |= AV_PKT_FLAG_KEY;

    if (pkt.keyFrame) m_impl->fragments++;

    const int rc = av_interleaved_write_frame(m_impl->fmt, avp);
    const int64_t size = avp->size;
    av_packet_free(&avp);

    if (rc < 0) {
        m_lastError = "av_interleaved_write_frame 失败";
        return false;
    }
    m_impl->packets++;
    m_impl->bytes += static_cast<uint64_t>(size);
    return true;
}

// 8-A：从 pcmBuf 取一个完整 AAC 帧（1024×ch 样本）编码写入；不足返回 false
bool Mp4Muxer::EncodePcmChunk() {
    Impl& im = *m_impl;
    if (!im.audioOpen || !im.aacCtx) return false;
    const size_t ch = 2;
    const size_t chunk = size_t(kAacFrameSamples) * ch;
    if (im.pcmBuf.size() < chunk) return false;

    // PCM int16 交错 → FLTP 平面
    AVFrame* frame = av_frame_alloc();
    if (!frame) return false;
    frame->nb_samples = kAacFrameSamples;
    frame->format = AV_SAMPLE_FMT_FLTP;
    frame->sample_rate = 48000;
    av_channel_layout_default(&frame->ch_layout, 2);
    if (av_frame_get_buffer(frame, 0) < 0) { av_frame_free(&frame); return false; }

    for (int i = 0; i < kAacFrameSamples; ++i) {
        for (size_t c = 0; c < ch; ++c) {
            reinterpret_cast<float*>(frame->extended_data[c])[i] =
                float(im.pcmBuf[size_t(i) * ch + c]) * (1.0f / 32768.0f);
        }
    }
    im.pcmBuf.erase(im.pcmBuf.begin(), im.pcmBuf.begin() + chunk);

    frame->pts = im.audioPtsSamples;
    im.audioPtsSamples += kAacFrameSamples;

    const int send = avcodec_send_frame(im.aacCtx, frame);
    av_frame_free(&frame);
    if (send < 0) return false;

    AVPacket* avp = av_packet_alloc();
    while (avcodec_receive_packet(im.aacCtx, avp) == 0) {
        avp->stream_index = 1;
        avp->pts = av_rescale_q(avp->pts, AVRational{ 1, 48000 },
                                im.audioStream->time_base);
        avp->dts = avp->pts;
        avp->flags |= AV_PKT_FLAG_KEY;
        const int rc = av_interleaved_write_frame(im.fmt, avp);
        if (rc < 0) {
            m_lastError = "音频包写入失败";
            av_packet_free(&avp);
            return false;
        }
        im.audioPackets++;
        im.bytes += static_cast<uint64_t>(avp->size);
        av_packet_unref(avp);
    }
    av_packet_free(&avp);
    return true;
}

bool Mp4Muxer::WriteAudioFrame(const AudioFrame& af) {
    if (!m_impl || !m_impl->open || !m_impl->audioOpen) return false;
    if (af.samples.empty()) return false;

    Impl& im = *m_impl;
    im.pcmBuf.insert(im.pcmBuf.end(), af.samples.begin(), af.samples.end());
    while (EncodePcmChunk()) { /* 编码全部完整帧 */ }
    return true;
}

bool Mp4Muxer::Finalize() {
    if (!m_impl || !m_impl->open) return false;

    // 冲刷 AAC 编码器：补齐不足一帧的 PCM（8-A 修复：保证最后音频样本完整）
    if (m_impl->audioOpen && m_impl->aacCtx) {
        // 剩余 PCM 不足 1024×2 样本 → 补零到完整 AAC 帧后编码（尾帧不丢失）
        const size_t ch = 2;
        const size_t frameSamples = size_t(kAacFrameSamples) * ch;
        if (!m_impl->pcmBuf.empty()) {
            m_impl->pcmBuf.resize(frameSamples, 0);   // 补零至完整帧
            EncodePcmChunk();                          // 编码尾帧
        }
        // flush 编码器内部缓冲（delay 帧）
        avcodec_send_frame(m_impl->aacCtx, nullptr);
        AVPacket* avp = av_packet_alloc();
        while (avcodec_receive_packet(m_impl->aacCtx, avp) == 0) {
            avp->stream_index = 1;
            avp->pts = av_rescale_q(avp->pts, AVRational{ 1, 48000 },
                                    m_impl->audioStream->time_base);
            avp->dts = avp->pts;
            avp->flags |= AV_PKT_FLAG_KEY;
            if (av_interleaved_write_frame(m_impl->fmt, avp) == 0) {
                m_impl->audioPackets++;
                m_impl->bytes += static_cast<uint64_t>(avp->size);
            }
            av_packet_unref(avp);
        }
        av_packet_free(&avp);
    }

    if (av_write_trailer(m_impl->fmt) < 0) {
        m_lastError = "av_write_trailer 失败";
        return false;
    }
    if (m_impl->fmt->pb) avio_closep(&m_impl->fmt->pb);
    avformat_free_context(m_impl->fmt);
    m_impl->fmt = nullptr;
    m_impl->open = false;
    m_impl->audioOpen = false;
    return true;
}

void Mp4Muxer::Abort() {
    if (!m_impl || !m_impl->open) return;
    if (m_impl->fmt) {
        av_write_trailer(m_impl->fmt);
        if (m_impl->fmt->pb) avio_closep(&m_impl->fmt->pb);
        avformat_free_context(m_impl->fmt);
        m_impl->fmt = nullptr;
    }
    m_impl->open = false;
    m_impl->audioOpen = false;
}

bool Mp4Muxer::IsOpen() const { return m_impl && m_impl->open; }
uint64_t Mp4Muxer::PacketsWritten() const { return m_impl ? m_impl->packets : 0; }
uint64_t Mp4Muxer::BytesWritten() const { return m_impl ? m_impl->bytes : 0; }
uint32_t Mp4Muxer::FragmentsWritten() const { return m_impl ? m_impl->fragments : 0; }

} // namespace sf
