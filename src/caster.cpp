/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "caster.hpp"

#ifdef USE_DROIDCAM
#include <glib/gtestutils.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gstinfo.h>
#include <gst/gstsample.h>
#endif

#ifdef USE_X11CAPTURE
#include <X11/Xlib.h>
#endif

#ifdef USE_V4L2
#include <dirent.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#endif

#include <fcntl.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <pulse/error.h>
#include <pulse/xmalloc.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/avstring.h>
#include <libavutil/dict.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

#include "fftools.hpp"
#include "logger.hpp"

using namespace std::literals;

static inline int64_t rescaleToUsec(int64_t time, AVRational srcTimeBase) {
    return av_rescale_q(time, srcTimeBase, AVRational{1, 1000000});
}

static inline int64_t rescaleFromUsec(int64_t time, AVRational destTimeBase) {
    return av_rescale_q(time, AVRational{1, 1000000}, destTimeBase);
}

static bool nearlyEqual(float a, float b) {
    return std::nextafter(a, std::numeric_limits<float>::lowest()) <= b &&
           std::nextafter(a, std::numeric_limits<float>::max()) >= b;
}

static void dataToHexStr(std::ostream &os, const uint8_t *data, int size) {
    os << std::hex << std::setfill('0');
    for (int i = 0; i < size; ++i)
        os << ' ' << std::setw(2) << static_cast<int>(data[i]);
}

[[maybe_unused]] static std::string dataToStr(const uint8_t *data, int size) {
    std::stringstream ss;
    dataToHexStr(ss, data, std::min(50, size));
    return ss.str();
}

[[maybe_unused]] static std::ostream &operator<<(std::ostream &os,
                                                 AVRational r) {
    os << r.num << "/" << r.den;
    return os;
}

[[maybe_unused]] static std::ostream &operator<<(std::ostream &os,
                                                 AVPixelFormat fmt) {
    const auto *name = av_get_pix_fmt_name(fmt);
    if (name == nullptr)
        os << "unknown";
    else
        os << name;
    return os;
}

[[maybe_unused]] static std::ostream &operator<<(std::ostream &os,
                                                 AVSampleFormat fmt) {
    const auto *name = av_get_sample_fmt_name(fmt);
    if (name == nullptr)
        os << "unknown";
    else
        os << name;
    return os;
}

[[maybe_unused]] static std::ostream &operator<<(std::ostream &os,
                                                 const AVPixelFormat *fmts) {
    for (int i = 0;; ++i) {
        if (fmts[i] == AV_PIX_FMT_NONE) break;
        if (i != 0) os << ", ";
        os << fmts[i];
    }

    return os;
}

static auto avCodecName(AVCodecID codec) {
    const auto *desc = avcodec_descriptor_get(codec);
    if (desc == nullptr) return "unknown";
    return desc->name;
}

[[maybe_unused]] static std::ostream &operator<<(std::ostream &os,
                                                 AVCodecID codec) {
    os << avCodecName(codec);
    return os;
}

[[maybe_unused]] static std::ostream &operator<<(std::ostream &os,
                                                 const AVPacket *pkt) {
    os << "pts=" << pkt->pts << ", dts=" << pkt->dts
       << ", duration=" << pkt->duration << ", pos=" << pkt->pos
       << ", sidx=" << pkt->stream_index << ", tb=" << pkt->time_base
       << ", size=" << pkt->size << ", data=";
    dataToHexStr(os, pkt->data, std::min(50, pkt->size));
    return os;
}

// static void logAvDevices() {
//     const AVInputFormat *d = nullptr;
//     while (true) {
//         d = av_input_video_device_next(d);
//         if (d == nullptr) break;
//         LOGD("av device: " << d->name << " (" << d->long_name << ")");

//        AVDeviceInfoList *device_list = nullptr;
//        if (avdevice_list_input_sources(d, nullptr, nullptr, &device_list) >
//                0 &&
//            device_list != nullptr) {
//            LOGD(" default source: " << device_list->default_device);
//            for (int i = 0; i < device_list->nb_devices; ++i) {
//                LOGD(" source["
//                     << i << "]: " << device_list->devices[i]->device_name
//                     << " " << device_list->devices[i]->device_description);
//            }
//            avdevice_free_list_devices(&device_list);
//        }
//    }
//}

std::ostream &operator<<(std::ostream &os, Caster::State state) {
    switch (state) {
        case Caster::State::Initing:
            os << "initing";
            break;
        case Caster::State::Inited:
            os << "inited";
            break;
        case Caster::State::Starting:
            os << "starting";
            break;
        case Caster::State::Started:
            os << "started";
            break;
        case Caster::State::Terminating:
            os << "terminating";
            break;
    }

    return os;
}

std::ostream &operator<<(std::ostream &os,
                         Caster::VideoOrientation videoOrientation) {
    switch (videoOrientation) {
        case Caster::VideoOrientation::Auto:
            os << "auto";
            break;
        case Caster::VideoOrientation::Landscape:
            os << "landscape";
            break;
        case Caster::VideoOrientation::Portrait:
            os << "portrait";
            break;
        case Caster::VideoOrientation::InvertedLandscape:
            os << "inverted-landscape";
            break;
        case Caster::VideoOrientation::InvertedPortrait:
            os << "inverted-portrait";
            break;
        default:
            os << "unknown";
    }

    return os;
}

std::ostream &operator<<(std::ostream &os, Caster::StreamFormat streamFormat) {
    switch (streamFormat) {
        case Caster::StreamFormat::Mp4:
            os << "mp4";
            break;
        case Caster::StreamFormat::MpegTs:
            os << "mpegts";
            break;
        case Caster::StreamFormat::Mp3:
            os << "mp3";
            break;
        default:
            os << "unknown";
    }

    return os;
}

std::ostream &operator<<(std::ostream &os, Caster::SensorDirection direction) {
    switch (direction) {
        case Caster::SensorDirection::Back:
            os << "back";
            break;
        case Caster::SensorDirection::Front:
            os << "front";
            break;
        default:
            os << "unknown";
    }

    return os;
}

std::ostream &operator<<(std::ostream &os, Caster::VideoEncoder encoder) {
    switch (encoder) {
        case Caster::VideoEncoder::Auto:
            os << "auto";
            break;
        case Caster::VideoEncoder::X264:
            os << "x264";
            break;
        case Caster::VideoEncoder::Nvenc:
            os << "nvenc";
            break;
        case Caster::VideoEncoder::V4l2:
            os << "v4l2";
            break;
        default:
            os << "unknown";
    }

    return os;
}

std::ostream &operator<<(std::ostream &os, Caster::Endianness endian) {
    switch (endian) {
        case Caster::Endianness::Be:
            os << "be";
            break;
        case Caster::Endianness::Le:
            os << "le";
            break;
        default:
            os << "unknown";
    }

    return os;
}

std::ostream &operator<<(std::ostream &os, Caster::VideoTrans type) {
    switch (type) {
        case Caster::VideoTrans::Off:
            os << "off";
            break;
        case Caster::VideoTrans::Scale:
            os << "scale";
            break;
        case Caster::VideoTrans::Vflip:
            os << "vflip";
            break;
        case Caster::VideoTrans::Frame169:
            os << "frame-169";
            break;
        case Caster::VideoTrans::Frame169Rot90:
            os << "frame-169-rot-90";
            break;
        case Caster::VideoTrans::Frame169Rot180:
            os << "frame-169-rot-180";
            break;
        case Caster::VideoTrans::Frame169Rot270:
            os << "frame-169-rot-270";
            break;
        case Caster::VideoTrans::Frame169Vflip:
            os << "frame-169-vflip";
            break;
        case Caster::VideoTrans::Frame169VflipRot90:
            os << "frame-169-vflip-rot-90";
            break;
        case Caster::VideoTrans::Frame169VflipRot180:
            os << "frame-169-vflip-rot-180";
            break;
        case Caster::VideoTrans::Frame169VflipRot270:
            os << "frame-169-vflip-rot-270";
            break;
        default:
            os << "unknown";
    }

    return os;
}

std::ostream &operator<<(std::ostream &os, Caster::VideoScale scale) {
    switch (scale) {
        case Caster::VideoScale::Off:
            os << "off";
            break;
        case Caster::VideoScale::Down25:
            os << "down-25%";
            break;
        case Caster::VideoScale::Down50:
            os << "down-50%";
            break;
        case Caster::VideoScale::Down75:
            os << "down-75%";
            break;
        default:
            os << "unknown";
    }

    return os;
}

std::ostream &operator<<(std::ostream &os, Caster::VideoSourceType type) {
    switch (type) {
        case Caster::VideoSourceType::DroidCam:
            os << "droidcam";
            break;
        case Caster::VideoSourceType::V4l2:
            os << "v4l2";
            break;
        case Caster::VideoSourceType::X11Capture:
            os << "x11-capture";
            break;
        case Caster::VideoSourceType::LipstickCapture:
            os << "lipstick-capture";
            break;
        case Caster::VideoSourceType::Test:
            os << "test";
            break;
        default:
            os << "unknown";
    }

    return os;
}

std::ostream &operator<<(std::ostream &os, Caster::AudioSourceType type) {
    switch (type) {
        case Caster::AudioSourceType::Mic:
            os << "mic";
            break;
        case Caster::AudioSourceType::Monitor:
            os << "monitor";
            break;
        case Caster::AudioSourceType::Playback:
            os << "playback";
            break;
        default:
            os << "unknown";
    }

    return os;
}

std::ostream &operator<<(std::ostream &os, Caster::Dim dim) {
    os << dim.width << "x" << dim.height;
    return os;
}

std::ostream &operator<<(std::ostream &os, Caster::VideoFormat format) {
    os << "codec=" << format.codecId << ", pixfmt=" << format.pixfmt;
    return os;
}

std::ostream &operator<<(std::ostream &os,
                         const Caster::VideoFormatExt &format) {
    os << "codec=" << format.codecId << ", pixfmt=" << format.pixfmt << ": ";
    for (const auto &s : format.frameSpecs)
        os << fmt::format("(size={}x{}, fr=[{}]), ", s.dim.width, s.dim.height,
                          fmt::join(s.framerates, ","));
    return os;
}

std::ostream &operator<<(std::ostream &os,
                         const Caster::VideoSourceInternalProps &props) {
    os << "type=" << props.type << ", name=" << props.name
       << ", fname=" << props.friendlyName << ", dev=" << props.dev
       << ", orientation=" << props.orientation
       << ", sensor-direction=" << props.sensorDirection
       << ", trans=" << props.trans << ", scale=" << props.scale
       << ", formats=(";
    if (!props.formats.empty())
        for (const auto &f : props.formats) os << "[" << f << "], ";
    os << ")";

    return os;
}

std::ostream &operator<<(std::ostream &os,
                         const Caster::V4l2H264EncoderProps &props) {
    os << "dev=" << props.dev << ", formats=(";
    if (!props.formats.empty())
        for (const auto &f : props.formats) os << "[" << f << "], ";
    os << ")";
    return os;
}

std::ostream &operator<<(std::ostream &os,
                         const Caster::AudioSourceInternalProps &props) {
    os << "type=" << props.type << ", name=" << props.name
       << ", fname=" << props.friendlyName << ", dev=" << props.dev
       << ", codec=" << props.codec
       << ", channels=" << static_cast<int>(props.channels)
       << ", rate=" << props.rate << ", bps=" << props.bps
       << ", endian=" << props.endian;
    return os;
}

std::ostream &operator<<(std::ostream &os, const Caster::PaClient &client) {
    os << "idx=" << client.idx << ", name=" << client.name
       << ", bin=" << client.bin;
    return os;
}

std::ostream &operator<<(std::ostream &os, const Caster::PaSinkInput &input) {
    os << "idx=" << input.idx << ", name=" << input.name
       << ", client idx=" << input.clientIdx << ", sink idx=" << input.sinkIdx
       << ", corked=" << input.corked << ", muted=" << input.muted
       << ", removed=" << input.removed;
    return os;
}

std::ostream &operator<<(std::ostream &os, const Caster::Config &config) {
    os << "stream-format=" << config.streamFormat << ", video-source="
       << (config.videoSource.empty() ? "off" : config.videoSource)
       << ", audio-source="
       << (config.audioSource.empty() ? "off" : config.audioSource)
       << ", video-orientation=" << config.videoOrientation
       << ", audio-volume=" << std::to_string(config.audioVolume)
       << ", stream-author=" << config.streamAuthor
       << ", stream-title=" << config.streamTitle
       << ", video-encoder=" << config.videoEncoder;
    return os;
}

bool Caster::configValid(const Config &config) const {
    if (!config.videoSource.empty() &&
        m_videoProps.count(config.videoSource) == 0) {
        LOGW("video-source is invalid");
        return false;
    }

    if (!config.audioSource.empty() &&
        m_audioProps.count(config.audioSource) == 0) {
        LOGW("audio-source is invalid");
        return false;
    }

    if (config.videoSource.empty() && config.audioSource.empty()) {
        LOGW("both video-source and audio-source cannot be empty");
        return false;
    }

    if (config.videoOrientation != VideoOrientation::Auto &&
        config.videoOrientation != VideoOrientation::Landscape &&
        config.videoOrientation != VideoOrientation::InvertedLandscape &&
        config.videoOrientation != VideoOrientation::Portrait &&
        config.videoOrientation != VideoOrientation::InvertedPortrait) {
        LOGW("video-orientation is invalid");
        return false;
    }

    if (config.videoEncoder != VideoEncoder::Auto &&
        config.videoEncoder != VideoEncoder::Nvenc &&
        config.videoEncoder != VideoEncoder::V4l2 &&
        config.videoEncoder != VideoEncoder::X264) {
        LOGW("video-encoder is invalid");
        return false;
    }

    if (config.streamFormat != StreamFormat::Mp4 &&
        config.streamFormat != StreamFormat::MpegTs &&
        config.streamFormat != StreamFormat::Mp3) {
        LOGW("stream-format is invalid");
        return false;
    }

    if (config.streamFormat == StreamFormat::Mp3 &&
        !config.videoSource.empty()) {
        LOGW("stream-format does not support video");
        return false;
    }

    if (config.audioVolume < 0.F || config.audioVolume > 10.F) {
        LOGW("audio-volume is invalid");
        return false;
    }

    if (config.streamAuthor.empty()) {
        LOGW("stream-author is invalid");
        return false;
    }

    if (config.streamTitle.empty()) {
        LOGW("stream-title is invalid");
        return false;
    }

    return true;
}

Caster::Caster(Config config, DataReadyHandler dataReadyHandler,
               StateChangedHandler stateChangedHandler)
    : m_config{std::move(config)},
      m_dataReadyHandler{std::move(dataReadyHandler)},
      m_stateChangedHandler{std::move(stateChangedHandler)} {
    LOGD("creating caster, config: " << m_config);

    try {
        detectSources();
#ifdef USE_V4L2
        detectV4l2Encoders();
#endif

        if (!configValid(m_config))
            throw std::runtime_error("invalid configuration");

        LOGD("audio enabled: " << audioEnabled());
        LOGD("video enabled: " << videoEnabled());

        if (audioEnabled()) initPa();
        if (videoEnabled()) {
            auto vtype = videoProps().type;
            if (vtype == VideoSourceType::Test)
                m_imageProvider.emplace(
                    [this](const uint8_t *data, size_t size) {
                        rawDataReadyCallback(data, size);
                    });
#ifdef USE_LIPSTICK_RECORDER
            if (vtype == VideoSourceType::LipstickCapture)
                m_lipstickRecorder.emplace(
                    [this](const uint8_t *data, size_t size) {
                        rawDataReadyCallback(data, size);
                    },
                    [this] {
                        LOGE("error in lipstick-recorder");
                        reportError();
                    });
#endif
#ifdef USE_DROIDCAM
            if (vtype == VideoSourceType::DroidCam) initGst();
#endif
        }
        initAv();
    } catch (...) {
        clean();
        throw;
    }
}

Caster::~Caster() {
    LOGD("caster termination started");
    setState(State::Terminating);
    m_videoCv.notify_all();
    clean();
    LOGD("caster termination completed");
}

void Caster::reportError() {
    setState(State::Terminating);
    m_videoCv.notify_all();
}

template <typename Dev>
static bool sortByName(const Dev &rhs, const Dev &lhs) {
    return lhs.name > rhs.name;
}

std::vector<Caster::VideoSourceProps> Caster::videoSources() {
    decltype(videoSources()) sources;

    auto props = detectVideoSources();
    sources.reserve(props.size());

    std::transform(
        props.begin(), props.end(), std::back_inserter(sources), [](auto &p) {
            return VideoSourceProps{std::move(p.second.name),
                                    std::move(p.second.friendlyName)};
        });

    std::sort(sources.begin(), sources.end(), sortByName<VideoSourceProps>);

    return sources;
}

