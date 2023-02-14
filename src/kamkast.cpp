/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "kamkast.hpp"

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <chrono>
#include <functional>

#include "config.h"
#include "logger.hpp"
#include "settings.hpp"
#include "utils.hpp"
#include "webui.h"

using namespace std::literals;

Kamkast::Kamkast(Settings&& settings, [[maybe_unused]] int argc,
                 [[maybe_unused]] char** argv)
    : m_settings{settings} {
    LOGI("kamkast staring, version " << APP_VERSION);
#ifdef USE_SFOS
    if (m_settings.gui)
        m_loop.emplace(
            std::in_place_type_t<SfosGui>{}, argc, argv,
            [this](Event::Pack&& event) { handleEvent(std::move(event)); },
            m_settings);
#endif
    if (!m_loop)
        m_loop.emplace(
            std::in_place_type_t<NoGuiEventLoop>{},
            [this](Event::Pack&& event) { handleEvent(std::move(event)); });
}

Kamkast::~Kamkast() {
    LOGD("kamkast shutdown started");
    try {
        shutdown();
    } catch (const std::exception& e) {
        LOGD(e.what());
    }

    LOGD("kamkast shutdown completed");
}

void Kamkast::enqueueEvent(Event::Pack&& event) {
    std::visit([event = std::move(event)](
                   auto& loop) mutable { loop.enqueue(std::move(event)); },
               *m_loop);
}

void Kamkast::enqueueEvent(Event::Type event) {
    std::visit([event](auto& loop) { loop.enqueue(event); }, *m_loop);
}

void Kamkast::shutdown() {
    std::visit([](auto& loop) { loop.shutdown(); }, *m_loop);
}

void Kamkast::notifyCastingStarted(
    std::optional<HttpServer::ConnectionId> connId) {
    if (!m_server || !m_caster) return;

    logConnection("casting started", m_castingConnId);

    auto client = connId ? m_server->clientAddress(*connId).value_or("unknown")
                         : "unknown";
    LOGD("casting started: client address=" << client);

    std::visit(
        [&](auto& loop) {
            const auto& config = m_caster->config();
            loop.notifyCastingStarted(Event::CastingProps{
                std::move(client), config.videoSource, config.audioSource});
        },
        *m_loop);
}

void Kamkast::notifyCastingEnded() {
    if (!m_server) return;

    logConnection("casting ended");

    LOGD("casting ended");
    std::visit([](auto& loop) { loop.notifyCastingEnded(); }, *m_loop);
}

void Kamkast::notifyServerStarted() {
    if (!m_server) return;

    LOGD("server started");

    std::visit([&](auto& loop) { loop.notifyServerStarted(makeServerProps()); },
               *m_loop);
}

void Kamkast::notifyServerEnded() {
    LOGD("server ended");
    std::visit([](auto& loop) { loop.notifyServerEnded(); }, *m_loop);
}

void Kamkast::logConnection(std::string_view message,
                            std::optional<HttpServer::ConnectionId> connId) {
    if (m_server && (!m_settings.logFile.empty() || m_settings.logRequests)) {
        auto msg = fmt::format(
            "[{:%Y-%m-%d %H:%M:%S}] {}{}\n", std::chrono::system_clock::now(),
            message,
            connId ? fmt::format(
                         " (received from {})",
                         m_server->clientAddress(*connId).value_or("unknown"))
                   : "");
        if (m_settings.logRequests) fmt::print(msg);
        if (!m_settings.logFile.empty()) {
            static std::ofstream file{m_settings.logFile, std::ios::app};
            if (file.good()) file << msg;
        }
    }
}

static bool audioOnlyFormat(Caster::StreamFormat format) {
    switch (format) {
        case Caster::StreamFormat::Mp4:
        case Caster::StreamFormat::MpegTs:
            return false;
        case Caster::StreamFormat::Mp3:
            return true;
    }
    return false;
}

