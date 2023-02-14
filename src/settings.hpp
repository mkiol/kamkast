/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "cxxopts.hpp"

#define DEFAULT_OPT(key) std::string{"default-"} + key

struct Settings {
    enum class StreamFormat { Mp4, MpegTs, Mp3 };
    enum class AudioMode { Enabled, Disabled };
    enum class VideoOrientation {
        Auto,
        Portrait,
        InvertedPortrait,
        Landscape,
        InvertedLandscape
    };
    enum class VideoEncoder { Auto, X264, Nvenc, V4l2 };

    static constexpr const char* sectionName = "General";

    static constexpr const char* configFileOpt = "config-file";
    static constexpr const char* urlPathOpt = "url-path";
    static constexpr const char* debugOpt = "debug";
    static constexpr const char* debugFileOpt = "debug-file";
    static constexpr const char* guiOpt = "gui";
    static constexpr const char* addressOpt = "address";
    static constexpr const char* ifnameOpt = "ifname";
    static constexpr const char* portOpt = "port";
    static constexpr const char* videoEncoderOpt = "video-encoder";
    static constexpr const char* streamFormatOpt = "stream-format";
    static constexpr const char* videoSourceNameOpt = "video-source";
    static constexpr const char* audioSourceNameOpt = "audio-source";
    static constexpr const char* audioVolumeOpt = "audio-volume";
    static constexpr const char* videoOrientationOpt = "video-orientation";
    static constexpr const char* ignoreUrlParamsOpt = "ignore-url-params";
    static constexpr const char* disableWebUiOpt = "disable-web-ui";
    static constexpr const char* disableCtrlApiOpt = "disable-ctrl-api";
    static constexpr const char* logRequestsOpt = "log-requests";
    static constexpr const char* logFileOpt = "log-file";
    static constexpr const char* audioSourceMutedOpt = "audio-source-muted";

    static constexpr const std::array urlOpts = {
        streamFormatOpt, videoSourceNameOpt,  audioSourceNameOpt,
        audioVolumeOpt,  audioSourceMutedOpt, videoOrientationOpt};

    static constexpr const std::array offValues = {
        "false", "no", "off", "0", "disable", "disabled"};
    static constexpr const std::array onValues = {"true", "yes",    "on",
                                                  "1",    "enable", "enabled"};
    bool debug = false;
    std::string debugFile;
    bool gui = false;
    bool ignoreUrlParams = false;
    bool disableWebUi = false;
    bool disableCtrlApi = false;
    bool logRequests = false;
    bool audioSourceMuted = false;
    int64_t port = 0;
    int audioVolume = 0;
    std::string urlPath;
    std::string ifname;
    std::string address;
    std::string logFile;
    std::string configFile;
    std::string videoSourceName;
    std::string audioSourceName;
    std::optional<StreamFormat> streamFormat;
    std::optional<VideoOrientation> videoOrientation;
    std::optional<VideoEncoder> videoEncoder;

    explicit Settings(const cxxopts::ParseResult& options);
    void updateFromStr(std::string_view key, std::string_view value);
    std::string streamFormatToStr() const;
    static std::optional<StreamFormat> streamFormatFromStr(
        std::string_view str);
    std::string videoOrientationToStr() const;
    static std::optional<VideoOrientation> videoOrientationFromStr(
        std::string_view str);
    std::string videoEncoderToStr() const;
    static std::optional<VideoEncoder> videoEncoderFromStr(
        std::string_view str);

    void saveToFile() const;
    void loadFromFile();
    void loadFromOpts(const cxxopts::ParseResult& options);
    void check();
    static int toInt(const std::string& str);
    static bool toBool(const std::string& str);
};

#endif // SETTINGS_H