std::vector<Caster::AudioSourceProps> Caster::audioSources() {
    decltype(audioSources()) sources;

    auto props = detectPaSources();
    sources.reserve(props.size());

    std::transform(
        props.begin(), props.end(), std::back_inserter(sources),
        [](auto &p) -> AudioSourceProps {
            return {std::move(p.second.name), std::move(p.second.friendlyName)};
        });

    std::sort(sources.begin(), sources.end(), sortByName<AudioSourceProps>);

    return sources;
}

void Caster::detectSources() {
    m_audioProps = detectAudioSources();
    m_videoProps = detectVideoSources();
}

Caster::AudioPropsMap Caster::detectAudioSources() { return detectPaSources(); }

void Caster::paSourceInfoCallback([[maybe_unused]] pa_context *ctx,
                                  const pa_source_info *info, int eol,
                                  void *userdata) {
    auto *result = static_cast<AudioSourceSearchResult *>(userdata);
    if (eol) {  // done
        result->done = true;
        return;
    }

    if (info->monitor_of_sink == PA_INVALID_INDEX &&
        info->active_port == nullptr)
        return;  // ignoring not-monitor without active
                 // ports

#ifdef USE_SFOS
    if (info->monitor_of_sink != PA_INVALID_INDEX) {
        LOGD("ignoring pa monitor on sfos: " << info->name);
        return;
    }

    if (strcmp("source.primary_input", info->name) != 0 &&
        strcmp("source.droid", info->name) != 0) {
        LOGD("ignoring pa source on sfos: " << info->name);
        return;
    }
#endif

    Caster::AudioSourceInternalProps props{
#ifdef USE_SFOS
        /*name=*/"mic",
        /*dev=*/info->name,
        /*friendlyName=*/"Microphone",
#else
        /*name=*/info->monitor_of_sink == PA_INVALID_INDEX
            ? fmt::format("mic-{:03}", hash(info->name))
            : fmt::format("monitor-{:03}", hash(info->name)),
        /*dev=*/info->name,
        /*friendlyName=*/info->description,
#endif
        /*codec=*/
        ff_tools::ff_pulse_format_to_codec_id(info->sample_spec.format),
        /*channels=*/info->sample_spec.channels,
        /*rate=*/info->sample_spec.rate,
        /*bps=*/
        pa_sample_size(&info->sample_spec),
        /*endian=*/
        pa_sample_format_is_be(info->sample_spec.format) == 1 ? Endianness::Be
                                                              : Endianness::Le,
        /*type=*/info->monitor_of_sink == PA_INVALID_INDEX
            ? AudioSourceType::Mic
            : AudioSourceType::Monitor,
        /*muteSource=*/false};

    if (props.codec == AV_CODEC_ID_NONE) {
        LOGW("invalid codec: " << props.dev);
        return;
    }

    LOGD("pa source found: " << props);

    result->propsMap.try_emplace(props.name, std::move(props));
}

std::optional<std::reference_wrapper<Caster::PaSinkInput>>
Caster::bestPaSinkInput() {
    if (m_paSinkInputs.count(m_connectedPaSinkInput) > 0) {
        auto &si = m_paSinkInputs.at(m_connectedPaSinkInput);
        if (!si.removed && !si.corked) {
            LOGD("best pa sink input is current sink input");
            return si;
        }
    }

    for (auto it = m_paSinkInputs.begin(); it != m_paSinkInputs.end(); ++it) {
        if (!it->second.removed && !it->second.corked) return it->second;
    }

    return std::nullopt;
}

Caster::AudioPropsMap Caster::detectPaSources() {
    LOGD("pa sources detection started");

    auto *loop = pa_mainloop_new();
    if (loop == nullptr) throw std::runtime_error("pa_mainloop_new error");

    auto *mla = pa_mainloop_get_api(loop);

    auto *ctx = pa_context_new(mla, "caster");
    if (ctx == nullptr) {
        pa_mainloop_free(loop);
        throw std::runtime_error("pa_context_new error");
    }

    AudioSourceSearchResult result;

    pa_context_set_state_callback(
        ctx,
        [](pa_context *ctx, void *userdata) {
            if (pa_context_get_state(ctx) == PA_CONTEXT_READY) {
                pa_operation_unref(pa_context_get_source_info_list(
                    ctx, paSourceInfoCallback, userdata));
            }
        },
        &result);

    if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        auto err = pa_context_errno(ctx);
        pa_context_unref(ctx);
        pa_mainloop_free(loop);
        throw std::runtime_error("pa_context_connect error: "s +
                                 pa_strerror(err));
    }

    while (true) {
        if (result.done || pa_mainloop_iterate(loop, 0, nullptr) < 0) break;
    }

    {
        Caster::AudioSourceInternalProps props{
            /*name=*/"playback",
            /*dev=*/{},
            /*friendlyName=*/"Playback capture",
            /*codec=*/AV_CODEC_ID_PCM_S16LE,
            /*channels=*/2,
            /*rate=*/44100,
            /*bps=*/2,
            /*endian=*/Endianness::Le,
            /*type=*/AudioSourceType::Playback,
            /*muteSource=*/false};
        LOGD("pa source found: " << props);
        result.propsMap.try_emplace(props.name, std::move(props));
    }

#ifdef USE_SFOS
    {
        Caster::AudioSourceInternalProps props{
            /*name=*/"playback-mute",
            /*dev=*/{},
            /*friendlyName=*/"Playback capture, mute source",
            /*codec=*/AV_CODEC_ID_PCM_S16LE,
            /*channels=*/2,
            /*rate=*/44100,
            /*bps=*/2,
            /*endian=*/Endianness::Le,
            /*type=*/AudioSourceType::Playback,
            /*muteSource=*/true};
        LOGD("pa source found: " << props);
        result.propsMap.try_emplace(props.name, std::move(props));
    }
#endif

    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(loop);

    LOGD("pa sources detection completed");

    return std::move(result.propsMap);
}

bool Caster::audioMuted() const {
    return nearlyEqual(m_config.audioVolume, 0.F);
}

bool Caster::audioBoosted() const {
    return !nearlyEqual(m_config.audioVolume, 1.F);
}

void Caster::setState(State newState) {
    if (m_state != newState) {
        LOGD("changing state: " << m_state << " => " << newState);
        m_state = newState;
        if (m_stateChangedHandler) m_stateChangedHandler(newState);
    }
}

void Caster::start() {
    if (m_state != State::Inited) {
        LOGW("start is only possible in inited state");
        return;
    }

    setState(State::Starting);

    try {
        if (videoEnabled()) {
            const auto &vprops = videoProps();
            if (vprops.type == VideoSourceType::Test) m_imageProvider->start();
#ifdef USE_LIPSTICK_RECORDER
            if (vprops.type == VideoSourceType::LipstickCapture)
                m_lipstickRecorder->start();
#endif
#ifdef USE_DROIDCAM
            if (vprops.type == VideoSourceType::DroidCam) startGst();
#endif
        }

        startAv();

        if (audioEnabled() && !audioMuted()) startPa();

        startMuxing();

        setState(State::Started);
    } catch (const std::runtime_error &e) {
        LOGW("failed to start: " << e.what());
        reportError();
    }
}

void Caster::clean() {
#ifdef USE_DROIDCAM
    if (m_gstThread.joinable()) m_gstThread.join();
    LOGD("gst thread joined");
#endif
    if (m_avMuxingThread.joinable()) m_avMuxingThread.join();
    LOGD("av muxing thread joined");
    if (m_audioPaThread.joinable()) m_audioPaThread.join();
    LOGD("pa thread joined");
    cleanPa();
    LOGD("pa cleaned");
    cleanAv();
    LOGD("av cleaned");
#ifdef USE_DROIDCAM
    cleanGst();
    LOGD("gst cleaned");
#endif
}

std::string Caster::strForAvOpts(const AVDictionary *opts) {
    if (opts == nullptr) return {};

    std::ostringstream os;

    AVDictionaryEntry *t = nullptr;
    while ((t = av_dict_get(opts, "", t, AV_DICT_IGNORE_SUFFIX))) {
        os << "[" << t->key << "=" << t->value << "],";
    }

    return os.str();
}

void Caster::cleanAv() {
    for (auto p : m_videoFilterCtxMap) {
        if (p.second.in != nullptr) avfilter_inout_free(&p.second.in);
        if (p.second.out != nullptr) avfilter_inout_free(&p.second.out);
        if (p.second.graph != nullptr) avfilter_graph_free(&p.second.graph);
    }

    if (m_audioFrameIn != nullptr) av_frame_free(&m_audioFrameIn);
    if (m_audioFrameOut != nullptr) av_frame_free(&m_audioFrameOut);
    if (m_videoFrameIn != nullptr) av_frame_free(&m_videoFrameIn);
    if (m_videoFrameAfterSws != nullptr) av_frame_free(&m_videoFrameAfterSws);
    if (m_videoFrameAfterFilter != nullptr)
        av_frame_free(&m_videoFrameAfterFilter);

    if (m_outFormatCtx != nullptr) {
        if (m_outFormatCtx->pb != nullptr) {
            if (m_outFormatCtx->pb->buffer != nullptr)
                av_freep(&m_outFormatCtx->pb->buffer);
            avio_context_free(&m_outFormatCtx->pb);
        }
        avformat_free_context(m_outFormatCtx);
        m_outFormatCtx = nullptr;
    }
    if (m_inVideoFormatCtx != nullptr) {
        if (m_inVideoFormatCtx->pb != nullptr) {
            if (m_inVideoFormatCtx->pb->buffer != nullptr)
                av_freep(&m_inVideoFormatCtx->pb->buffer);
            avio_context_free(&m_inVideoFormatCtx->pb);
        }
        avformat_close_input(&m_inVideoFormatCtx);
    }
    if (m_keyVideoPkt != nullptr) av_packet_free(&m_keyVideoPkt);
    if (m_keyAudioPkt != nullptr) av_packet_free(&m_keyAudioPkt);
    if (m_audioSwrCtx != nullptr) swr_free(&m_audioSwrCtx);
    if (m_outAudioCtx != nullptr) avcodec_free_context(&m_outAudioCtx);
    if (m_inAudioCtx != nullptr) avcodec_free_context(&m_inAudioCtx);
    if (m_outVideoCtx != nullptr) avcodec_free_context(&m_outVideoCtx);
    if (m_inVideoCtx != nullptr) avcodec_free_context(&m_inVideoCtx);
    if (m_videoSwsBuf != nullptr) av_freep(&m_videoSwsBuf);
    if (m_videoSwsCtx != nullptr) {
        sws_freeContext(m_videoSwsCtx);
        m_videoSwsCtx = nullptr;
    }

    m_outAudioStream = nullptr;
    m_outVideoStream = nullptr;
}

void Caster::unmuteAllPaSinkInputs() {
    for (auto it = m_paSinkInputs.begin(); it != m_paSinkInputs.end(); ++it) {
        if (it->second.muted) unmutePaSinkInput(it->second);
    }

    while (true) {
        auto ret = pa_mainloop_iterate(m_paLoop, 0, nullptr);
        if (ret <= 0) break;
    }
}

void Caster::cleanPa() {
    if (m_paCtx != nullptr) {
        if (m_paStream != nullptr) {
            pa_stream_disconnect(m_paStream);
            pa_stream_unref(m_paStream);
            m_paStream = nullptr;
        }

        unmuteAllPaSinkInputs();

        pa_context_unref(m_paCtx);
        m_paCtx = nullptr;
    }

    if (m_paLoop != nullptr) {
        pa_mainloop_free(m_paLoop);
        m_paLoop = nullptr;
    }
}

void Caster::cleanAvOpts(AVDictionary **opts) {
    if (*opts != nullptr) {
        LOGW("rejected av options: " << strForAvOpts(*opts));
        av_dict_free(opts);
    }
}

void Caster::paSubscriptionCallback(pa_context *ctx,
                                    pa_subscription_event_type_t t,
                                    uint32_t idx, void *userdata) {
    auto *caster = static_cast<Caster *>(userdata);

    if (caster->terminating()) return;

    auto facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    auto type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    switch (facility) {
        case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
            if (type == PA_SUBSCRIPTION_EVENT_NEW ||
                type == PA_SUBSCRIPTION_EVENT_CHANGE) {
                if (type == PA_SUBSCRIPTION_EVENT_NEW)
                    LOGD("pa sink input created: " << idx);
                else
                    LOGD("pa sink input changed: " << idx);
                pa_operation_unref(pa_context_get_sink_input_info(
                    ctx, idx, paSinkInputInfoCallback, userdata));
            } else if (type == PA_SUBSCRIPTION_EVENT_REMOVE)
                if (caster->m_paSinkInputs.count(idx) > 0) {
                    LOGD("pa sink input removed: " << idx);
                    if (caster->m_paSinkInputs.count(idx))
                        caster->m_paSinkInputs.at(idx).removed = true;
                    caster->reconnectPaSinkInput();
                }
            break;
        case PA_SUBSCRIPTION_EVENT_CLIENT:
            if (type == PA_SUBSCRIPTION_EVENT_NEW ||
                type == PA_SUBSCRIPTION_EVENT_CHANGE) {
                if (type == PA_SUBSCRIPTION_EVENT_NEW)
                    LOGD("pa client created: " << idx);
                else
                    LOGD("pa client changed: " << idx);
                pa_operation_unref(pa_context_get_client_info(
                    ctx, idx, paClientInfoCallback, userdata));
            } else if (type == PA_SUBSCRIPTION_EVENT_REMOVE)
                if (caster->m_paClients.count(idx) > 0) {
                    LOGD("pa client removed: " << idx);
                    caster->m_paClients.erase(idx);
                }
            break;
    }
}

bool Caster::paClientShouldBeIgnored(
    [[maybe_unused]] const pa_client_info *info) {
    bool me = [&] {
        const auto *cpid =
            pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_PROCESS_ID);
        if (cpid == nullptr) return true;
        static auto pid = ::getpid();
        return strtoimax(cpid, nullptr, 10) == pid;
    }();

    if (me) return true;

#ifdef USE_SFOS
    if (!strcmp(info->name, "ngfd") || !strcmp(info->name, "feedback-event") ||
        !strcmp(info->name, "keyboard_0") ||
        !strcmp(info->name, "keyboard_1") ||
        !strcmp(info->name, "ngf-tonegen-plugin") ||
        !strcmp(info->name, "jolla keyboard")) {
        return true;
    }
#endif
    if (!strcmp(info->name, "speech-dispatcher")) return true;

    return false;
}

void Caster::paClientInfoCallback([[maybe_unused]] pa_context *ctx,
                                  const pa_client_info *info, int eol,
                                  void *userdata) {
    if (eol || paClientShouldBeIgnored(info)) return;

    auto *caster = static_cast<Caster *>(userdata);

    auto &client = caster->m_paClients[info->index];

    client.idx = info->index;
    client.name = info->name;

    if (const auto *binary = pa_proplist_gets(
            info->proplist, PA_PROP_APPLICATION_PROCESS_BINARY)) {
        client.bin = binary;
    }

    LOGD("pa client: " << client);

    //    auto *props = pa_proplist_to_string(info->proplist);
    //    LOGD(" props:\n" << props);
    //    pa_xfree(props);
}

void Caster::paSinkInputInfoCallback([[maybe_unused]] pa_context *ctx,
                                     const pa_sink_input_info *info, int eol,
                                     void *userdata) {
    if (eol) return;

    auto *caster = static_cast<Caster *>(userdata);

    if (caster->m_paClients.count(info->client) == 0) return;

    auto &input = caster->m_paSinkInputs[info->index];

    input.idx = info->index;
    input.name = info->name;
    input.clientIdx = info->client;
    input.corked = info->corked != 0;
    if (!input.muted) input.sinkIdx = info->sink;

    LOGD("pa sink input: " << input);

    caster->reconnectPaSinkInput();
}

void Caster::reconnectPaSinkInput() {
    if (!m_audioPaThread.joinable()) return;  // muxing not started yet
    if (audioProps().type != AudioSourceType::Playback) return;
    connectPaSinkInput();
}