void Kamkast::startCaster(HttpServer::ConnectionId connId,
                          Settings&& settings) {
    try {
        Caster::Config config;
        config.streamAuthor = APP_NAME;
        config.videoSource = settings.videoSourceName;
        config.audioSource = settings.audioSourceName;
        config.audioVolume = settings.audioVolume;
        config.videoEncoder = [&]() {
            if (settings.videoEncoder) {
                switch (*settings.videoEncoder) {
                    case Settings::VideoEncoder::Auto:
                        return Caster::VideoEncoder::Auto;
                    case Settings::VideoEncoder::Nvenc:
                        return Caster::VideoEncoder::Nvenc;
                    case Settings::VideoEncoder::V4l2:
                        return Caster::VideoEncoder::V4l2;
                    case Settings::VideoEncoder::X264:
                        return Caster::VideoEncoder::X264;
                }
            }
            return Caster::VideoEncoder::Auto;
        }();
        config.streamFormat = [&]() {
            if (settings.streamFormat) {
                switch (*settings.streamFormat) {
                    case Settings::StreamFormat::Mp4:
                        return Caster::StreamFormat::Mp4;
                    case Settings::StreamFormat::MpegTs:
                        return Caster::StreamFormat::MpegTs;
                    case Settings::StreamFormat::Mp3:
                        return Caster::StreamFormat::Mp3;
                }
            }
            return Caster::StreamFormat::Mp4;
        }();
        config.videoOrientation = [&]() {
            if (settings.videoOrientation) {
                switch (*settings.videoOrientation) {
                    case Settings::VideoOrientation::Auto:
                        return Caster::VideoOrientation::Auto;
                    case Settings::VideoOrientation::Landscape:
                        return Caster::VideoOrientation::Landscape;
                    case Settings::VideoOrientation::InvertedLandscape:
                        return Caster::VideoOrientation::InvertedLandscape;
                    case Settings::VideoOrientation::Portrait:
                        return Caster::VideoOrientation::Portrait;
                    case Settings::VideoOrientation::InvertedPortrait:
                        return Caster::VideoOrientation::InvertedPortrait;
                }
            }
            return Caster::VideoOrientation::Auto;
        }();

        if (settings.audioSourceMuted)
            config.options |= Caster::OptionsFlags::MuteAudioSource;

        if (audioOnlyFormat(config.streamFormat) &&
            !config.videoSource.empty()) {
            LOGW(
                "stream-format does not support video, so disabling video "
                "source");
            config.videoSource.clear();
        }

        if (!config.videoSource.empty())
            config.options |=
                Caster::OptionsFlags::V4l2VideoSources |
                Caster::OptionsFlags::DroidCamRawVideoSources |
                Caster::OptionsFlags::X11CaptureVideoSources |
                Caster::OptionsFlags::LipstickCaptureVideoSources |
                Caster::OptionsFlags::OnlyNiceVideoFormats;
        if (!config.audioSource.empty())
            config.options |= Caster::OptionsFlags::AllPaAudioSources;

        m_caster.emplace(
            config,
            /* data ready handler */
            [this, connId](const uint8_t* data, size_t size) {
                auto pushedSize = m_server->pushData(connId, data, size);
                if (pushedSize && *pushedSize != size) {
                    throw std::runtime_error("failed to push data to server");
                }
                return size;
            },
            /* state changed handler */
            [this, connId](Caster::State state) {
                if (state == Caster::State::Started) {
                    enqueueEvent({Event::Type::CasterStarted, connId, {}});
                } else if (state == Caster::State::Terminating) {
                    enqueueEvent({Event::Type::CasterEnded, connId, {}});
                    enqueueEvent(Event::Type::StopCaster);
                }
            });
    } catch (const std::runtime_error& e) {
        LOGE("failed to init caster: " << e.what());
        m_server->dropConnection(connId);
        return;
    }

    try {
        m_caster->start();
    } catch (const std::runtime_error& e) {
        LOGE("failed to start caster: " << e.what());
        m_server->dropConnection(connId);
        return;
    }

    m_castingConnId = connId;
}

Kamkast::HttpRequestType Kamkast::determineRequestType(
    const std::string& url) const {
    if (url.find(m_settings.urlPath) == std::string::npos) {
        LOGD("invalid request");
        return HttpRequestType::Invalid;
    }

    if (url == m_settings.urlPath) {
        LOGD("web ui request");
        return HttpRequestType::WebUi;
    }

    if (url == m_settings.urlPath + m_streamUrlPath) {
        LOGD("stream request");
        return HttpRequestType::Stream;
    }

    if (url.rfind(m_settings.urlPath + m_ctrlUrlPath, 0) != std::string::npos) {
        LOGD("ctrl request");
        return HttpRequestType::Ctrl;
    }

    return HttpRequestType::Unknown;
}

