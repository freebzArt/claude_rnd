#include "ptrack/io/movie.h"

#include "ptrack/colour.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <cmath>
#include <cstdio>
#include <regex>
#include <stdexcept>
#include <vector>

namespace ptrack::io {

namespace {

std::string avError(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}

int swsColorspace(AVColorSpace cs) {
    switch (cs) {
    case AVCOL_SPC_BT709: return SWS_CS_ITU709;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL: return SWS_CS_BT2020;
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT470BG: return SWS_CS_ITU601;
    default: return SWS_CS_ITU709; // HD default when the file doesn't say
    }
}

} // namespace

struct MovieReader::Impl {
    std::string path;
    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    AVStream* stream = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* sws = nullptr;
    int swsSrcFormat = -1;
    MovieInfo info;
    int64_t nextIndex = 0;      // index the decoder will produce next, if reading sequentially
    bool eof = false;
    std::vector<uint16_t> rgb;  // RGB48 scratch

    ~Impl() {
        sws_freeContext(sws);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
    }

    int64_t indexOf(int64_t pts) const {
        const int64_t start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        const double seconds = (pts - start) * av_q2d(stream->time_base);
        return static_cast<int64_t>(std::llround(seconds * info.fps));
    }

    int64_t ptsOf(int64_t index) const {
        const int64_t start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        return start + av_rescale_q(index, av_inv_q(stream->avg_frame_rate.num ? stream->avg_frame_rate
                                                                                : stream->r_frame_rate),
                                    stream->time_base);
    }

    // Decode the next frame into `frame`. Returns false at end of stream.
    bool decodeNext() {
        while (true) {
            const int r = avcodec_receive_frame(dec, frame);
            if (r == 0) return true;
            if (r == AVERROR_EOF) return false;
            if (r != AVERROR(EAGAIN)) throw std::runtime_error(path + ": decode error: " + avError(r));
            if (eof) {
                avcodec_send_packet(dec, nullptr); // drain
                eof = false;
                continue;
            }
            int rr;
            while ((rr = av_read_frame(fmt, pkt)) >= 0 && pkt->stream_index != stream->index)
                av_packet_unref(pkt);
            if (rr < 0) {
                eof = true;
                continue;
            }
            const int s = avcodec_send_packet(dec, pkt);
            av_packet_unref(pkt);
            if (s < 0 && s != AVERROR(EAGAIN))
                throw std::runtime_error(path + ": decode error: " + avError(s));
        }
    }

    void seekTo(int64_t index) {
        const int r = av_seek_frame(fmt, stream->index, ptsOf(index), AVSEEK_FLAG_BACKWARD);
        if (r < 0) throw std::runtime_error(path + ": seek failed: " + avError(r));
        avcodec_flush_buffers(dec);
        eof = false;
    }

    Image convert(const std::string& channel) {
        const int w = frame->width, h = frame->height;
        if (!sws || swsSrcFormat != frame->format) {
            sws_freeContext(sws);
            sws = sws_getContext(w, h, static_cast<AVPixelFormat>(frame->format), w, h, AV_PIX_FMT_RGB48,
                                 SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT, nullptr, nullptr,
                                 nullptr);
            if (!sws) throw std::runtime_error(path + ": unsupported pixel format");
            swsSrcFormat = frame->format;
            // Honour the file's matrix and range; always produce full-range RGB.
            const int* coeffs = sws_getCoefficients(swsColorspace(frame->colorspace));
            const int srcRange = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
            sws_setColorspaceDetails(sws, coeffs, srcRange, sws_getCoefficients(SWS_CS_ITU709), 1, 0,
                                     1 << 16, 1 << 16);
        }
        rgb.resize(size_t(w) * h * 3);
        uint8_t* dst[1] = {reinterpret_cast<uint8_t*>(rgb.data())};
        const int dstStride[1] = {w * 6};
        sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dstStride);

        Image out(w, h);
        constexpr float k = 1.0f / 65535.0f;
        const int c = channel == "R" ? 0 : channel == "G" ? 1 : channel == "B" ? 2 : -1;
        for (int y = 0; y < h; ++y) {
            const uint16_t* s = rgb.data() + size_t(y) * w * 3;
            float* d = out.row(y);
            for (int x = 0; x < w; ++x, s += 3)
                d[x] = c >= 0 ? s[c] * k : luma709(s[0] * k, s[1] * k, s[2] * k);
        }
        return out;
    }
};