void Caster::paStateCallback(pa_context *ctx, [[maybe_unused]] void *userdata) {
    auto *caster = static_cast<Caster *>(userdata);

    if (caster->terminating()) return;

    switch (pa_context_get_state(ctx)) {
        case PA_CONTEXT_CONNECTING:
            LOGD("pa connecting");
            break;
        case PA_CONTEXT_AUTHORIZING:
            LOGD("pa authorizing");
            break;
        case PA_CONTEXT_SETTING_NAME:
            LOGD("pa setting name");
            break;
        case PA_CONTEXT_READY:
            LOGD("pa ready");
            break;
        case PA_CONTEXT_TERMINATED:
            LOGD("pa terminated");
            break;
        case PA_CONTEXT_FAILED:
            LOGD("pa failed");
            throw std::runtime_error("pa failed");
        default:
            LOGD("pa unknown state");
    }
}

void Caster::initPa() {
    LOGD("pa init started");

    m_paLoop = pa_mainloop_new();
    if (m_paLoop == nullptr) throw std::runtime_error("pa_mainloop_new error");

    auto *mla = pa_mainloop_get_api(m_paLoop);

    m_paCtx = pa_context_new(mla, m_config.streamAuthor.c_str());
    if (m_paCtx == nullptr) throw std::runtime_error("pa_context_new error");

    if (pa_context_connect(m_paCtx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        throw std::runtime_error("pa_context_connect error: "s +
                                 pa_strerror(pa_context_errno(m_paCtx)));
    }

    pa_context_set_state_callback(m_paCtx, paStateCallback, this);

    while(true) {
        auto ret = pa_mainloop_iterate(m_paLoop, 0, nullptr);
        auto state = pa_context_get_state(m_paCtx);
        if (ret < 0 || state == PA_CONTEXT_FAILED ||
            state == PA_CONTEXT_TERMINATED)
            throw std::runtime_error("pa error");
        if (state == PA_CONTEXT_READY) break;
    }

    if (audioProps().type == AudioSourceType::Playback) {
        pa_context_set_subscribe_callback(m_paCtx, paSubscriptionCallback,
                                          this);
        auto mask = static_cast<pa_subscription_mask_t>(
            PA_SUBSCRIPTION_MASK_SINK_INPUT | PA_SUBSCRIPTION_MASK_CLIENT);

        auto *op = pa_context_subscribe(
            m_paCtx, mask,
            [](pa_context *ctx, int success, void *userdata) {
                if (success) {
                    pa_operation_unref(pa_context_get_client_info_list(
                        ctx, paClientInfoCallback, userdata));
                    pa_operation_unref(pa_context_get_sink_input_info_list(
                        ctx, paSinkInputInfoCallback, userdata));
                }
            },
            this);
        if (op == nullptr)
            throw std::runtime_error("pa_context_subscribe error");
        pa_operation_unref(op);
    }

    LOGD("pa init completed");
}

void Caster::initAvAudioDecoder() {
    LOGD("initing audio decoder");

    const auto &props = audioProps();

    const auto *decoder = avcodec_find_decoder(props.codec);
    if (decoder == nullptr)
        throw std::runtime_error("avcodec_find_decoder for audio error");

    if (decoder->sample_fmts == nullptr ||
        decoder->sample_fmts[0] == AV_SAMPLE_FMT_NONE)
        throw std::runtime_error(
            "audio decoder does not support any sample fmts");

    LOGD("sample fmts supported by audio decoder:");
    for (int i = 0; decoder->sample_fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
        LOGD("[" << i << "]: " << decoder->sample_fmts[i]);
    }

    m_inAudioCtx = avcodec_alloc_context3(decoder);
    if (m_inAudioCtx == nullptr)
        throw std::runtime_error("avcodec_alloc_context3 for in audio error");

    av_channel_layout_default(&m_inAudioCtx->ch_layout, props.channels);
    m_inAudioCtx->sample_rate = static_cast<int>(props.rate);
    m_inAudioCtx->sample_fmt = decoder->sample_fmts[0];
    m_inAudioCtx->time_base = AVRational{1, m_inAudioCtx->sample_rate};

    if (avcodec_open2(m_inAudioCtx, nullptr, nullptr) != 0)
        throw std::runtime_error("avcodec_open2 for in audio error");

    m_audioFrameIn = av_frame_alloc();
    if (m_audioFrameIn == nullptr)
        throw std::runtime_error("av_frame_alloc error");
}

void Caster::setAudioEncoderOpts(AudioEncoder encoder, AVDictionary **opts) {
    switch (encoder) {
        case AudioEncoder::Aac:
            av_dict_set(opts, "aac_coder", "fast", 0);
            break;
        case AudioEncoder::Mp3Lame:
            av_dict_set(opts, "b", "128k", 0);
            av_dict_set(opts, "compression_level", "9", 0);
            break;
        default:
            LOGE("failed to set audio encoder options");
    }
}

void Caster::initAvAudioEncoder() {
    LOGD("initing audio encoder");

    auto type = m_config.streamFormat == StreamFormat::Mp3
                    ? AudioEncoder::Mp3Lame
                    : AudioEncoder::Aac;

    const auto *encoder =
        avcodec_find_encoder_by_name(audioEncoderAvName(type).c_str());
    if (!encoder)
        throw std::runtime_error("no audio encoder: "s +
                                 audioEncoderAvName(type));

    m_outAudioCtx = avcodec_alloc_context3(encoder);
    if (m_outAudioCtx == nullptr)
        throw std::runtime_error("avcodec_alloc_context3 for out audio error");

    const auto &props = audioProps();

    m_outAudioCtx->sample_fmt = bestAudioSampleFormat(encoder, props);
    LOGD("audio encoder sample fmt: " << m_outAudioCtx->sample_fmt);

    av_channel_layout_default(&m_outAudioCtx->ch_layout, props.channels);
    m_outAudioCtx->sample_rate = m_inAudioCtx->sample_rate;
    m_outAudioCtx->time_base = AVRational{1, m_outAudioCtx->sample_rate};

    AVDictionary *opts = nullptr;

    setAudioEncoderOpts(type, &opts);

    if (avcodec_open2(m_outAudioCtx, encoder, &opts) < 0) {
        av_dict_free(&opts);
        throw std::runtime_error("avcodec_open2 for out audio error");
    }

    cleanAvOpts(&opts);

    m_audioFrameOut = av_frame_alloc();
    if (m_audioFrameOut == nullptr)
        throw std::runtime_error("av_frame_alloc error");
}

void Caster::initAvAudioResampler() {
    LOGD("initing audio resampler");

    if (swr_alloc_set_opts2(&m_audioSwrCtx, &m_outAudioCtx->ch_layout,
                            m_outAudioCtx->sample_fmt,
                            m_outAudioCtx->sample_rate,
                            &m_inAudioCtx->ch_layout, m_inAudioCtx->sample_fmt,
                            m_inAudioCtx->sample_rate, 0, nullptr) != 0) {
        throw std::runtime_error("swr_alloc error");
    }

    if (swr_init(m_audioSwrCtx) != 0) {
        throw std::runtime_error("swr_init error");
    }
}

void Caster::initAvAudio() {
    initAvAudioDecoder();
    initAvAudioEncoder();

    if (m_inAudioCtx->sample_fmt != m_outAudioCtx->sample_fmt) {
        LOGD("audio resampling required");
        initAvAudioResampler();
    }

    m_audioFrameSize = av_samples_get_buffer_size(
        nullptr, m_inAudioCtx->ch_layout.nb_channels, m_outAudioCtx->frame_size,
        m_inAudioCtx->sample_fmt, 0);
}

void Caster::initAvVideoForGst() {
    LOGD("initing video for gst");

    auto *in_buf = static_cast<uint8_t *>(av_malloc(m_videoBufSize));
    if (in_buf == nullptr) {
        av_freep(&in_buf);
        throw std::runtime_error("unable to allocate in av buf");
    }

    auto *in_ctx = avformat_alloc_context();

    in_ctx->max_analyze_duration = m_avMaxAnalyzeDuration;
    in_ctx->probesize = m_avProbeSize;

    in_ctx->pb =
        avio_alloc_context(in_buf, m_videoBufSize, 0, this,
                           avReadPacketCallbackStatic, nullptr, nullptr);
    if (in_ctx->pb == nullptr) {
        avformat_free_context(in_ctx);
        av_freep(&in_buf);
        throw std::runtime_error("avio_alloc_context error");
    }

    in_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

    m_inVideoFormatCtx = in_ctx;

    m_videoFramerate = static_cast<int>(
        *videoProps().formats.front().frameSpecs.front().framerates.begin());
}

Caster::Dim Caster::computeTransDim(Dim dim, VideoTrans trans,
                                    VideoScale scale) {
    Dim outDim;

    auto factor = [scale]() {
        switch (scale) {
            case VideoScale::Off:
                return 1.0;
            case VideoScale::Down25:
                return 0.75;
            case VideoScale::Down50:
                return 0.5;
            case VideoScale::Down75:
                return 0.25;
        }
        return 1.0;
    }();

    switch (trans) {
        case VideoTrans::Off:
        case VideoTrans::Vflip:
        case VideoTrans::Scale:
            outDim.width = std::ceil(dim.width * factor);
            outDim.height = std::ceil(dim.height * factor);
            break;
        case VideoTrans::Frame169:
        case VideoTrans::Frame169Rot90:
        case VideoTrans::Frame169Rot180:
        case VideoTrans::Frame169Rot270:
        case VideoTrans::Frame169Vflip:
        case VideoTrans::Frame169VflipRot90:
        case VideoTrans::Frame169VflipRot180:
        case VideoTrans::Frame169VflipRot270:
            outDim.height = static_cast<uint32_t>(
                std::ceil(std::max(dim.width, dim.height) * factor));
            outDim.width =
                static_cast<uint32_t>(std::ceil((16.0 / 9.0) * outDim.height));
            break;
    }

    outDim.height -= outDim.height % 2;
    outDim.width -= outDim.width % 2;

    LOGD("dim change: " << dim << " => " << outDim << " (thin=" << dim.thin()
                        << ")");

    return outDim;
}

void Caster::initAvVideoFiltersFrame169() {
    const auto filters = [this] {
        if (m_inDim.thin())
            return std::unordered_map<VideoTrans, std::string>{
                {VideoTrans::Frame169,
                 "scale=h={1}:w=-1,pad=width={0}:height={1}:x=-1:y=-2:color="
                 "black"},
                {VideoTrans::Frame169Rot90,
                 "transpose=dir=cclock,scale=h=-1:w={0},pad=width={0}:height={"
                 "1}"
                 ":x=-1:y=-1:color=black"},
                {VideoTrans::Frame169Rot180,
                 "scale=h={1}:w=-1,vflip,pad=width={0}:height={1}:x=-1:y=-1:"
                 "color=black"},
                {VideoTrans::Frame169Rot270,
                 "transpose=dir=clock,scale=h=-1:w={0},pad=width={0}:height={"
                 "1}:x=-1:y=-1:color=black"},
                {VideoTrans::Frame169Vflip,
                 "scale=h={1}:w=-1,vflip,pad=width={0}:height={1}:x=-1:y=-2:"
                 "color=black"},
                {VideoTrans::Frame169VflipRot90,
                 "transpose=dir=cclock_flip,scale=h=-1:w={0},pad=width={0}:"
                 "height={1}:x=-1:y=-1:color=black"},
                {VideoTrans::Frame169VflipRot180,
                 "scale=h={1}:w=-1,hflip,pad=width={0}:height={1}:x=-1:y=-1:"
                 "color=black"},
                {VideoTrans::Frame169VflipRot270,
                 "transpose=dir=clock_flip,scale=h=-1:w={0},pad=width={0}:"
                 "height={1}:x=-1:y=-1:color=black"}};
        return std::unordered_map<VideoTrans, std::string>{
            {VideoTrans::Frame169,
             "scale=h={1}:w=-1,pad=width={0}:height={1}:x=-1:y=-2:color="
             "black"},
            {VideoTrans::Frame169Rot90,
             "transpose=dir=cclock,scale=h={1}:w=-1,pad=width={0}:height={1}"
             ":x=-1:y=-1:color=black"},
            {VideoTrans::Frame169Rot180,
             "scale=h={1}:w=-1,vflip,pad=width={0}:height={1}:x=-1:y=-1:"
             "color=black"},
            {VideoTrans::Frame169Rot270,
             "transpose=dir=clock,scale=h={1}:w=-1,pad=width={0}:height={"
             "1}:x=-1:y=-1:color=black"},
            {VideoTrans::Frame169Vflip,
             "scale=h={1}:w=-1,vflip,pad=width={0}:height={1}:x=-1:y=-2:"
             "color=black"},
            {VideoTrans::Frame169VflipRot90,
             "transpose=dir=cclock_flip,scale=h={1}:w=-1,pad=width={0}:"
             "height={1}:x=-1:y=-1:color=black"},
            {VideoTrans::Frame169VflipRot180,
             "scale=h={1}:w=-1,hflip,pad=width={0}:height={1}:x=-1:y=-1:"
             "color=black"},
            {VideoTrans::Frame169VflipRot270,
             "transpose=dir=clock_flip,scale=h={1}:w=-1,pad=width={0}:"
             "height={1}:x=-1:y=-1:color=black"}};
    }();

    for (const auto &p : filters) initAvVideoFilter(p.first, p.second);
}

void Caster::initAvVideoFilters() {
    m_videoTrans = videoProps().trans;

    if (m_videoTrans == VideoTrans::Off) {
        if (m_inVideoCtx->pix_fmt != m_outVideoCtx->pix_fmt) {
            LOGD("pixfmt conversion required: "
                 << m_inVideoCtx->pix_fmt << " => " << m_outVideoCtx->pix_fmt);
            m_videoTrans = VideoTrans::Scale;
        } else if (m_inVideoCtx->width != m_outVideoCtx->width ||
                   m_inVideoCtx->height != m_outVideoCtx->height) {
            LOGD("dim conversion required");
            m_videoTrans = VideoTrans::Scale;
        } else {
            LOGD("video filtering is not needed");
            return;
        }
    }

    m_videoFrameAfterFilter = av_frame_alloc();

    switch (m_videoTrans) {
        case VideoTrans::Scale:
        case VideoTrans::Vflip:
            initAvVideoFilter(VideoTrans::Scale, "scale=h={1}:w={0}");
            initAvVideoFilter(VideoTrans::Vflip, "scale=h={1}:w={0},vflip");
            break;
        case VideoTrans::Frame169:
            initAvVideoFiltersFrame169();
            break;
        default:
            throw std::runtime_error("unsuported video trans");
    }
}

void Caster::initAvVideoFilter(VideoTrans trans, const std::string &fmt) {
    initAvVideoFilter(
        m_videoFilterCtxMap[trans],
        fmt::format(fmt, m_outVideoCtx->width, m_outVideoCtx->height).c_str());
}

void Caster::initAvVideoFilter(FilterCtx &ctx, const char *arg) {
    LOGD("initing av filter: " << arg);

    ctx.in = avfilter_inout_alloc();
    ctx.out = avfilter_inout_alloc();
    ctx.graph = avfilter_graph_alloc();
    if (ctx.in == nullptr || ctx.out == nullptr || ctx.graph == nullptr)
        throw std::runtime_error("failed to allocate av filter");

    const auto *buffersrc = avfilter_get_by_name("buffer");
    if (buffersrc == nullptr) throw std::runtime_error("no buffer filter");

    char srcArgs[512];
    snprintf(srcArgs, sizeof(srcArgs),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d", m_inVideoCtx->width,
             m_inVideoCtx->height, m_inVideoCtx->pix_fmt,
             m_inVideoCtx->time_base.num, m_inVideoCtx->time_base.den);
    LOGD("filter bufsrc: " << srcArgs);

    if (avfilter_graph_create_filter(&ctx.srcCtx, buffersrc, "in", srcArgs,
                                     nullptr, ctx.graph) < 0) {
        throw std::runtime_error("src avfilter_graph_create_filter error");
    }

    const auto *buffersink = avfilter_get_by_name("buffersink");
    if (buffersink == nullptr) throw std::runtime_error("no buffersink filter");

    if (avfilter_graph_create_filter(&ctx.sinkCtx, buffersink, "out", nullptr,
                                     nullptr, ctx.graph) < 0) {
        throw std::runtime_error("sink avfilter_graph_create_filter error");
    }

    enum AVPixelFormat pix_fmts[] = {m_outVideoCtx->pix_fmt, AV_PIX_FMT_NONE};
    if (av_opt_set_int_list(ctx.sinkCtx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE,
                            AV_OPT_SEARCH_CHILDREN) < 0) {
        throw std::runtime_error("av_opt_set_int_list error");
    }

    ctx.out->name = av_strdup("in");
    ctx.out->filter_ctx = ctx.srcCtx;
    ctx.out->pad_idx = 0;
    ctx.out->next = nullptr;

    ctx.in->name = av_strdup("out");
    ctx.in->filter_ctx = ctx.sinkCtx;
    ctx.in->pad_idx = 0;
    ctx.in->next = nullptr;

    if (avfilter_graph_parse_ptr(ctx.graph, arg, &ctx.in, &ctx.out, nullptr) <
        0)
        throw std::runtime_error("avfilter_graph_parse_ptr error");

    if (avfilter_graph_config(ctx.graph, nullptr) < 0)
        throw std::runtime_error("avfilter_graph_config error");

    LOGD("av filter successfully inited");
}

AVSampleFormat Caster::bestAudioSampleFormat(
    const AVCodec *encoder, const AudioSourceInternalProps &props) {
    if (encoder->sample_fmts == nullptr)
        throw std::runtime_error(
            "audio encoder does not support any sample fmts");

    LOGD("sample fmts supported by audio encoder:");
    for (int i = 0; encoder->sample_fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
        LOGD("[" << i << "]: " << encoder->sample_fmts[i]);
    }

    const auto *decoder = avcodec_find_decoder(props.codec);
    if (!decoder)
        throw std::runtime_error("no audio decoder for codec: "s +
                                 avCodecName(props.codec));
    if (decoder->sample_fmts == nullptr ||
        decoder->sample_fmts[0] == AV_SAMPLE_FMT_NONE)
        throw std::runtime_error(
            "audio decoder does not support any sample fmts");

    AVSampleFormat bestFmt = AV_SAMPLE_FMT_NONE;

    for (int i = 0; encoder->sample_fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
        bestFmt = encoder->sample_fmts[i];
        if (bestFmt == decoder->sample_fmts[0]) {
            LOGD("sample fmt exact match");
            break;
        }
    }

    return bestFmt;
}

bool Caster::nicePixfmt(AVPixelFormat fmt) {
    return std::find(nicePixfmts.cbegin(), nicePixfmts.cend(), fmt) !=
           nicePixfmts.cend();
}

AVPixelFormat Caster::fixPixfmt(AVPixelFormat fmt,
                                const AVPixelFormat *supportedFmts) {
    if (nicePixfmt(fmt)) return fmt;

    auto newFmt = fmt;

    for (int i = 0;; ++i) {
        if (nicePixfmt(supportedFmts[i])) {
            newFmt = supportedFmts[i];
            break;
        }
        if (supportedFmts[i] == AV_PIX_FMT_NONE) break;
    }

    if (fmt == newFmt)
        LOGW("encoder does not support any nice pixfmt");
    else
        LOGD("changing encoder pixfmt to nice one: " << fmt << " => "
                                                     << newFmt);

    return newFmt;
}

std::pair<std::reference_wrapper<const Caster::VideoFormatExt>, AVPixelFormat>
Caster::bestVideoFormat(const AVCodec *encoder,
                        const VideoSourceInternalProps &props) {
    if (encoder->pix_fmts == nullptr)
        throw std::runtime_error("encoder does not support any pixfmts");

    LOGD("pixfmts supported by encoder: " << encoder->pix_fmts);

    if (auto it =
            std::find_if(props.formats.cbegin(), props.formats.cend(),
                         [encoder](const auto &sf) {
                             for (int i = 0;; ++i) {
                                 if (nicePixfmt(encoder->pix_fmts[i]) &&
                                     encoder->pix_fmts[i] == sf.pixfmt)
                                     return true;
                                 if (encoder->pix_fmts[i] == AV_PIX_FMT_NONE)
                                     return false;
                             }
                         });
        it != props.formats.cend()) {
        LOGD("pixfmt exact match: " << it->pixfmt);

        return {*it, it->pixfmt};
    }

    auto fmt = avcodec_find_best_pix_fmt_of_list(
        encoder->pix_fmts, props.formats.front().pixfmt, 0, nullptr);

    return {props.formats.front(), fixPixfmt(fmt, encoder->pix_fmts)};
}

static std::string tempPathForX264() {
    char path[] = "/tmp/libx264-XXXXXX";
    auto fd = mkstemp(path);
    if (fd == -1) throw std::runtime_error("mkstemp error");
    close(fd);
    return path;
}

void Caster::setVideoEncoderOpts(VideoEncoder encoder, AVDictionary **opts) {
    switch (encoder) {
        case VideoEncoder::Nvenc:
            av_dict_set(opts, "preset", "p1", 0);
            av_dict_set(opts, "tune", "ull", 0);
            av_dict_set(opts, "zerolatency", "1", 0);
            av_dict_set(opts, "rc", "constqp", 0);
            break;
        case VideoEncoder::X264:
            av_dict_set(opts, "preset", "ultrafast", 0);
            av_dict_set(opts, "tune", "zerolatency", 0);
            av_dict_set(opts, "passlogfile", tempPathForX264().c_str(), 0);
            break;
        default:
            LOGW("failed to set video encoder options");
    }
}

std::string Caster::videoEncoderAvName(VideoEncoder encoder) {
    switch (encoder) {
        case VideoEncoder::X264:
            return "libx264";
        case VideoEncoder::Nvenc:
            return "h264_nvenc";
        case VideoEncoder::V4l2:
            return "h264_v4l2m2m";
        case VideoEncoder::Auto:
            break;
    }

    throw std::runtime_error("invalid video encoder");
}

std::string Caster::audioEncoderAvName(AudioEncoder encoder) {
    switch (encoder) {
        case AudioEncoder::Aac:
            return "aac";
        case AudioEncoder::Mp3Lame:
            return "libmp3lame";
    }

    throw std::runtime_error("invalid audio encoder");
}

std::string Caster::videoSourceAvName(VideoSourceType type) {
    switch (type) {
        case VideoSourceType::V4l2:
            return "video4linux2";
        case VideoSourceType::X11Capture:
            return "x11grab";
        case VideoSourceType::LipstickCapture:
        case VideoSourceType::Test:
            return "rawvideo";
        case VideoSourceType::DroidCam:
        case VideoSourceType::Unknown:
            break;
    }

    throw std::runtime_error("invalid video source");
}

std::string Caster::streamFormatAvName(StreamFormat format) {
    switch (format) {
        case StreamFormat::Mp4:
            return "mp4";
        case StreamFormat::MpegTs:
            return "mpegts";
        case StreamFormat::Mp3:
            return "mp3";
    }

    throw std::runtime_error("invalid stream format");
}

void Caster::initAvVideoRawDecoder() {
    const auto *decoder = avcodec_find_decoder(AV_CODEC_ID_RAWVIDEO);
    if (decoder == nullptr)
        throw std::runtime_error("avcodec_find_decoder for video error");

    m_inVideoCtx = avcodec_alloc_context3(decoder);
    if (m_inVideoCtx == nullptr)
        throw std::runtime_error("avcodec_alloc_context3 for video error");

    m_inVideoCtx->pix_fmt = m_inPixfmt;
    m_inVideoCtx->width = static_cast<int>(m_inDim.width);
    m_inVideoCtx->height = static_cast<int>(m_inDim.height);
    m_inVideoCtx->time_base = AVRational{1, m_videoFramerate};

    m_videoRawFrameSize = av_image_get_buffer_size(
        m_inVideoCtx->pix_fmt, m_inVideoCtx->width, m_inVideoCtx->height, 32);

    if (avcodec_open2(m_inVideoCtx, nullptr, nullptr) != 0) {
        throw std::runtime_error("avcodec_open2 for in video error");
    }

    LOGD("video decoder: tb=" << m_inVideoCtx->time_base
                              << ", pixfmt=" << m_inVideoCtx->pix_fmt
                              << ", width=" << m_inVideoCtx->width
                              << ", height=" << m_inVideoCtx->height
                              << ", raw frame size=" << m_videoRawFrameSize);

    m_videoFrameIn = av_frame_alloc();
}

void Caster::initAvVideoEncoder(VideoEncoder type) {
    auto enc = videoEncoderAvName(type);

    LOGD("initing video encoder: " << enc);

    const auto *encoder = avcodec_find_encoder_by_name(enc.c_str());
    if (!encoder) throw std::runtime_error(fmt::format("no {} encoder", enc));

    m_outVideoCtx = avcodec_alloc_context3(encoder);
    if (m_outVideoCtx == nullptr)
        throw std::runtime_error("avcodec_alloc_context3 for video error");

    const auto &props = videoProps();

    const auto &bestFormat = [&]() {
#ifdef USE_V4L2
        if (type == VideoEncoder::V4l2)
            return bestVideoFormatForV4l2Encoder(props);
#endif
        return bestVideoFormat(encoder, props);
    }();

    m_outVideoCtx->pix_fmt = bestFormat.second;
    if (m_outVideoCtx->pix_fmt == AV_PIX_FMT_NONE)
        throw std::runtime_error("failed to find pixfmt for video encoder");

    const auto &fs = bestFormat.first.get().frameSpecs.front();

    m_videoFramerate = static_cast<int>(*fs.framerates.begin());

    m_outVideoCtx->time_base = AVRational{1, m_videoFramerate};
    m_outVideoCtx->flags = AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;

    m_inDim = fs.dim;
    m_inPixfmt = bestFormat.first.get().pixfmt;

    auto outDim = computeTransDim(m_inDim, props.trans, props.scale);
    m_outVideoCtx->width = static_cast<int>(outDim.width);
    m_outVideoCtx->height = static_cast<int>(outDim.height);

    AVDictionary *opts = nullptr;

    setVideoEncoderOpts(type, &opts);

    if (avcodec_open2(m_outVideoCtx, nullptr, &opts) < 0) {
        av_dict_free(&opts);
        throw std::runtime_error("avcodec_open2 for out video error");
    }

    cleanAvOpts(&opts);

    LOGD("video encoder: tb=" << m_outVideoCtx->time_base
                              << ", pixfmt=" << m_outVideoCtx->pix_fmt
                              << ", width=" << m_outVideoCtx->width
                              << ", height=" << m_outVideoCtx->height
                              << ", framerate=" << m_videoFramerate);

    LOGD("encoder successfuly inited");
}

void Caster::initAvVideoEncoder() {
    if (m_config.videoEncoder == VideoEncoder::Auto) {
        try {
            initAvVideoEncoder(VideoEncoder::V4l2);
        } catch (const std::runtime_error &e) {
            LOGW("failed to init h264_v4l2m2m encoder: " << e.what());
            try {
                initAvVideoEncoder(VideoEncoder::Nvenc);
            } catch (const std::runtime_error &e) {
                LOGW("failed to init h264_nvenc encoder: " << e.what());
                initAvVideoEncoder(VideoEncoder::X264);
            }
        }
        return;
    }

    initAvVideoEncoder(m_config.videoEncoder);
}

void Caster::initAvVideoInputRawFormat() {
    const auto &props = videoProps();

    const auto *in_video_format =
        av_find_input_format(videoSourceAvName(props.type).c_str());
    if (in_video_format == nullptr)
        throw std::runtime_error("av_find_input_format for video error");

    AVDictionary *opts = nullptr;

    auto dim = fmt::format("{}x{}", m_inDim.width, m_inDim.height);
    av_dict_set(&opts, "video_size", dim.c_str(), 0);
    av_dict_set_int(&opts, "framerate", m_videoFramerate, 0);

    if (props.type == VideoSourceType::V4l2)
        av_dict_set(&opts, "input_format", av_get_pix_fmt_name(m_inPixfmt), 0);

    AVFormatContext *in_cxt = nullptr;
    if (avformat_open_input(&in_cxt, props.dev.c_str(), in_video_format,
                            &opts) < 0) {
        av_dict_free(&opts);
        throw std::runtime_error("avformat_open_input for video error");
    }

    cleanAvOpts(&opts);

    m_inVideoFormatCtx = in_cxt;
}

void Caster::initAv() {
    LOGD("av init started");

    if (audioEnabled()) initAvAudio();

    if (videoEnabled()) {
        const auto &props = videoProps();

        switch (props.type) {
            case VideoSourceType::DroidCam:
                initAvVideoForGst();
                break;
            case VideoSourceType::V4l2:
            case VideoSourceType::X11Capture:
                initAvVideoEncoder();
                initAvVideoInputRawFormat();
                initAvVideoRawDecoderFromInputStream(
                    findAvVideoInputStreamIdx());
                break;
            case VideoSourceType::LipstickCapture:
            case VideoSourceType::Test:
                initAvVideoEncoder();
                initAvVideoRawDecoder();
                initAvVideoFilters();
                break;
            default:
                throw std::runtime_error("unknown video source type");
        }

        m_videoRealFrameDuration =
            rescaleToUsec(1, AVRational{1, m_videoFramerate});
        m_videoFrameDuration = m_videoRealFrameDuration / 2;
    }

    LOGD("using muxer: " << m_config.streamFormat);

    if (avformat_alloc_output_context2(
            &m_outFormatCtx, nullptr,
            streamFormatAvName(m_config.streamFormat).c_str(), nullptr) < 0) {
        throw std::runtime_error("avformat_alloc_output_context2 error");
    }

    setState(State::Inited);

    LOGD("av init completed");
}

void Caster::startAvVideoForGst() {
    AVDictionary *opts = nullptr;

    av_dict_set_int(&opts, "framerate", m_videoFramerate, 0);

    if (auto ret = avformat_open_input(&m_inVideoFormatCtx, "", 0, &opts);
        ret != 0) {
        av_dict_free(&opts);
        throw std::runtime_error("avformat_open_input for video error: " +
                                 strForAvError(ret));
    }

    cleanAvOpts(&opts);

    if (avformat_find_stream_info(m_inVideoFormatCtx, nullptr) < 0)
        throw std::runtime_error("avformat_find_stream_info for video error");

    auto idx = av_find_best_stream(m_inVideoFormatCtx, AVMEDIA_TYPE_VIDEO, -1,
                                   -1, nullptr, 0);
    if (idx < 0) throw std::runtime_error("no video stream found in input");

    auto *stream = m_inVideoFormatCtx->streams[idx];

    av_dump_format(m_inVideoFormatCtx, idx, "", 0);

    m_outVideoStream = avformat_new_stream(m_outFormatCtx, nullptr);
    if (!m_outVideoStream)
        throw std::runtime_error("avformat_new_stream for video error");

    m_outVideoStream->id = 0;

    if (avcodec_parameters_copy(m_outVideoStream->codecpar, stream->codecpar) <
        0) {
        throw std::runtime_error("avcodec_parameters_copy for video error");
    }
}

void Caster::initAvVideoRawDecoderFromInputStream(int idx) {
    const auto *stream = m_inVideoFormatCtx->streams[idx];

    m_inPixfmt = static_cast<AVPixelFormat>(stream->codecpar->format);

    const auto *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (decoder == nullptr)
        throw std::runtime_error("avcodec_find_decoder for video error");

    m_inVideoCtx = avcodec_alloc_context3(decoder);
    if (m_inVideoCtx == nullptr)
        throw std::runtime_error("avcodec_alloc_context3 for in video error");

    if (avcodec_parameters_to_context(m_inVideoCtx, stream->codecpar) < 0) {
        throw std::runtime_error(
            "avcodec_parameters_to_context for video error");
    }

    m_inVideoCtx->time_base = stream->time_base;
    m_videoRawFrameSize = av_image_get_buffer_size(
        m_inVideoCtx->pix_fmt, m_inVideoCtx->width, m_inVideoCtx->height, 32);

    if (avcodec_open2(m_inVideoCtx, nullptr, nullptr) != 0) {
        throw std::runtime_error("avcodec_open2 for in video error");
    }

    LOGD("video decoder: tb=" << m_inVideoCtx->time_base
                              << ", pixfmt=" << m_inVideoCtx->pix_fmt
                              << ", width=" << m_inVideoCtx->width
                              << ", height=" << m_inVideoCtx->height
                              << ", raw frame size=" << m_videoRawFrameSize);

    if (m_inVideoCtx->width != static_cast<int>(m_inDim.width) ||
        m_inVideoCtx->height != static_cast<int>(m_inDim.height) ||
        m_inVideoCtx->pix_fmt != m_inPixfmt) {
        LOGE("input stream has invalid params, expected: pixfmt="
             << m_inPixfmt << ", width=" << m_inDim.width
             << ", height=" << m_inDim.height);
        throw std::runtime_error("decoder params are invalid");
    }

    m_videoFrameIn = av_frame_alloc();
}

int Caster::findAvVideoInputStreamIdx() {
    if (avformat_find_stream_info(m_inVideoFormatCtx, nullptr) < 0)
        throw std::runtime_error("avformat_find_stream_info for video error");

    auto idx = av_find_best_stream(m_inVideoFormatCtx, AVMEDIA_TYPE_VIDEO, -1,
                                   -1, nullptr, 0);
    if (idx < 0) throw std::runtime_error("no video stream found in input");

    av_dump_format(m_inVideoFormatCtx, idx, "", 0);

    return idx;
}

void Caster::initAvVideoOutStream() {
    m_outVideoStream = avformat_new_stream(m_outFormatCtx, nullptr);
    if (!m_outVideoStream)
        throw std::runtime_error("avformat_new_stream for video error");

    m_outVideoStream->id = 0;
    m_outVideoStream->r_frame_rate = AVRational{m_videoFramerate, 1};

    if (avcodec_parameters_from_context(m_outVideoStream->codecpar,
                                        m_outVideoCtx) < 0) {
        throw std::runtime_error(
            "avcodec_parameters_from_context for video error");
    }
}

void Caster::initAvAudioDurations() {
    m_audioFrameDuration =
        rescaleToUsec(m_outAudioCtx->frame_size, m_inAudioCtx->time_base);
    m_audioPktDuration =
        rescaleFromUsec(m_audioFrameDuration, m_outAudioStream->time_base);

    LOGD("audio in tb: " << m_inAudioCtx->time_base);
    LOGD("audio out tb: " << m_outAudioCtx->time_base << " "
                    << m_outAudioStream->time_base);
    LOGD("audio frame dur: " << m_audioFrameDuration);
    LOGD("audio pkt dur: " << m_audioPktDuration);
    LOGD("audio samples in frame: " << m_outAudioCtx->frame_size);
    LOGD("audio frame size: " << m_audioFrameSize);
}

void Caster::startAvAudio() {
    m_outAudioStream = avformat_new_stream(m_outFormatCtx, nullptr);
    if (!m_outAudioStream) {
        throw std::runtime_error("avformat_new_stream for audio error");
    }

    m_outAudioStream->id = 1;

    if (avcodec_parameters_from_context(m_outAudioStream->codecpar,
                                        m_outAudioCtx) < 0) {
        throw std::runtime_error(
            "avcodec_parameters_from_context for audio error");
    }
}

void Caster::startAv() {
    LOGD("starting av");

    if (videoEnabled()) {
        const auto &props = videoProps();

        switch (props.type) {
            case VideoSourceType::DroidCam:
                startAvVideoForGst();
                break;
            case VideoSourceType::V4l2:
            case VideoSourceType::X11Capture:
                /*initAvVideoRawDecoderFromInputStream(
                    findAvVideoInputStreamIdx());*/
                initAvVideoOutStream();
                initAvVideoFilters();
                break;
            case VideoSourceType::LipstickCapture:
            case VideoSourceType::Test:
                initAvVideoOutStream();
                break;
            default:
                throw std::runtime_error("unknown video source type");
        }

        setVideoStreamRotation(m_config.videoOrientation);

        m_outVideoStream->time_base = AVRational{1, m_videoFramerate};
    }

    if (audioEnabled()) startAvAudio();

    auto *outBuf = static_cast<uint8_t *>(av_malloc(m_videoBufSize));
    if (outBuf == nullptr) {
        av_freep(&outBuf);
        throw std::runtime_error("unable to allocate out av buf");
    }

    m_outFormatCtx->pb =
        avio_alloc_context(outBuf, m_videoBufSize, 1, this, nullptr,
                           avWritePacketCallbackStatic, nullptr);
    if (m_outFormatCtx->pb == nullptr) {
        throw std::runtime_error("avio_alloc_context error");
    }

    AVDictionary *opts = nullptr;

    if (m_config.streamFormat == StreamFormat::MpegTs) {
        av_dict_set(&opts, "mpegts_m2ts_mode", "-1", 0);
        av_dict_set(&m_outFormatCtx->metadata, "service_provider",
                    m_config.streamAuthor.c_str(), 0);
        av_dict_set(&m_outFormatCtx->metadata, "service_name",
                    m_config.streamTitle.c_str(), 0);
    } else {
        av_dict_set(&opts, "movflags", "frag_custom+empty_moov+delay_moov", 0);
        av_dict_set(&m_outFormatCtx->metadata, "author",
                    m_config.streamAuthor.c_str(), 0);
        av_dict_set(&m_outFormatCtx->metadata, "title",
                    m_config.streamTitle.c_str(), 0);
    }

    m_outFormatCtx->flags |= AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS |
                             AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_AUTO_BSF;

    if (auto ret = avformat_write_header(m_outFormatCtx, &opts);
        ret != AVSTREAM_INIT_IN_WRITE_HEADER &&
        ret != AVSTREAM_INIT_IN_INIT_OUTPUT) {
        av_dict_free(&opts);
        throw std::runtime_error("avformat_write_header error");
    }

    cleanAvOpts(&opts);

    if (audioEnabled()) initAvAudioDurations();

    LOGD("av start completed");
}

void Caster::disconnectPaSinkInput() {
    if (m_paStream == nullptr) return;

    LOGD("disconnecting pa stream");

    if (m_connectedPaSinkInput != PA_INVALID_INDEX &&
        m_paSinkInputs.count(m_connectedPaSinkInput) > 0) {
        if (audioProps().muteSource)
            unmutePaSinkInput(m_paSinkInputs.at(m_connectedPaSinkInput));
    }

    pa_stream_disconnect(m_paStream);
    pa_stream_unref(m_paStream);
    m_paStream = nullptr;
    m_connectedPaSinkInput = PA_INVALID_INDEX;

    for (auto it = m_paSinkInputs.begin(); it != m_paSinkInputs.end();) {
        if (it->second.removed)
            it = m_paSinkInputs.erase(it);
        else
            ++it;
    }
}

void Caster::mutePaSinkInput([[maybe_unused]] PaSinkInput &si) {
#ifdef USE_SFOS
    auto *o = pa_context_move_sink_input_by_name(
        m_paCtx, si.idx, "sink.null",
        []([[maybe_unused]] pa_context *ctx, int success,
           [[maybe_unused]] void *userdata) {
            if (success)
                LOGD("pa sink input successfully muted");
            else
                LOGW("failed to mute pa sink input");
        },
        nullptr);
    if (o != nullptr) pa_operation_unref(o);
    si.muted = true;
#endif
}

void Caster::unmutePaSinkInput([[maybe_unused]] PaSinkInput &si) {
#ifdef USE_SFOS
    auto *o = pa_context_move_sink_input_by_index(
        m_paCtx, si.idx, si.sinkIdx,
        []([[maybe_unused]] pa_context *ctx, int success,
           [[maybe_unused]] void *userdata) {
            if (success)
                LOGD("pa sink input successfully unmuted");
            else
                LOGW("failed to unmute pa sink input");
        },
        nullptr);
    if (o != nullptr) pa_operation_unref(o);
    si.muted = false;
#endif
}

void Caster::connectPaSinkInput() {
    auto si = bestPaSinkInput();
    if (!si) {
        LOGD("no active pa sink input");
        disconnectPaSinkInput();
        return;
    }

    const auto idx = si->get().idx;

    LOGD("best pa sink input: " << idx);

    if (m_paStream != nullptr && m_connectedPaSinkInput != PA_INVALID_INDEX) {
        LOGD("connected pa sink input: " << m_connectedPaSinkInput);
        if (m_connectedPaSinkInput == idx) {
            LOGD("best pa sink input is already connected");
            return;
        }
        disconnectPaSinkInput();
    }

    const auto &props = audioProps();

    pa_sample_spec spec{ff_tools::ff_codec_id_to_pulse_format(props.codec),
                        props.rate, props.channels};

#ifdef USE_SFOS
    m_paStream = pa_stream_new(m_paCtx, "notiftone", &spec, nullptr);
#else
    m_paStream =
        pa_stream_new(m_paCtx, m_config.streamTitle.c_str(), &spec, nullptr);
#endif

    pa_stream_set_read_callback(m_paStream, paStreamRequestCallbackStatic,
                                this);

    if (props.muteSource) mutePaSinkInput(*si);

    const pa_buffer_attr attr{
        /*maxlength=*/static_cast<uint32_t>(-1),
        /*tlength=*/static_cast<uint32_t>(-1),
        /*prebuf=*/static_cast<uint32_t>(-1),
        /*minreq=*/static_cast<uint32_t>(-1),
        /*fragsize=*/static_cast<uint32_t>(m_audioFrameSize)};

    if (pa_stream_set_monitor_stream(m_paStream, idx) < 0) {
        if (props.muteSource) unmutePaSinkInput(*si);
        throw std::runtime_error("pa_stream_set_monitor_stream error");
    }

    LOGD("connecting pa sink input: " << si->get());

    m_connectedPaSinkInput = idx;

    if (pa_stream_connect_record(m_paStream, nullptr, &attr,
                                 PA_STREAM_ADJUST_LATENCY) != 0) {
        if (props.muteSource) unmutePaSinkInput(*si);
        throw std::runtime_error("pa_stream_connect_record error");
    }
}

void Caster::connectPaSource() {
    const auto &props = audioProps();

    pa_sample_spec spec{ff_tools::ff_codec_id_to_pulse_format(props.codec),
                        props.rate, props.channels};

    m_paStream =
        pa_stream_new(m_paCtx, m_config.streamTitle.c_str(), &spec, nullptr);
    pa_stream_set_read_callback(m_paStream, paStreamRequestCallbackStatic,
                                this);

    const pa_buffer_attr attr{
        /*maxlength=*/static_cast<uint32_t>(-1),
        /*tlength=*/static_cast<uint32_t>(-1),
        /*prebuf=*/static_cast<uint32_t>(-1),
        /*minreq=*/static_cast<uint32_t>(-1),
        /*fragsize=*/static_cast<uint32_t>(m_audioFrameSize)};

    LOGD("connecting pa source: " << props.dev);

    if (pa_stream_connect_record(
            m_paStream, props.dev.empty() ? nullptr : props.dev.c_str(), &attr,
            PA_STREAM_ADJUST_LATENCY) != 0) {
        throw std::runtime_error("pa_stream_connect_record error");
    }
}

void Caster::startPa() {
    LOGD("starting pa");

    switch (audioProps().type) {
        case AudioSourceType::Mic:
        case AudioSourceType::Monitor:
            connectPaSource();
            break;
        case AudioSourceType::Playback:
            connectPaSinkInput();
            break;
        default:
            throw std::runtime_error("invalid audio source type");
    }

    LOGD("pa started");
}

void Caster::paStreamRequestCallbackStatic(pa_stream *stream, size_t nbytes,
                                           void *userdata) {
    static_cast<Caster *>(userdata)->paStreamRequestCallback(stream, nbytes);
}

void Caster::paStreamRequestCallback(pa_stream *stream, size_t nbytes) {
    std::lock_guard lock{m_audioMtx};

    LOGT("pa audio sample: " << nbytes);

    const void *data;
    if (pa_stream_peek(stream, &data, &nbytes) != 0) {
        LOGW("pa_stream_peek error");
        return;
    }

    if (data == nullptr || nbytes == 0) {
        LOGW("no pa data");
        return;
    }

    m_audioBuf.pushExactForce(
        static_cast<const decltype(m_audioBuf)::BufType *>(data), nbytes);

    pa_stream_drop(stream);
}

void Caster::restartVideoCapture() {
    if (m_state != State::Started || m_state == State::Terminating ||
        m_restartRequested || m_restarting)
        return;

    LOGD("restart video capture requested");

    m_restartRequested = true;
    m_videoCv.notify_one();
}

void Caster::doPaTask() {
    const auto sleepDur = std::chrono::microseconds{m_audioFrameDuration};
    LOGD("starting pa thread");
    try {
        while (!terminating()) {
            if (pa_mainloop_iterate(m_paLoop, 0, nullptr) < 0) break;
            std::this_thread::sleep_for(sleepDur);
        }
    } catch (const std::runtime_error &e) {
        LOGE("error in pa thread: " << e.what());
        reportError();
    }

    LOGD("pa thread ended");
}

void Caster::startAudioOnlyMuxing() {
    if (!audioMuted()) {
        m_audioPaThread = std::thread(&Caster::doPaTask, this);
    }

    m_avMuxingThread = std::thread([this, sleep = m_audioFrameDuration / 2]() {
        LOGD("starting muxing");

        try {
            auto *audio_pkt = av_packet_alloc();
            m_nextAudioPts = 0;

            while (!terminating()) {
                if (muxAudio(audio_pkt))
                    av_write_frame(m_outFormatCtx, nullptr);  // force fragment
                av_usleep(sleep);
            }

            av_packet_free(&audio_pkt);
        } catch (const std::runtime_error &e) {
            LOGE("error in audio muxing thread: " << e.what());
            reportError();
        }

        LOGD("muxing ended");
    });
}

void Caster::startVideoOnlyMuxing() {
    m_avMuxingThread = std::thread([this]() {
        LOGD("starting muxing");

        try {
            auto *video_pkt = av_packet_alloc();
            m_nextVideoPts = 0;

            while (!terminating()) {
                if (muxVideo(video_pkt))
                    av_write_frame(m_outFormatCtx, nullptr);  // force fragment
            }

            av_packet_free(&video_pkt);
        } catch (const std::runtime_error &e) {
            LOGE("error in video muxing thread: " << e.what());
            reportError();
        }

        LOGD("muxing ended");
    });
}

void Caster::startVideoAudioMuxing() {
    if (!audioMuted()) {
        m_audioPaThread = std::thread(&Caster::doPaTask, this);
    }

    m_avMuxingThread = std::thread([this]() {
        LOGD("starting muxing");

        try {
            auto *video_pkt = av_packet_alloc();
            auto *audio_pkt = av_packet_alloc();
            m_nextVideoPts = 0;
            m_nextAudioPts = 0;

            while (!terminating()) {
                bool pktDone = muxVideo(video_pkt);
                if (muxAudio(audio_pkt)) pktDone = true;
                if (pktDone)
                    av_write_frame(m_outFormatCtx, nullptr);  // force fragment
            }

            av_packet_free(&video_pkt);
            av_packet_free(&audio_pkt);
        } catch (const std::runtime_error &e) {
            LOGE("error in video-audio muxing thread: " << e.what());
            reportError();
        }

        LOGD("muxing ended");
    });
}

void Caster::startMuxing() {
    if (videoEnabled()) {
        if (audioEnabled())
            startVideoAudioMuxing();
        else
            startVideoOnlyMuxing();
    } else {
        if (audioEnabled())
            startAudioOnlyMuxing();
        else
            throw std::runtime_error("audio and video disabled");
    }
}

static auto orientationToRot(Caster::VideoOrientation o) {
    switch (o) {
        case Caster::VideoOrientation::Auto:
        case Caster::VideoOrientation::Landscape:
            return 0;
        case Caster::VideoOrientation::Portrait:
            return 90;
        case Caster::VideoOrientation::InvertedLandscape:
            return 180;
        case Caster::VideoOrientation::InvertedPortrait:
            return 270;
    }

    return 0;
}

void Caster::setVideoStreamRotation(VideoOrientation requestedOrientation) {
    const auto &props = videoProps();

    auto rotation = [ro = requestedOrientation, o = props.orientation]() {
        if (ro == VideoOrientation::Auto || ro == o) return 0;
        return (orientationToRot(ro) + orientationToRot(o)) % 360;
    }();

    LOGD("video rotation: " << rotation << ", o=" << props.orientation << " ("
                            << orientationToRot(props.orientation)
                            << "), ro=" << requestedOrientation << " ("
                            << orientationToRot(requestedOrientation) << ")");

    if (rotation == 0) return;

    if (m_outVideoStream->side_data == nullptr) {
        if (!av_stream_new_side_data(m_outVideoStream,
                                     AV_PKT_DATA_DISPLAYMATRIX,
                                     sizeof(int32_t) * 9)) {
            throw std::runtime_error("av_stream_new_side_data error");
        }
    }

    av_display_rotation_set(
        reinterpret_cast<int32_t *>(m_outVideoStream->side_data->data),
        rotation);
}

bool Caster::readVideoFrameFromBuf(AVPacket *pkt) {
    std::unique_lock lock{m_videoMtx};

    if (!m_videoBuf.hasEnoughData(m_videoRawFrameSize)) {
        LOGT("video buff dont have enough data");
        lock.unlock();
        av_usleep(m_videoFrameDuration);
        return false;
    }

    if (av_new_packet(pkt, m_videoRawFrameSize) < 0) {
        throw std::runtime_error("av_new_packet for video error");
    }

    m_videoBuf.pull(pkt->data, m_videoRawFrameSize);

    return true;
}

void Caster::readVideoFrameFromDemuxer(AVPacket *pkt) {
    if (av_read_frame(m_inVideoFormatCtx, pkt) != 0)
        throw std::runtime_error("av_read_frame for video error");
}

bool Caster::filterVideoFrame(VideoTrans trans, AVFrame *frameIn,
                              AVFrame *frameOut) {
    auto &ctx = m_videoFilterCtxMap.at(trans);

    if (av_buffersrc_add_frame_flags(ctx.srcCtx, frameIn,
                                     AV_BUFFERSRC_FLAG_PUSH) < 0)
        throw std::runtime_error("av_buffersrc_add_frame_flags error");

    auto ret = av_buffersink_get_frame(ctx.sinkCtx, frameOut);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return false;
    if (ret < 0) throw std::runtime_error("av_buffersink_get_frame error");

    return true;
}

void Caster::convertVideoFramePixfmt(AVFrame *frameIn, AVFrame *frameOut) {
    sws_scale(m_videoSwsCtx, frameIn->data, frameIn->linesize, 0,
              m_inVideoCtx->height, frameOut->data, frameOut->linesize);

    av_frame_copy_props(frameOut, frameIn);
    frameOut->format = m_outVideoCtx->pix_fmt;
    frameOut->width = frameIn->width;
    frameOut->height = frameIn->height;

    av_frame_unref(frameIn);
}

AVFrame *Caster::filterVideoIfNeeded(AVFrame *frameIn) {
    switch (m_videoTrans) {
        case VideoTrans::Off:
            break;
        case VideoTrans::Scale:
        case VideoTrans::Vflip: {
            auto t = m_videoTrans;
#ifdef USE_LIPSTICK_RECORDER
            t = m_lipstickRecorder && m_lipstickRecorder->yinverted()
                    ? VideoTrans::Vflip
                    : VideoTrans::Scale;
#endif
            if (!filterVideoFrame(t, frameIn, m_videoFrameAfterFilter)) {
                av_frame_unref(m_videoFrameIn);
                av_frame_unref(m_videoFrameAfterFilter);
                return nullptr;
            }

            av_frame_unref(frameIn);
            return m_videoFrameAfterFilter;
        }
        case VideoTrans::Frame169:
        case VideoTrans::Frame169Rot90:
        case VideoTrans::Frame169Rot180:
        case VideoTrans::Frame169Rot270:
        case VideoTrans::Frame169Vflip:
        case VideoTrans::Frame169VflipRot90:
        case VideoTrans::Frame169VflipRot180:
        case VideoTrans::Frame169VflipRot270: {
            auto t = VideoTrans::Frame169;
#ifdef USE_LIPSTICK_RECORDER
            if (m_lipstickRecorder) {
                t = [this]() {
                    auto inv = m_lipstickRecorder->yinverted();
                    switch (m_lipstickRecorder->transform()) {
                        case LipstickRecorderSource::Transform::Normal:
                            return inv ? VideoTrans::Frame169Vflip
                                       : VideoTrans::Frame169;
                        case LipstickRecorderSource::Transform::Rot90:
                            return inv ? VideoTrans::Frame169VflipRot90
                                       : VideoTrans::Frame169Rot90;
                        case LipstickRecorderSource::Transform::Rot180:
                            return inv ? VideoTrans::Frame169VflipRot180
                                       : VideoTrans::Frame169Rot180;
                        case LipstickRecorderSource::Transform::Rot270:
                            return inv ? VideoTrans::Frame169VflipRot270
                                       : VideoTrans::Frame169Rot270;
                    }
                    return inv ? VideoTrans::Frame169Vflip
                               : VideoTrans::Frame169;
                }();
            }
#endif
            if (!filterVideoFrame(t, frameIn, m_videoFrameAfterFilter)) {
                av_frame_unref(m_videoFrameIn);
                av_frame_unref(m_videoFrameAfterFilter);
                return nullptr;
            }

            av_frame_unref(frameIn);
            return m_videoFrameAfterFilter;
        }
    }

    return frameIn;
}

bool Caster::encodeVideoFrame(AVPacket *pkt) {
    if (auto ret = avcodec_send_packet(m_inVideoCtx, pkt);
        ret != 0 && ret != AVERROR(EAGAIN)) {
        av_packet_unref(pkt);
        throw std::runtime_error(fmt::format(
            "avcodec_send_packet for video error ({})", strForAvError(ret)));
    }

    av_packet_unref(pkt);

    if (auto ret = avcodec_receive_frame(m_inVideoCtx, m_videoFrameIn);
        ret != 0) {
        throw std::runtime_error("avcodec_receive_frame for video error");
    }

    m_videoFrameIn->format = m_inVideoCtx->pix_fmt;
    m_videoFrameIn->width = m_inVideoCtx->width;
    m_videoFrameIn->height = m_inVideoCtx->height;

    auto *frameOut = filterVideoIfNeeded(m_videoFrameIn);
    if (frameOut == nullptr) return false;

    if (auto ret = avcodec_send_frame(m_outVideoCtx, frameOut);
        ret != 0 && ret != AVERROR(EAGAIN)) {
        av_frame_unref(frameOut);
        throw std::runtime_error("avcodec_send_frame for video error");
    }

    av_frame_unref(frameOut);

    if (auto ret = avcodec_receive_packet(m_outVideoCtx, pkt); ret != 0) {
        if (ret == AVERROR(EAGAIN)) {
            LOGD("video pkt not ready");
            return false;
        }

        throw std::runtime_error("avcodec_receive_packet for video error");
    }

    return true;
}

bool Caster::muxVideo(AVPacket *pkt) {
    const auto now = av_gettime();

    if (m_restartRequested || m_restarting) {
        if (m_keyVideoPkt == nullptr || videoDelay(now) < 0) return false;

        LOGT("video read key frame");
        if (auto ret = av_packet_ref(pkt, m_keyVideoPkt); ret != 0) {
            throw std::runtime_error("av_packet_ref video error");
        }
    } else {
        LOGT("video read real frame");
        switch (videoProps().type) {
            case VideoSourceType::DroidCam:
                readVideoFrameFromDemuxer(pkt);
                break;
            case VideoSourceType::V4l2:
            case VideoSourceType::X11Capture:
                readVideoFrameFromDemuxer(pkt);
                if (!encodeVideoFrame(pkt)) return false;
                break;
            case VideoSourceType::LipstickCapture:
            case VideoSourceType::Test:
                if (!readVideoFrameFromBuf(pkt)) return false;
                if (!encodeVideoFrame(pkt)) return false;
                break;
            default:
                throw std::runtime_error("unknown video source type");
        }

        if (pkt->flags & AV_PKT_FLAG_CORRUPT) {
            av_packet_unref(pkt);
            LOGW("corrupted pkt detected");
            return false;
        }

        if (pkt->flags & AV_PKT_FLAG_DISCARD) {
            av_packet_unref(pkt);
            LOGW("discarded pkt detected");
            return false;
        }

        if (pkt->flags & AV_PKT_FLAG_KEY) {
            if (m_keyVideoPkt == nullptr) {
                m_keyVideoPkt = av_packet_alloc();
                if (auto ret = av_packet_ref(m_keyVideoPkt, pkt); ret != 0) {
                    av_packet_unref(pkt);
                    throw std::runtime_error("av_packet_ref keypkt error");
                }
            }
        }
    }

    updateVideoSampleStats(now);

    LOGT("video: frd=" << m_videoRealFrameDuration << ", npts="
                       << m_nextVideoPts << ", lft=" << m_videoTimeLastFrame
                       << ", os_tb=" << m_outVideoStream->time_base);

    pkt->stream_index = m_outVideoStream->index;
    pkt->pts = m_nextVideoPts;
    pkt->dts = m_nextVideoPts;
    pkt->duration =
        rescaleFromUsec(m_videoRealFrameDuration, m_outVideoStream->time_base);
    m_nextVideoPts += pkt->duration;

    if (auto ret = av_write_frame(m_outFormatCtx, pkt); ret < 0) {
        throw std::runtime_error("av_interleaved_write_frame for video error");
    }

    av_packet_unref(pkt);

    if (!m_videoFlushed) {
        LOGD("first av video data");
        m_videoFlushed = true;
    }

    return true;
}

int64_t Caster::videoAudioDelay() const {
    auto video_pts = rescaleToUsec(m_nextVideoPts, m_outVideoStream->time_base);
    auto audio_pts = rescaleToUsec(m_nextAudioPts, m_outAudioStream->time_base);
    return video_pts - audio_pts;
}

int64_t Caster::videoDelay(int64_t now) const {
    if (m_videoTimeLastFrame == 0) return m_videoRealFrameDuration;
    return now - (m_videoTimeLastFrame + m_videoRealFrameDuration);
}

int64_t Caster::audioDelay(int64_t now) const {
    if (m_audioTimeLastFrame == 0) return m_audioFrameDuration;
    return now - (m_audioTimeLastFrame + m_audioFrameDuration);
}

bool Caster::readRawAudioPkt(AVPacket *pkt, int64_t now) {
    const auto maxAudioDelay = 2 * m_audioFrameDuration;
    const auto delay = videoEnabled() ? videoAudioDelay() : audioDelay(now);

    LOGT("audio: delay=" << delay
                         << ", audio frame dur=" << m_audioFrameDuration
                         << ", audio buf size=" << m_audioBuf.size());
    if (delay < -maxAudioDelay) {
        LOGD("too much audio, deleting audio frame: delay=" << delay);
        std::lock_guard lock{m_audioMtx};
        m_audioBuf.discardExact(m_audioFrameSize);
        return false;
    }

    if (delay < m_audioFrameDuration) return false;

    std::lock_guard lock{m_audioMtx};

    if (!m_audioBuf.hasEnoughData(m_audioFrameSize)) {
        const auto pushNull =
            m_paStream == nullptr || (delay > maxAudioDelay) || audioMuted();

        if (pushNull) {
            LOGT("audio push null: " << (m_audioFrameSize - m_audioBuf.size()));
            m_audioBuf.pushNullExactForce(m_audioFrameSize - m_audioBuf.size());
        } else {
            return false;
        }
    }

    if (av_new_packet(pkt, m_audioFrameSize) < 0) {
        throw std::runtime_error("av_new_packet for audio error");
    }

    m_audioBuf.pull(pkt->data, m_audioFrameSize);

    if (audioBoosted()) {
        const auto &props = audioProps();
        if (props.bps == 1)
            changeAudioVolume<1>(props.endian, pkt);
        else if (props.bps == 2)
            changeAudioVolume<2>(props.endian, pkt);
        else if (props.bps == 4)
            changeAudioVolume<4>(props.endian, pkt);
    }

    return true;
}

bool Caster::muxAudio(AVPacket *pkt) {
    bool pktDone = false;

    while (!terminating()) {
        auto now = av_gettime();

        if (!readRawAudioPkt(pkt, now)) break;

        if (auto ret = avcodec_send_packet(m_inAudioCtx, pkt);
            ret != 0 && ret != AVERROR(EAGAIN)) {
            throw std::runtime_error("avcodec_send_packet for audio error");
        }

        if (auto ret = avcodec_receive_frame(m_inAudioCtx, m_audioFrameIn);
            ret != 0) {
            throw std::runtime_error("avcodec_receive_frame for audio error");
        }

        if (m_audioSwrCtx == nullptr) {  // resampling not needed
            m_audioFrameIn->ch_layout = m_outAudioCtx->ch_layout;
            m_audioFrameIn->format = m_outAudioCtx->sample_fmt;
            m_audioFrameIn->sample_rate = m_outAudioCtx->sample_rate;

            if (auto ret = avcodec_send_frame(m_outAudioCtx, m_audioFrameIn);
                ret != 0 && ret != AVERROR(EAGAIN)) {
                throw std::runtime_error("avcodec_send_frame for audio error");
            }
        } else {  // resampling required
            m_audioFrameOut->ch_layout = m_outAudioCtx->ch_layout;
            m_audioFrameOut->format = m_outAudioCtx->sample_fmt;
            m_audioFrameOut->sample_rate = m_outAudioCtx->sample_rate;

            if (swr_convert_frame(m_audioSwrCtx, m_audioFrameOut,
                                  m_audioFrameIn) != 0) {
                throw std::runtime_error("swr_convert_frame for audio error");
            }

            if (auto ret = avcodec_send_frame(m_outAudioCtx, m_audioFrameOut);
                ret != 0 && ret != AVERROR(EAGAIN)) {
                throw std::runtime_error("avcodec_send_frame for audio error");
            }
        }

        if (auto ret = avcodec_receive_packet(m_outAudioCtx, pkt); ret != 0) {
            if (ret == AVERROR(EAGAIN)) {
                LOGD("audio pkt not ready");
                break;
            }

            throw std::runtime_error("avcodec_receive_packet for audio error");
        }

        pkt->stream_index = m_outAudioStream->index;
        pkt->pts = m_nextAudioPts;
        pkt->dts = m_nextAudioPts;
        pkt->duration = m_audioPktDuration;
        m_nextAudioPts += pkt->duration;

        if (pkt->pts == 0)
            m_audioTimeLastFrame = now;
        else
            m_audioTimeLastFrame += m_audioFrameDuration;

        if (auto ret = av_write_frame(m_outFormatCtx, pkt); ret < 0) {
            throw std::runtime_error(
                "av_interleaved_write_frame for audio error");
        }

        LOGT("audio real frame dur: " << now - m_audioTimeLastFrame);

        av_packet_unref(pkt);

        if (!m_audioFlushed) {
            LOGD("first av audio data");
            m_audioFlushed = true;
        }

        pktDone = true;
    }

    return pktDone;
}

std::string Caster::strForAvError(int err) {
    char str[AV_ERROR_MAX_STRING_SIZE];

    if (av_strerror(err, str, AV_ERROR_MAX_STRING_SIZE) < 0) {
        return std::to_string(err);
    }

    return std::string(str);
}

int Caster::avWritePacketCallbackStatic(void *opaque, uint8_t *buf,
                                        int bufSize) {
    return static_cast<Caster *>(opaque)->avWritePacketCallback(buf, bufSize);
}

int Caster::avWritePacketCallback(uint8_t *buf, int bufSize) {
    if (bufSize < 0)
        throw std::runtime_error("invalid read packet callback buf size");

    LOGT("write packet: size=" << bufSize
                               << ", data=" << dataToStr(buf, bufSize));

    if (!terminating() && m_dataReadyHandler) {
        if (!m_muxedFlushed && m_avMuxingThread.joinable()) {
            LOGD("first av muxed data");
            m_muxedFlushed = true;
        }
        return static_cast<int>(m_dataReadyHandler(buf, bufSize));
    }

    return bufSize;
}

int Caster::avReadPacketCallbackStatic(void *opaque, uint8_t *buf,
                                       int bufSize) {
    return static_cast<Caster *>(opaque)->avReadPacketCallback(buf, bufSize);
}

int Caster::avReadPacketCallback(uint8_t *buf, int bufSize) {
    if (bufSize < 0)
        throw std::runtime_error("invalid read_packet_callback buf size");

    LOGT("read packet: request");

    std::unique_lock lock{m_videoMtx};
    m_videoCv.wait(lock, [this] {
        return terminating() || m_restartRequested || m_restarting ||
               !m_videoBuf.empty();
    });

    if (terminating()) {
        m_videoBuf.clear();
        lock.unlock();
        m_videoCv.notify_one();
        LOGT("read packet: terminating");
        return AVERROR_EOF;
    }

    if (m_restartRequested || m_restarting) {
        lock.unlock();
        m_videoCv.notify_one();
        LOGT("read packet: restart");
        return AVERROR_EOF;
    }

    auto pulledSize = m_videoBuf.pull(buf, bufSize);

    LOGT("read packet: done, size=" << pulledSize
                                    << ", data=" + dataToStr(buf, pulledSize));

    lock.unlock();
    m_videoCv.notify_one();

    return static_cast<decltype(bufSize)>(pulledSize);
}

void Caster::updateVideoSampleStats(int64_t now) {
    if (m_videoTimeLastFrame > 0) {
        auto lastDur = now - m_videoTimeLastFrame;
        if (lastDur >= m_videoRealFrameDuration / 4)
            m_videoRealFrameDuration = lastDur;
    }
    m_videoTimeLastFrame = now;
}

uint32_t Caster::hash(std::string_view str) {
    return std::accumulate(str.cbegin(), str.cend(), 0U) % 999;
}

Caster::VideoPropsMap Caster::detectVideoSources() {
    avdevice_register_all();

    VideoPropsMap props;
#ifdef USE_DROIDCAM
    props.merge(detectDroidCamVideoSources());
#endif
#ifdef USE_V4L2
    props.merge(detectV4l2VideoSources());
#endif
#ifdef USE_X11CAPTURE
    props.merge(detectX11VideoSources());
#endif
#ifdef USE_LIPSTICK_RECORDER
    props.merge(detectLipstickRecorderVideoSources());
#endif
#ifdef USE_TESTSOURCE
    props.merge(detectTestVideoSources());
#endif
    return props;
}

void Caster::switchVideoDirection() {
    const auto &props = videoProps();

    auto it =
        std::find_if(m_videoProps.cbegin(), m_videoProps.cend(),
                     [dir = (props.sensorDirection == SensorDirection::Front
                                 ? SensorDirection::Back
                                 : SensorDirection::Front)](const auto &p) {
                         return p.second.sensorDirection == dir;
                     });

    if (it == m_videoProps.cend()) {
        LOGW("failed to change video direction");
        return;
    }

    LOGD("video direction change: "
         << (props.sensorDirection == SensorDirection::Back ? "front => back"
                                                            : "back => front"));

    m_config.videoSource = props.name;

    restartVideoCapture();
}

Caster::SensorDirection Caster::videoDirection() const {
    return videoProps().sensorDirection;
}

Caster::VideoPropsMap Caster::detectTestVideoSources() {
    LOGD("test video source detecton started");
    VideoPropsMap map;

    if (TestSource::supported()) {
        auto iprops = TestSource::properties();

        {
            VideoSourceInternalProps props;
            props.type = VideoSourceType::Test;
            props.formats.push_back(
                {AV_CODEC_ID_RAWVIDEO,
                 iprops.pixfmt,
                 {{{iprops.width, iprops.height}, {iprops.framerate}}}});
            props.name = "test";
            props.friendlyName = "Test";
            props.orientation = iprops.width < iprops.height
                                    ? VideoOrientation::Portrait
                                    : VideoOrientation::Landscape;

            LOGD("test source found: " << props);

            map.try_emplace(props.name, std::move(props));
        }

        {
            VideoSourceInternalProps props;
            props.type = VideoSourceType::Test;
            props.formats.push_back(
                {AV_CODEC_ID_RAWVIDEO,
                 iprops.pixfmt,
                 {{{iprops.width, iprops.height}, {iprops.framerate}}}});
            props.name = "test-rotate";
            props.friendlyName = "Test, auto rotate";
            props.trans = VideoTrans::Frame169;
            props.orientation = VideoOrientation::Landscape;

            LOGD("test source found: " << props);

            map.try_emplace(props.name, std::move(props));
        }
    };

    LOGD("test video source detecton completed");

    return map;
}

void Caster::rawDataReadyCallback(const uint8_t *data, size_t size) {
    if (terminating()) return;

    std::lock_guard lock{m_videoMtx};

    m_videoBuf.pushExactForce(data, size);
}

#ifdef USE_X11CAPTURE
static std::vector<AVPixelFormat> x11Pixfmts(Display *dpy) {
    std::vector<AVPixelFormat> fmts;
    auto bo = BitmapBitOrder(dpy);
    int n = 0;
    auto *pmf = XListPixmapFormats(dpy, &n);
    if (pmf) {
        fmts.reserve(n);
        for (int i = 0; i < n; ++i) {
            auto pixfmt = ff_tools::ff_fmt_x112ff(bo, pmf[i].depth,
                                                  pmf[i].bits_per_pixel);
            if (pixfmt != AV_PIX_FMT_NONE) fmts.push_back(pixfmt);
        }
        XFree(pmf);
    }
    return fmts;
}

Caster::VideoPropsMap Caster::detectX11VideoSources() {
    LOGD("x11 source detecton started");

    VideoPropsMap map;

    if (av_find_input_format("x11grab") == nullptr) return map;

    auto *dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) return map;

    if (DisplayString(dpy) == nullptr) {
        LOGW("x11 display string is null");
        XCloseDisplay(dpy);
        return map;
    }

    auto pixfmts = x11Pixfmts(dpy);

    int count = ScreenCount(dpy);

    LOGD("x11 screen count: " << count);

    for (int i = 0; i < count; ++i) {
        VideoSourceInternalProps props;
        props.type = VideoSourceType::X11Capture;
        props.name = fmt::format("screen-{}", i + 1);
        props.friendlyName = fmt::format("Screen {} capture", i + 1);
        props.dev = fmt::format("{}.{}", DisplayString(dpy), i);

        FrameSpec fs{Dim{static_cast<uint32_t>(XDisplayWidth(dpy, i)),
                         static_cast<uint32_t>(XDisplayHeight(dpy, i))},
                     {30}};

        props.orientation = fs.dim.orientation();

        for (auto pixfmt : pixfmts) {
            props.formats.push_back(
                VideoFormatExt{AV_CODEC_ID_RAWVIDEO, pixfmt, {fs}});
        }

        LOGD("x11 source found: " << props);

        map.try_emplace(props.name, std::move(props));
    }

    XCloseDisplay(dpy);

    LOGD("x11 source detecton completed");

    return map;
}
#endif  // USE_X11CAPTURE