void Kamkast::stopCaster() {
    if (m_caster) {
        if (m_castingConnId) m_server->dropConnection(*m_castingConnId);
        m_caster.reset();
        enqueueEvent(Event::Type::CasterEnded);
    }
}

void Kamkast::updateSettingsFromUrlParams(HttpServer::ConnectionId id,
                                          Settings& settings) {
    for (const auto& key : Settings::urlOpts) {
        if (auto value = m_server->queryValue(id, key)) {
            LOGD("request url has param: " << key << "=" << *value);
            settings.updateFromStr(key, *value);
        }
    }
}

int Kamkast::handleWebRequest(
    HttpServer::ConnectionId id,
    std::vector<HttpServer::Header>& responseHeaders) {
    responseHeaders.emplace_back("Content-Type", "text/html");
    m_server->pushData(id, webui);
    return 200;
}

std::string Kamkast::contentType(Settings::StreamFormat format) {
    switch (format) {
        case Settings::StreamFormat::Mp4:
            return "video/mp4";
        case Settings::StreamFormat::MpegTs:
            return "video/MP2T";
        case Settings::StreamFormat::Mp3:
            return "audio/mpeg";
    }

    return {};
}

int Kamkast::handleStreamRequest(
    Settings settings, HttpServer::ConnectionId id,
    std::vector<HttpServer::Header>& responseHeaders) {
    if (!settings.ignoreUrlParams) updateSettingsFromUrlParams(id, settings);

    responseHeaders.reserve(2);
    responseHeaders.emplace_back("Content-Type",
                                 contentType(*settings.streamFormat));
    responseHeaders.emplace_back("Accept-Ranges", "none");

    enqueueEvent(Event::Type::StopCaster);
    enqueueEvent({Event::Type::StartCaster, id, std::move(settings)});

    return 200;
}

int Kamkast::handleCtrlRequest(
    HttpServer::ConnectionId id, const std::string& url,
    std::vector<HttpServer::Header>& responseHeaders) {
    static const char infoStr[] = "/info";
    if (url.rfind(infoStr, m_settings.urlPath.size() + sizeof(infoStr)) ==
        std::string::npos) {
        LOGW("unknown ctrl request");
        return 404;
    }

    auto videoSources =
        Caster::videoSources(Caster::OptionsFlags::V4l2VideoSources |
                             Caster::OptionsFlags::DroidCamRawVideoSources |
                             Caster::OptionsFlags::X11CaptureVideoSources |
                             Caster::OptionsFlags::LipstickCaptureVideoSources);
    auto audioSources =
        Caster::audioSources(Caster::OptionsFlags::AllPaAudioSources);

    std::ostringstream os;

    auto dev2js = [&os](const auto& devs) {
        for (auto it = devs.cbegin(); it != devs.cend(); ++it) {
            os << fmt::format("{{\"name\":\"{}\",\"friendly_name\":\"{}\"}}",
                              it->name, it->friendlyName);
            if (std::next(it) != devs.cend()) os << ',';
        }
    };

    auto defaultDev = [](const auto& name, const auto& devs) {
        if (name.empty()) return name;
        if (std::find_if(devs.cbegin(), devs.cend(), [&](const auto& dev) {
                return dev.name == name;
            }) == devs.cend()) {
            return std::string{};
        }
        return name;
    };

#ifdef USE_SFOS
    static const auto* platform = "sfos";
#else
    static const auto* platform = "generic";
#endif

    os << "{\"server_name\":\"" << APP_NAME << "\",\"server_version\":\""
       << APP_VERSION << "\",\"platform\":\"" << platform
       << "\",\"video_sources\":[";
    dev2js(videoSources);
    os << "],\"audio_sources\":[";
    dev2js(audioSources);
    os << "],\"default_video_source\":\""
       << defaultDev(m_settings.videoSourceName, videoSources)
       << "\",\"default_audio_source\":\""
       << defaultDev(m_settings.audioSourceName, audioSources)
       << "\",\"default_video_orientation\":\""
       << m_settings.videoOrientationToStr()
       << "\",\"default_stream_format\":\"" << m_settings.streamFormatToStr()
       << "\",\"default_audio_volume\":\"" << m_settings.audioVolume
       << "\",\"default_audio_source_muted\":"
       << (m_settings.audioSourceMuted ? "true" : "false") << "}";

    responseHeaders.emplace_back("Content-Type", "application/json");

    m_server->pushData(id, os.str());

    return 200;
}