MovieReader::MovieReader(const std::string& path) : impl_(std::make_unique<Impl>()) {
    Impl& m = *impl_;
    m.path = path;
    int r = avformat_open_input(&m.fmt, path.c_str(), nullptr, nullptr);
    if (r < 0) throw std::runtime_error(path + ": " + avError(r));
    if ((r = avformat_find_stream_info(m.fmt, nullptr)) < 0)
        throw std::runtime_error(path + ": " + avError(r));
    const int si = av_find_best_stream(m.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (si < 0) throw std::runtime_error(path + ": no video stream");
    m.stream = m.fmt->streams[si];
    const AVCodec* codec = avcodec_find_decoder(m.stream->codecpar->codec_id);
    if (!codec) throw std::runtime_error(path + ": no decoder for this codec");
    m.dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m.dec, m.stream->codecpar);
    m.dec->thread_count = 0; // let FFmpeg pick
    if ((r = avcodec_open2(m.dec, codec, nullptr)) < 0) throw std::runtime_error(path + ": " + avError(r));
    m.pkt = av_packet_alloc();
    m.frame = av_frame_alloc();

    MovieInfo& i = m.info;
    i.width = m.stream->codecpar->width;
    i.height = m.stream->codecpar->height;
    const AVRational fr = m.stream->avg_frame_rate.num ? m.stream->avg_frame_rate : m.stream->r_frame_rate;
    i.fps = fr.den ? av_q2d(fr) : 0.0;
    if (i.fps <= 0) throw std::runtime_error(path + ": unknown frame rate");
    if (m.stream->nb_frames > 0) i.frameCount = m.stream->nb_frames;
    else if (m.stream->duration != AV_NOPTS_VALUE)
        i.frameCount = std::llround(m.stream->duration * av_q2d(m.stream->time_base) * i.fps);
    else if (m.fmt->duration != AV_NOPTS_VALUE)
        i.frameCount = std::llround(m.fmt->duration / double(AV_TIME_BASE) * i.fps);
    const AVRational sar = m.stream->codecpar->sample_aspect_ratio;
    i.pixelAspect = (sar.num > 0 && sar.den > 0) ? static_cast<float>(av_q2d(sar)) : 1.0f;
    i.codec = codec->name;
    if (const AVDictionaryEntry* e = av_dict_get(m.stream->metadata, "timecode", nullptr, 0)) i.timecode = e->value;
    else if (const AVDictionaryEntry* f = av_dict_get(m.fmt->metadata, "timecode", nullptr, 0)) i.timecode = f->value;
    i.transfer = av_color_transfer_name(m.stream->codecpar->color_trc);
    i.primaries = av_color_primaries_name(m.stream->codecpar->color_primaries);
    i.fullRange = m.stream->codecpar->color_range == AVCOL_RANGE_JPEG;
}

MovieReader::~MovieReader() = default;

const MovieInfo& MovieReader::info() const { return impl_->info; }

Image MovieReader::readFrame(int64_t index, const std::string& channel) {
    Impl& m = *impl_;
    if (index < 0 || (m.info.frameCount > 0 && index >= m.info.frameCount)) return {};
    if (index != m.nextIndex) m.seekTo(index);
    while (m.decodeNext()) {
        const int64_t pts = m.frame->best_effort_timestamp != AV_NOPTS_VALUE ? m.frame->best_effort_timestamp
                                                                              : m.frame->pts;
        const int64_t got = pts == AV_NOPTS_VALUE ? m.nextIndex : m.indexOf(pts);
        m.nextIndex = got + 1;
        if (got < index) continue; // decoding forward from a keyframe
        if (got > index) {
            // Overshot (frame missing from the stream): report it as unavailable.
            return {};
        }
        return m.convert(channel);
    }
    m.nextIndex = -1;
    return {};
}

bool timecodeToFrame(const std::string& tc, double fps, int& frame) {
    static const std::regex re(R"(^(\d{1,2})[:;](\d{2})[:;](\d{2})[:;.](\d{2,3})$)");
    std::smatch m;
    if (!std::regex_match(tc, m, re) || fps <= 0) return false;
    const int base = static_cast<int>(std::lround(fps));
    frame = ((std::stoi(m[1]) * 60 + std::stoi(m[2])) * 60 + std::stoi(m[3])) * base + std::stoi(m[4]);
    return true;
}

} // namespace ptrack::io