#ifdef USE_DROIDCAM
int Caster::droidCamProp(GstElement *source, const char *name) {
    int val = -1;
    g_object_get(source, name, &val, NULL);
    return val;
}

bool Caster::setDroidCamProp(GstElement *source, const char *name, int value) {
    g_object_set(source, name, value, NULL);
    return droidCamProp(source, name) == value;
}

Caster::VideoPropsMap Caster::detectDroidCamProps(GstElement *source) {
    auto makeProps = [source](int dir) {
        int cd = droidCamProp(source, "camera-device");

        VideoSourceInternalProps props;
        props.type = VideoSourceType::DroidCam;
        props.dev = std::to_string(cd);
        props.formats.push_back(
            {AV_CODEC_ID_H264, AV_PIX_FMT_YUVJ420P, {{{1280, 720}, {30}}}});

        /* read "sensor-direction" or "sensor-orientation" sometimes causes
         * crash, so assuming cam=0 dir=0, cam=1 dir=1 */
        // if (droidCamProp(source, "sensor-direction") == 0) {
        if (dir == 0) {
            props.sensorDirection = SensorDirection::Back;
            props.name = "back";
            props.friendlyName = "Back camera";
            props.orientation = VideoOrientation::Landscape;
        } else {
            props.sensorDirection = SensorDirection::Front;
            props.name = "front";
            props.friendlyName = "Front camera";
            props.orientation = VideoOrientation::InvertedLandscape;
        }

        LOGD("droid cam found: " << props);
        return props;
    };

    auto dev = droidCamProp(source, "camera-device");

    VideoPropsMap props;

    if (setDroidCamProp(source, "camera-device", 0)) {
        auto p = makeProps(0);
        props.try_emplace(p.name, std::move(p));
    } else {
        LOGW("no droid camera-device 0");
    }
    if (setDroidCamProp(source, "camera-device", 1)) {
        auto p = makeProps(1);
        props.try_emplace(p.name, std::move(p));
    } else {
        LOGW("no droid camera-device 1");
    }

    setDroidCamProp(source, "camera-device", dev);

    return props;
}