void Kamkast::startServer() {
    HttpServer::Config config;
    config.port = m_settings.port;
    config.address = m_settings.address;
    config.ifname = m_settings.ifname;
    config.connectionLimit = m_connectionLimit;

    m_server.emplace(
        config,
        /* new connection */
        [&](HttpServer::ConnectionId id, const char* url,
            [[maybe_unused]] const std::vector<HttpServer::Header>&
                requestHeaders,
            std::vector<HttpServer::Header>& responseHeaders) {
            auto turl = trimmed(url, '/');
            switch (determineRequestType(turl)) {
                case HttpRequestType::Invalid:
                    return 404;
                case HttpRequestType::WebUi:
                    if (m_settings.disableWebUi) {
                        LOGD("web ui is disabled");
                        return 404;
                    }
                    logConnection("web interface request", id);
                    return handleWebRequest(id, responseHeaders);
                case HttpRequestType::Stream:
                    logConnection("stream request", id);
                    return handleStreamRequest(m_settings, id, responseHeaders);
                case HttpRequestType::Ctrl:
                    if (m_settings.disableCtrlApi) {
                        LOGD("ctrl api is disabled");
                        return 404;
                    }
                    logConnection("control request", id);
                    return handleCtrlRequest(id, turl, responseHeaders);
                case HttpRequestType::Unknown:
                    logConnection("unknown request", id);
                    return 404;
            }

            return 404;
        },
        /* connection removed */
        [&](HttpServer::ConnectionId id) {
            if (id == m_castingConnId && m_caster && !m_caster->terminating()) {
                LOGD("connection was removed, so stopping caster");
                enqueueEvent({Event::Type::StopCaster, id, {}});
            }
        });
}

void Kamkast::stopServer() {
    m_server.reset();
    m_caster.reset();
}

std::string Kamkast::videoSourcesTable() {
    return sourcesTable(Caster::videoSources(
        Caster::OptionsFlags::V4l2VideoSources |
        Caster::OptionsFlags::DroidCamRawVideoSources |
        Caster::OptionsFlags::X11CaptureVideoSources |
        Caster::OptionsFlags::LipstickCaptureVideoSources));
}

std::string Kamkast::audioSourcesTable() {
    return sourcesTable(
        Caster::audioSources(Caster::OptionsFlags::AllPaAudioSources));
}

std::pair<std::string, std::string> Kamkast::sourcesTable() {
    return {videoSourcesTable(), audioSourcesTable()};
}

Event::ServerProps Kamkast::makeServerProps() const {
    Event::ServerProps props;

    if (!m_server) return props;

    auto port = m_server->port();
    auto addrs = m_server->listeningAddresses();

    props.webUrls.reserve(addrs.size());
    props.streamUrls.reserve(addrs.size());

    for (const auto& addr : addrs) {
        if (addr.find(':') == std::string::npos) {  // ipv4
            props.webUrls.push_back(
                fmt::format("http://{}:{}/{}", addr, port, m_settings.urlPath));
            props.streamUrls.push_back(fmt::format("http://{}:{}/{}{}", addr,
                                                   port, m_settings.urlPath,
                                                   m_streamUrlPath));
        } else {  // ipv6
            props.webUrls.push_back(fmt::format("http://[{}]:{}/{}", addr, port,
                                                m_settings.urlPath));
            props.streamUrls.push_back(fmt::format("http://[{}]:{}/{}{}", addr,
                                                   port, m_settings.urlPath,
                                                   m_streamUrlPath));
        }
    }

    return props;
}

void Kamkast::handleEvent(Event::Pack&& event) {
    LOGD("new event: " << event.type);
    switch (event.type) {
        case Event::Type::StartServer:
            startServer();
            notifyServerStarted();
            break;
        case Event::Type::StartCaster:
            stopCaster();
            startCaster(*event.connId, std::move(*event.settings));
            break;
        case Event::Type::StopCaster:
            stopCaster();
            break;
        case Event::Type::StopServer:
            stopCaster();
            stopServer();
            notifyServerEnded();
            shutdown();
            break;
        case Event::Type::CasterStarted:
            notifyCastingStarted(event.connId);
            break;
        case Event::Type::CasterEnded:
            notifyCastingEnded();
            break;
        default:
            LOGW("unhandled event");
    }
}

void Kamkast::start() {
    enqueueEvent(Event::Type::StartServer);

    std::visit([](auto& loop) { loop.start(); }, *m_loop);

    LOGD("event loop ended");
}
