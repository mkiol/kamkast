/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "options.hpp"

#include "config.h"
#include "logger.hpp"

using namespace std::literals;

Options::Options(int argc, char** argv) : m_options{APP_NAME, APP_DESC} {
    // clang-format off
    m_options.add_options()
        ("u,"s + Settings::urlPathOpt, "A path portion of URL. Server rejects requests with invalid path. If path is not given it will be generated.",
            cxxopts::value<std::string>()->default_value(""))
        ("p,"s + Settings::portOpt, "Listening port. Port 0 means any port.",
            cxxopts::value<int64_t>()->default_value("0"))
        ("a,"s + Settings::addressOpt, "IP address to listen on. Missing or 0.0.0.0 means listen for requests on all available interfaces.",
            cxxopts::value<std::string>()->default_value("0.0.0.0"))
        ("i,"s + Settings::ifnameOpt, "Network interface to listen on. Missing or empty means listen for requests on all available interfaces. This option works only when --address is not set.",
            cxxopts::value<std::string>()->default_value(""))
        (DEFAULT_OPT(Settings::streamFormatOpt), "Set the default stream format. Supported formats: mp4, mpegts, mp3.",
            cxxopts::value<std::string>()->default_value("mp4"))
        (DEFAULT_OPT(Settings::videoSourceNameOpt), "Set the id of default video source. Use --list-video-sources to get available sources. Missing or empty means that by default video is disabled.",
            cxxopts::value<std::string>()->default_value(""))
        (DEFAULT_OPT(Settings::audioSourceNameOpt), "Set the id od default audio source. Use --list-audio-sources to get available sources. Missing or empty means that by default audio is disabled.",
            cxxopts::value<std::string>()->default_value(""))
        (DEFAULT_OPT(Settings::videoOrientationOpt), "Set the default video orientation. Supported orientations: auto, landscape, inverted-landscape, portrait, inverted-portrait",
            cxxopts::value<std::string>()->default_value("auto"))
        (DEFAULT_OPT(Settings::audioVolumeOpt), "Set the default audio volume. Valid values are in a range 0.0-10.0. Value 0 mutes the audio. Value 1 means volume is not changed.",
            cxxopts::value<float>()->default_value("1.0"))
        (Settings::ignoreUrlParamsOpt, "URL parameters in a request are ignored. Only default options are used.",
            cxxopts::value<bool>()->default_value("false"))
        ("list-sources", "Show all video and audio sources detected.")
        ("list-video-sources", "Show all video sources detected.")
        ("list-audio-sources", "Show all audio sources detected.")
        (Settings::disableWebUiOpt, "Requests for web interface are ignored. Only stream requests are accepted.",
            cxxopts::value<bool>()->default_value("false"))
        (Settings::disableCtrlApiOpt, "Requests to control API are ignored. Web UI cannot work when API is disabled.",
            cxxopts::value<bool>()->default_value("false"))
        (Settings::logRequestsOpt, "Print (to stdout) details of every request received.",
            cxxopts::value<bool>()->default_value("false"))
        (Settings::logFileOpt, "File where details of every received request are logged.",
            cxxopts::value<std::string>()->default_value(""))
        (Settings::videoEncoderOpt, "Force specific video encoder. Supported values: auto, nvenc, v4l2, x264",
            cxxopts::value<std::string>()->default_value("auto"))
        ("g,"s + Settings::guiOpt, "Start native graphical UI. GUI is not supported on every platform.",
            cxxopts::value<bool>()->default_value("false"))
        ("c,"s + Settings::configFileOpt, "Configuration file. When file doesn't exist, it is created based on command-line options provided. Configuration file takes precedence over any conflicting command-line options",
            cxxopts::value<std::string>()->default_value(""))
        ("d,"s + Settings::debugOpt, "Enable debugging logs (stderr)",
            cxxopts::value<bool>()->default_value("false"))
        (Settings::debugFileOpt, "File where debugging logs are written when --debug is enabled (instead of stderr).",
            cxxopts::value<std::string>()->default_value(""))
        ("h,help", "Print usage");
    // clang-format on

    m_result = m_options.parse(argc, argv);

    if (m_result[Settings::debugOpt].as<bool>())
        Logger::setLevel(Logger::LogType::Trace);
}

Options::Command Options::command() const {
    if (m_result.count("help")) return Command::Help;
    if (m_result.count("list-sources")) return Command::ListSources;
    if (m_result.count("list-audio-sources")) return Command::ListAudioSources;
    if (m_result.count("list-video-sources")) return Command::ListVideoSources;
    return Command::None;
}

std::string Options::help() const { return m_options.help(); }

Settings Options::settings() const { return Settings{m_result}; }