void Caster::initGstLib() {
    if (GError *err = nullptr;
        gst_init_check(nullptr, nullptr, &err) == FALSE) {
        throw std::runtime_error("gst_init error: " +
                                 std::to_string(err->code));
    }

    gst_debug_set_active(Logger::match(Logger::LogType::Debug) ? TRUE : FALSE);
}

Caster::VideoPropsMap Caster::detectDroidCamVideoSources() {
    LOGD("droid cam detection started");

    initGstLib();

    decltype(detectDroidCamVideoSources()) props;

    auto *source_factory = gst_element_factory_find("droidcamsrc");
    if (source_factory == nullptr) {
        LOGE("no droidcamsrc");
        return props;
    }
    auto *source =
        gst_element_factory_create(source_factory, "app_camera_source");
    if (source == nullptr) {
        g_object_unref(source_factory);
        LOGE("failed to create droidcamsrc");
        return props;
    }

    props = detectDroidCamProps(source);

    g_object_unref(source);
    g_object_unref(source_factory);

    LOGD("droid cam detection completed");

    return props;
}

void Caster::initGst() {
    LOGD("gst init started");

    auto *source_factory = gst_element_factory_find("droidcamsrc");
    if (source_factory == nullptr) {
        throw std::runtime_error("no droidcamsrc");
    }
    m_gstPipe.source =
        gst_element_factory_create(source_factory, "app_camera_source");
    if (m_gstPipe.source == nullptr) {
        g_object_unref(source_factory);
        throw std::runtime_error("failed to create droidcamsrc");
    }
    g_object_unref(source_factory);

    auto *sink_factory = gst_element_factory_find("appsink");
    if (sink_factory == nullptr) {
        g_object_unref(source_factory);
        throw std::runtime_error("no appsink");
    }
    m_gstPipe.sink = gst_element_factory_create(sink_factory, "app_sink");
    if (m_gstPipe.sink == nullptr) {
        g_object_unref(sink_factory);
        g_object_unref(source_factory);
        throw std::runtime_error("failed to create droidcamsrc");
    }
    gst_base_sink_set_async_enabled(GST_BASE_SINK(m_gstPipe.sink), FALSE);
    g_object_set(m_gstPipe.sink, "sync", TRUE, NULL);
    g_object_set(m_gstPipe.sink, "emit-signals", TRUE, NULL);
    g_signal_connect(m_gstPipe.sink, "new-sample",
                     G_CALLBACK(gstNewSampleCallbackStatic), this);
    g_object_unref(sink_factory);

    m_gstPipe.pipeline = gst_pipeline_new("app_bin");
    if (m_gstPipe.pipeline == nullptr) {
        g_object_unref(sink_factory);
        g_object_unref(source_factory);
        throw std::runtime_error("failed to create pipeline");
    }

    setDroidCamProp(m_gstPipe.source, "mode", /*video recording*/ 2);

    const auto &camDev = videoProps();

    setDroidCamProp(m_gstPipe.source, "camera-device", std::stoi(camDev.dev));

    auto *fake_vid_sink =
        gst_element_factory_make("fakesink", "app_fake_vid_sink");
    g_object_set(fake_vid_sink, "sync", FALSE, NULL);
    gst_base_sink_set_async_enabled(GST_BASE_SINK(fake_vid_sink), FALSE);

    auto *queue = gst_element_factory_make("queue", "app_queue");

    auto *h264parse = gst_element_factory_make("h264parse", "app_h264parse");
    g_object_set(h264parse, "disable-passthrough", TRUE, NULL);

    auto dim = camDev.formats.front().frameSpecs.front().dim;
    auto *capsfilter = gst_element_factory_make("capsfilter", "app_capsfilter");
    auto *mpeg_caps = gst_caps_new_simple(
        "video/x-h264", "stream-format", G_TYPE_STRING, "byte-stream",
        "alignment", G_TYPE_STRING, "au", "width", G_TYPE_INT, dim.width,
        "height", G_TYPE_INT, dim.height, NULL);
    g_object_set(GST_OBJECT(capsfilter), "caps", mpeg_caps, NULL);
    gst_caps_unref(mpeg_caps);

    gst_bin_add_many(GST_BIN(m_gstPipe.pipeline), m_gstPipe.source, capsfilter,
                     h264parse, queue, m_gstPipe.sink, fake_vid_sink, NULL);

    try {
        if (!gst_element_link_pads(m_gstPipe.source, "vidsrc", h264parse,
                                   "sink")) {
            throw std::runtime_error("unable to link vidsrc pad");
        }

        if (!gst_element_link_pads(m_gstPipe.source, "vfsrc", fake_vid_sink,
                                   "sink")) {
            throw std::runtime_error("unable to link vfsrc pad");
        }

        if (!gst_element_link_many(h264parse, capsfilter, queue, m_gstPipe.sink,
                                   NULL)) {
            throw std::runtime_error("unable to link many");
        }
    } catch (const std::runtime_error &err) {
        gst_object_unref(capsfilter);
        gst_object_unref(h264parse);
        gst_object_unref(queue);
        gst_object_unref(fake_vid_sink);
        throw;
    }

    LOGD("gst init completed");
}
void Caster::startDroidCamCapture() const {
    LOGD("starting video capture");
    g_signal_emit_by_name(m_gstPipe.source, "start-capture", NULL);
}

void Caster::stopDroidCamCapture() const {
    LOGD("stopping video capture");
    g_signal_emit_by_name(m_gstPipe.source, "stop-capture", NULL);
}

void Caster::cleanGst() {
    if (m_gstPipe.pipeline != nullptr) {
        gst_element_set_state(m_gstPipe.pipeline, GST_STATE_NULL);
        gst_object_unref(m_gstPipe.pipeline);
        m_gstPipe.pipeline = nullptr;
    } else {
        if (m_gstPipe.source != nullptr) {
            gst_object_unref(m_gstPipe.source);
            m_gstPipe.source = nullptr;
        }
        if (m_gstPipe.sink != nullptr) {
            gst_object_unref(m_gstPipe.sink);
            m_gstPipe.sink = nullptr;
        }
    }
}

void Caster::startGst() {
    LOGD("starting gst");

    if (auto ret = gst_element_set_state(m_gstPipe.pipeline, GST_STATE_PLAYING);
        ret == GST_STATE_CHANGE_FAILURE) {
        throw std::runtime_error(
            "unable to set the pipeline to the playing state");
    }

    LOGD("gst start completed");
    startGstThread();
}

static auto gstStateChangeFromMsg(GstMessage *msg) {
    GstState os, ns, ps;
    gst_message_parse_state_changed(msg, &os, &ns, &ps);

    auto *name = gst_element_get_name(GST_MESSAGE_SRC(msg));

    LOGD("gst state changed ("
         << name << "): " << gst_element_state_get_name(os) << " -> "
         << gst_element_state_get_name(ns) << " ("
         << gst_element_state_get_name(ps) << ")");

    g_free(name);

    return std::array{os, ns, ps};
}

void Caster::doGstIteration() {
    auto *msg = gst_bus_timed_pop_filtered(
        gst_element_get_bus(m_gstPipe.pipeline), m_gsPipelineTickTime,
        GstMessageType(GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR |
                       GST_MESSAGE_EOS));

    if (msg == nullptr) return;

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr;
            gchar *debug_info = nullptr;
            gst_message_parse_error(msg, &err, &debug_info);

            std::ostringstream os;
            os << "error received from element " << GST_OBJECT_NAME(msg->src)
               << " " << err->message;

            g_clear_error(&err);
            g_free(debug_info);
            gst_message_unref(msg);

            throw std::runtime_error(os.str());
        }
        case GST_MESSAGE_EOS:
            gst_message_unref(msg);
            throw std::runtime_error("end-of-stream reached");
        case GST_MESSAGE_STATE_CHANGED:
            if (auto states = gstStateChangeFromMsg(msg);
                GST_MESSAGE_SRC(msg) == GST_OBJECT(m_gstPipe.pipeline)) {
                if (!terminating() && states[1] == GST_STATE_PAUSED &&
                    states[2] == GST_STATE_PLAYING &&
                    droidCamProp(m_gstPipe.source, "ready-for-capture") == 1) {
                    startDroidCamCapture();
                }
            }
            break;
        default:
            LOGW("unexpected gst message received");
            break;
    }

    gst_message_unref(msg);
}

void Caster::restartGst() {
    LOGD("restarting gst");
    try {
        cleanGst();
        initGst();
        startGst();
    } catch (const std::runtime_error &e) {
        LOGE("failed to restart gst: " << e.what());
        reportError();
    }
}

void Caster::startGstThread() {
    if (m_gstThread.joinable()) {
        m_gstThread.detach();
    }
    m_gstThread = std::thread([this] {
        LOGD("staring gst pipeline");

        if (m_restartRequested) {
            m_restartRequested = false;
            m_restarting = true;
        }

        try {
            while (!terminating() && !m_restartRequested) {
                doGstIteration();
            }
        } catch (const std::runtime_error &e) {
            LOGE("error in gst pipeline thread: " << e.what());
        }

        stopDroidCamCapture();

        if (m_restartRequested) {
            restartGst();
        } else {
            reportError();
        }

        LOGD("gst pipeline ended");
    });
}
GstFlowReturn Caster::gstNewSampleCallbackStatic(GstElement *element,
                                                 gpointer udata) {
    return static_cast<Caster *>(udata)->gstNewSampleCallback(element);
}

GstFlowReturn Caster::gstNewSampleCallback(GstElement *element) {
    GstSample *sample =
        gst_app_sink_pull_sample(reinterpret_cast<GstAppSink *>(element));
    if (sample == nullptr) {
        LOGW("sample is null");
        return GST_FLOW_OK;
    }

    GstBuffer *sample_buf = gst_sample_get_buffer(sample);
    if (sample_buf == nullptr) {
        LOGW("sample buf is null");
        return GST_FLOW_OK;
    }

    GstMapInfo info;
    if (!gst_buffer_map(sample_buf, &info, GST_MAP_READ)) {
        LOGW("gst buffer map error");
        return GST_FLOW_OK;
    }

    LOGT("new gst video sample");

    if (info.size == 0) {
        gst_buffer_unmap(sample_buf, &info);
        gst_sample_unref(sample);
        LOGW("sample size is zero");
        return GST_FLOW_ERROR;
    }

    GstFlowReturn ret = GST_FLOW_OK;

    if (m_restarting) m_restarting = false;

    std::unique_lock lock{m_videoMtx};
    m_videoCv.wait(lock, [this, sample_size = info.size] {
        return terminating() || m_restartRequested ||
               m_videoBuf.hasFreeSpace(sample_size);
    });

    if (terminating()) {
        m_videoBuf.clear();
        ret = GST_FLOW_EOS;
    } else if (m_restartRequested) {
        ret = GST_FLOW_EOS;
    } else {
        m_videoBuf.pushExactForce(info.data, info.size);
    }

    if (!m_avMuxingThread.joinable()) /* video muxing not started yet */ {
        updateVideoSampleStats(av_gettime());
    }

    LOGT("new sample written: ret=" << ret);

    lock.unlock();
    m_videoCv.notify_one();

    gst_buffer_unmap(sample_buf, &info);
    gst_sample_unref(sample);

    return ret;
}

[[maybe_unused]] GHashTable *Caster::getDroidCamDevTable() const {
    GHashTable *table = nullptr;

    g_object_get(m_gstPipe.source, "device-parameters", &table, NULL);

    if (table == nullptr) LOGW("failed to get device parameters table");

    return table;
}

[[maybe_unused]] std::optional<std::string> Caster::readDroidCamDevParam(
    const std::string &key) const {
    auto *params = getDroidCamDevTable();
    if (params == nullptr) return std::nullopt;

    auto *value = static_cast<char *>(g_hash_table_lookup(params, key.data()));

    if (value == nullptr) return std::nullopt;

    return std::make_optional<std::string>(value);
}
#endif  // USE_DROIDCAM

#ifdef USE_V4L2
static inline auto isV4lDev(const char *name) {
    return av_strstart(name, "video", nullptr);
}

static std::optional<std::string> readLinkTarget(const std::string &file) {
    char link[64];
    auto linkLen = readlink(file.c_str(), link, sizeof(link));

    if (linkLen < 0) return std::nullopt;  // not a link

    std::string target;
    if (link[0] != '/') target.assign("/dev/");
    target += std::string(link, linkLen);

    return target;
}

static auto v4lDevFiles() {
    std::vector<std::string> files;

    auto *dir = opendir("/dev");
    if (dir == nullptr) throw std::runtime_error("failed to open /dev dir");

    while (auto *entry = readdir(dir)) {
        if (isV4lDev(entry->d_name)) {
            files.push_back("/dev/"s + entry->d_name);
            if (files.size() > 1000) break;
        }
    }

    closedir(dir);

    for (auto it = files.begin(); it != files.end();) {
        auto target = readLinkTarget(*it);
        if (!target) {
            ++it;
            continue;
        }

        if (std::find(files.cbegin(), files.cend(), *target) == files.end()) {
            ++it;
            continue;
        }

        it = files.erase(it);
    }

    std::sort(files.begin(), files.end());

    return files;
}

std::vector<Caster::FrameSpec> Caster::detectV4l2FrameSpecs(
    int fd, uint32_t pixelformat) {
    std::vector<FrameSpec> specs;

    v4l2_frmsizeenum vfse{};
    vfse.type = V4L2_FRMIVAL_TYPE_DISCRETE;
    vfse.pixel_format = pixelformat;

    for (int i = 0; i < m_maxIters && !ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &vfse);
         ++i) {
        if (vfse.type != V4L2_FRMSIZE_TYPE_DISCRETE) break;

        v4l2_frmivalenum vfie{};
        vfie.type = V4L2_FRMIVAL_TYPE_DISCRETE;
        vfie.pixel_format = pixelformat;
        vfie.height = vfse.discrete.height;
        vfie.width = vfse.discrete.width;

        FrameSpec spec;
        for (int ii = 0;
             ii < m_maxIters && !ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &vfie);
             ++ii) {
            if (vfie.type != V4L2_FRMIVAL_TYPE_DISCRETE) break;

            if (vfie.discrete.numerator == 1)
                spec.framerates.insert(vfie.discrete.denominator);

            vfie.index++;
        }

        if (!spec.framerates.empty()) {
            spec.dim = {vfse.discrete.width, vfse.discrete.height};
            specs.push_back(std::move(spec));
        }

        vfse.index++;
    }

    std::sort(specs.begin(), specs.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.dim > rhs.dim;
    });

    return specs;
}

void Caster::addV4l2VideoFormats(int fd, v4l2_buf_type type,
                                 std::vector<VideoFormat> &formats) {
    v4l2_fmtdesc vfmt{};
    vfmt.type = type;

    for (int i = 0; i < m_maxIters && !ioctl(fd, VIDIOC_ENUM_FMT, &vfmt); ++i) {
        if (vfmt.type == 0) break;

        vfmt.index++;

        auto c = ff_tools::ff_fmt_v4l2codec(vfmt.pixelformat);
        if (c == AV_CODEC_ID_NONE) continue;

        formats.push_back({c, ff_tools::ff_fmt_v4l2ff(vfmt.pixelformat, c)});
    }
}

void Caster::addV4l2VideoFormatsExt(int fd, v4l2_buf_type type,
                                    std::vector<VideoFormatExt> &formats) {
    v4l2_fmtdesc vfmt{};
    vfmt.type = type;

    for (int i = 0; i < m_maxIters && !ioctl(fd, VIDIOC_ENUM_FMT, &vfmt); ++i) {
        if (vfmt.type == 0) break;

        vfmt.index++;

        auto c = ff_tools::ff_fmt_v4l2codec(vfmt.pixelformat);
        if (c == AV_CODEC_ID_NONE) continue;

        auto pf = ff_tools::ff_fmt_v4l2ff(vfmt.pixelformat, c);
        if (pf == AV_PIX_FMT_NONE) continue;

        auto fs = detectV4l2FrameSpecs(fd, vfmt.pixelformat);
        if (fs.empty()) continue;

        formats.push_back({c, pf, std::move(fs)});
    }
}

static auto v4l2Caps(uint32_t caps) {
    std::ostringstream os;
    if (caps & V4L2_CAP_VIDEO_CAPTURE) os << "V4L2_CAP_VIDEO_CAPTURE, ";
    if (caps & V4L2_CAP_VIDEO_OUTPUT) os << "V4L2_CAP_VIDEO_OUTPUT, ";
    if (caps & V4L2_CAP_VIDEO_OVERLAY) os << "V4L2_CAP_VIDEO_OVERLAY, ";
    if (caps & V4L2_CAP_VBI_CAPTURE) os << "V4L2_CAP_VBI_CAPTURE, ";
    if (caps & V4L2_CAP_VBI_OUTPUT) os << "V4L2_CAP_VBI_OUTPUT, ";
    if (caps & V4L2_CAP_SLICED_VBI_CAPTURE)
        os << "V4L2_CAP_SLICED_VBI_CAPTURE, ";
    if (caps & V4L2_CAP_SLICED_VBI_OUTPUT) os << "V4L2_CAP_SLICED_VBI_OUTPUT, ";
    if (caps & V4L2_CAP_RDS_CAPTURE) os << "V4L2_CAP_RDS_CAPTURE, ";
    if (caps & V4L2_CAP_VIDEO_OUTPUT_OVERLAY)
        os << "V4L2_CAP_VIDEO_OUTPUT_OVERLAY, ";
    if (caps & V4L2_CAP_HW_FREQ_SEEK) os << "V4L2_CAP_HW_FREQ_SEEK, ";
    if (caps & V4L2_CAP_RDS_OUTPUT) os << "V4L2_CAP_RDS_OUTPUT, ";
    if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        os << "V4L2_CAP_VIDEO_CAPTURE_MPLANE, ";
    if (caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE)
        os << "V4L2_CAP_VIDEO_OUTPUT_MPLANE, ";
    if (caps & V4L2_CAP_VIDEO_M2M_MPLANE) os << "V4L2_CAP_VIDEO_M2M_MPLANE, ";
    if (caps & V4L2_CAP_VIDEO_M2M) os << "V4L2_CAP_VIDEO_M2M, ";
    if (caps & V4L2_CAP_TUNER) os << "V4L2_CAP_TUNER, ";
    if (caps & V4L2_CAP_AUDIO) os << "V4L2_CAP_AUDIO, ";
    if (caps & V4L2_CAP_RADIO) os << "V4L2_CAP_RADIO, ";
    if (caps & V4L2_CAP_MODULATOR) os << "V4L2_CAP_MODULATOR, ";
    if (caps & V4L2_CAP_SDR_CAPTURE) os << "V4L2_CAP_SDR_CAPTURE, ";
    if (caps & V4L2_CAP_EXT_PIX_FORMAT) os << "V4L2_CAP_EXT_PIX_FORMAT, ";
    if (caps & V4L2_CAP_SDR_OUTPUT) os << "V4L2_CAP_SDR_OUTPUT, ";
    if (caps & V4L2_CAP_META_CAPTURE) os << "V4L2_CAP_META_CAPTURE, ";
    if (caps & V4L2_CAP_READWRITE) os << "V4L2_CAP_READWRITE, ";
    if (caps & V4L2_CAP_ASYNCIO) os << "V4L2_CAP_ASYNCIO, ";
    if (caps & V4L2_CAP_STREAMING) os << "V4L2_CAP_STREAMING, ";
    if (caps & V4L2_CAP_META_OUTPUT) os << "V4L2_CAP_META_OUTPUT, ";
    if (caps & V4L2_CAP_TOUCH) os << "V4L2_CAP_TOUCH, ";
    if (caps & V4L2_CAP_IO_MC) os << "V4L2_CAP_IO_MC, ";
    if (caps & V4L2_CAP_DEVICE_CAPS) os << "V4L2_CAP_DEVICE_CAPS, ";
    return os.str();
}

static bool mightBeV4l2m2mEncoder(uint32_t caps) {
    return !(caps & V4L2_CAP_VIDEO_CAPTURE) &&
           !(caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) &&
           (caps & V4L2_CAP_VIDEO_M2M || caps & V4L2_CAP_VIDEO_M2M_MPLANE);
}

static bool mightBeV4l2Cam(uint32_t caps) {
    return (caps & V4L2_CAP_VIDEO_CAPTURE ||
            caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE);
}

Caster::VideoPropsMap Caster::detectV4l2VideoSources() {
    LOGD("v4l2 sources detection started");
    auto files = v4lDevFiles();

    decltype(detectV4l2VideoSources()) cards;  // bus_info => props

    v4l2_capability vcap = {};

    for (auto &file : files) {
        auto fd = open(file.c_str(), O_RDWR);

        if (fd < 0) continue;

        if (ioctl(fd, VIDIOC_QUERYCAP, &vcap) < 0 ||
            !mightBeV4l2Cam(vcap.device_caps)) {
            close(fd);
            continue;
        }

        LOGD("found v4l2 dev: file="
             << file
             << ", card=" << reinterpret_cast<const char *>(vcap.bus_info)
             << ", caps=[" << v4l2Caps(vcap.device_caps) << "]");

        std::vector<VideoFormatExt> outFormats;

        addV4l2VideoFormatsExt(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, outFormats);
        addV4l2VideoFormatsExt(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                               outFormats);

        if (outFormats.empty()) {
            close(fd);
            continue;
        }

        Caster::VideoSourceInternalProps props;
        props.type = VideoSourceType::V4l2;
        props.name = fmt::format(
            "cam-{:03}", hash(reinterpret_cast<const char *>(vcap.card)));
        props.dev = std::move(file);
        props.friendlyName = reinterpret_cast<const char *>(vcap.card);
        props.orientation =
            outFormats.front().frameSpecs.front().dim.orientation();
        props.formats = std::move(outFormats);

        LOGD("v4l2 source found: " << props);

        cards.try_emplace(reinterpret_cast<const char *>(vcap.bus_info),
                          std::move(props));
        close(fd);
    }

    decltype(detectV4l2VideoSources()) cams;

    for (auto &&pair : cards) {
        cams.try_emplace(pair.second.name, std::move(pair.second));
    }

    LOGD("v4l2 sources detection completed");

    return cams;
}

void Caster::detectV4l2Encoders() {
    LOGD("v4l2 encoders detection started");

    auto files = v4lDevFiles();

    v4l2_capability vcap{};

    for (auto &file : files) {
        auto fd = open(file.c_str(), O_RDWR);

        if (fd < 0) continue;

        if (ioctl(fd, VIDIOC_QUERYCAP, &vcap) < 0 ||
            !mightBeV4l2m2mEncoder(vcap.device_caps)) {
            close(fd);
            continue;
        }

        LOGD("found v4l2 dev: file="
             << file
             << ", card=" << reinterpret_cast<const char *>(vcap.bus_info)
             << ", caps=[" << v4l2Caps(vcap.device_caps) << "]");

        std::vector<VideoFormat> outFormats;
        addV4l2VideoFormats(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, outFormats);
        addV4l2VideoFormats(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, outFormats);
        if (outFormats.empty()) {
            LOGD("v4l2 encoder does not support h264");
            close(fd);
            continue;
        }

        std::vector<VideoFormat> formats;
        addV4l2VideoFormats(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT, formats);
        addV4l2VideoFormats(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, formats);

        if (!formats.empty()) {
            auto props = m_v4l2Encoders.emplace_back(
                V4l2H264EncoderProps{file, std::move(formats)});
            LOGD("found v4l2 encoder: " << props);
        }

        close(fd);
    }

    LOGD("v4l2 encoders detection completed");
}

std::pair<std::reference_wrapper<const Caster::VideoFormatExt>, AVPixelFormat>
Caster::bestVideoFormatForV4l2Encoder(const VideoSourceInternalProps &props) {
    if (m_v4l2Encoders.empty()) throw std::runtime_error("no v4l2 encoder");

    auto it = std::find_if(
        props.formats.cbegin(), props.formats.cend(), [&](const auto &sf) {
            return std::any_of(m_v4l2Encoders.cbegin(), m_v4l2Encoders.cend(),
                               [&sf](const auto &e) {
                                   return std::any_of(
                                       e.formats.cbegin(), e.formats.cend(),
                                       [&sf](const auto &ef) {
                                           return sf.codecId == ef.codecId &&
                                                  sf.pixfmt == ef.pixfmt;
                                       });
                               });
        });

    if (it == props.formats.cend()) {
        return {props.formats.front(),
                m_v4l2Encoders.front().formats.front().pixfmt};
    }

    LOGD("pixfmt exact match");

    return {*it, it->pixfmt};
}
#endif  // USE_V4L2
#ifdef USE_LIPSTICK_RECORDER
Caster::VideoPropsMap Caster::detectLipstickRecorderVideoSources() {
    LOGD("lipstick-recorder video source detecton started");
    VideoPropsMap map;

    if (LipstickRecorderSource::supported()) {
        auto lprops = LipstickRecorderSource::properties();

        {
            VideoSourceInternalProps props;
            props.type = VideoSourceType::LipstickCapture;
            props.orientation = VideoOrientation::Portrait;
            props.formats.push_back(
                {AV_CODEC_ID_RAWVIDEO,
                 lprops.pixfmt,
                 {{{lprops.width, lprops.height}, {lprops.framerate}}}});
            props.name = "screen";
            props.friendlyName = "Screen capture";
            props.trans = VideoTrans::Vflip;

            LOGD("lipstick recorder source found: " << props);

            map.try_emplace(props.name, std::move(props));
        }

        {
            VideoSourceInternalProps props;
            props.type = VideoSourceType::LipstickCapture;
            props.orientation = VideoOrientation::Landscape;
            props.formats.push_back(
                {AV_CODEC_ID_RAWVIDEO,
                 lprops.pixfmt,
                 {{{lprops.width, lprops.height}, {lprops.framerate}}}});
            props.name = "screen-rotate";
            props.friendlyName = "Screen capture, auto rotate";
            props.trans = VideoTrans::Frame169;
            props.scale = VideoScale::Down50;

            LOGD("lipstick recorder source found: " << props);

            map.try_emplace(props.name, std::move(props));
        }
    };

    LOGD("lipstick-recorder video source detecton completed");

    return map;
}
#endif
