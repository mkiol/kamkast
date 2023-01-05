/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "settings.hpp"

#include <fmt/core.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>

#include "ini.h"
#include "logger.hpp"
#include "utils.hpp"

using namespace std::literals;

static bool fileReadable(const std::string& file) {
    return std::ifstream{file}.is_open();
}

static bool fileWrittable(const std::string& file) {
    return std::ofstream{file}.is_open();
}

static std::string randStr() {
    std::array chars{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A',
                     'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
                     'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
                     'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
                     'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',
                     't', 'u', 'v', 'w', 'x', 'y', 'z'};
    std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> dist{0, chars.size() - 1};

    std::string str(5, '\0');

    std::generate(str.begin(), str.end(),
                  [&]() { return chars.at(dist(generator)); });

    return str;
}

Settings::Settings(const cxxopts::ParseResult& options) {
    loadFromOpts(options);

    if (!configFile.empty()) loadFromFile();

    check();

    // save config to file when file name provided but file doesn't exist
    if (!configFile.empty() && !fileReadable(configFile)) saveToFile();
}

void Settings::loadFromOpts(const cxxopts::ParseResult& options) {
    LOGD("loading config from options");

    configFile = options[configFileOpt].as<std::string>();
    urlPath = options[urlPathOpt].as<std::string>();
    if (urlPath.empty()) urlPath = randStr();
    debug = options[debugOpt].as<bool>();
    debugFile = options[debugFileOpt].as<std::string>();
    gui = options[guiOpt].as<bool>();
    address = options[addressOpt].as<std::string>();
    ifname = options[ifnameOpt].as<std::string>();
    port = options[portOpt].as<int64_t>();
    videoEncoder = videoEncoderFromStr(
        trimmed(options[videoEncoderOpt].as<std::string>()));
    streamFormat = streamFormatFromStr(
        trimmed(options[DEFAULT_OPT(streamFormatOpt)].as<std::string>()));
    videoSourceName =
        options[DEFAULT_OPT(videoSourceNameOpt)].as<std::string>();
    audioSourceName =
        options[DEFAULT_OPT(audioSourceNameOpt)].as<std::string>();
    audioVolume = options[DEFAULT_OPT(audioVolumeOpt)].as<float>();
    videoOrientation = videoOrientationFromStr(
        trimmed(options[DEFAULT_OPT(videoOrientationOpt)].as<std::string>()));
    ignoreUrlParams = options[ignoreUrlParamsOpt].as<bool>();
    disableWebUi = options[disableWebUiOpt].as<bool>();
    disableCtrlApi = options[disableCtrlApiOpt].as<bool>();
    logRequests = options[logRequestsOpt].as<bool>();
    logFile = options[logFileOpt].as<std::string>();
}

void Settings::loadFromFile() {
    LOGD("loading config from file: " << configFile);

    mINI::INIFile f{configFile};
    mINI::INIStructure ini;

    if (!f.read(ini)) {
        LOGW("failed to read from config file");
        return;
    }

    if (!ini.has("General")) {
        LOGW("invalid config file");
        return;
    }

    auto to_bool = [](const auto& str) { return str == "1" || str == "true"; };
    auto to_int = [](const auto& str) {
        try {
            return std::stoi(str);
        } catch (...) {
            return 0;
        }
    };
    auto to_float = [](const auto& str) {
        try {
            return std::stof(str);
        } catch (...) {
            return 1.F;
        }
    };

    auto& sec = ini[sectionName];

    if (sec.has(urlPathOpt)) urlPath = sec[urlPathOpt];
    if (urlPath.empty()) urlPath = randStr();
    if (sec.has(debugOpt)) debug = to_bool(sec[debugOpt]);
    if (sec.has(debugFileOpt)) debugFile = sec[debugFileOpt];
    if (sec.has(guiOpt)) gui = to_bool(sec[guiOpt]);
    if (sec.has(addressOpt)) address = sec[addressOpt];
    if (sec.has(ifnameOpt)) ifname = sec[ifnameOpt];
    if (sec.has(portOpt)) port = to_int(sec[portOpt]);
    if (sec.has(videoEncoderOpt))
        videoEncoder = videoEncoderFromStr(sec[videoEncoderOpt]);
    if (sec.has(DEFAULT_OPT(streamFormatOpt)))
        streamFormat = streamFormatFromStr(sec[DEFAULT_OPT(streamFormatOpt)]);
    if (sec.has(DEFAULT_OPT(videoSourceNameOpt)))
        videoSourceName = sec[DEFAULT_OPT(videoSourceNameOpt)];
    if (sec.has(DEFAULT_OPT(audioSourceNameOpt)))
        audioSourceName = sec[DEFAULT_OPT(audioSourceNameOpt)];
    if (sec.has(DEFAULT_OPT(audioVolumeOpt)))
        audioVolume = to_float(sec[DEFAULT_OPT(audioVolumeOpt)]);
    if (sec.has(DEFAULT_OPT(videoOrientationOpt)))
        videoOrientation =
            videoOrientationFromStr(sec[DEFAULT_OPT(videoOrientationOpt)]);
    if (sec.has(ignoreUrlParamsOpt))
        ignoreUrlParams = to_bool(sec[ignoreUrlParamsOpt]);
    if (sec.has(disableWebUiOpt)) disableWebUi = to_bool(sec[disableWebUiOpt]);
    if (sec.has(disableCtrlApiOpt))
        disableCtrlApi = to_bool(sec[disableCtrlApiOpt]);
    if (sec.has(logRequestsOpt)) logRequests = to_bool(sec[logRequestsOpt]);
    if (sec.has(logFileOpt)) logFile = sec[logFileOpt];
}

void Settings::check() {
    auto invalidOption = [](const std::string& opt) {
        throw std::runtime_error("invalid option: "s + opt);
    };

    trim(configFile);
    trim(urlPath, '/');
    if (urlPath.empty()) invalidOption(urlPathOpt);
    trim(address);
    trim(ifname);
    if (port < 0 || port > std::numeric_limits<uint16_t>::max())
        invalidOption(portOpt);
    if (!videoEncoder) invalidOption(videoEncoderOpt);
    if (!streamFormat) invalidOption(DEFAULT_OPT(streamFormatOpt));
    trim(videoSourceName);
    if (std::find(offValues.cbegin(), offValues.cend(), videoSourceName) !=
        offValues.cend())
        videoSourceName.clear();
    trim(audioSourceName);
    if (std::find(offValues.cbegin(), offValues.cend(), audioSourceName) !=
        offValues.cend())
        audioSourceName.clear();
    if (audioVolume < 0.0 || audioVolume > 100.0)
        invalidOption(DEFAULT_OPT(audioVolumeOpt));
    if (!videoOrientation) invalidOption(DEFAULT_OPT(videoOrientationOpt));
    trim(logFile);
    if (!logFile.empty() && !fileWrittable(logFile)) {
        LOGW("failed to create log file: " << logFile);
        logFile.clear();
    }
}

void Settings::saveToFile() const {
    LOGD("saving config to file: " << configFile);

    mINI::INIStructure ini;
    auto& sec = ini[sectionName];

    sec[urlPathOpt] = urlPath;
    sec[addressOpt] = address;
    sec[ifnameOpt] = ifname;
    sec[portOpt] = std::to_string(port);
    sec[videoEncoderOpt] = videoEncoderToStr();
    sec[DEFAULT_OPT(streamFormatOpt)] = streamFormatToStr();
    sec[DEFAULT_OPT(videoSourceNameOpt)] = videoSourceName;
    sec[DEFAULT_OPT(audioSourceNameOpt)] = audioSourceName;
    sec[DEFAULT_OPT(audioVolumeOpt)] = std::to_string(audioVolume);
    sec[DEFAULT_OPT(videoOrientationOpt)] = videoOrientationToStr();
    sec[ignoreUrlParamsOpt] = std::to_string(ignoreUrlParams);
    sec[disableWebUiOpt] = std::to_string(disableWebUi);
    sec[disableCtrlApiOpt] = std::to_string(disableCtrlApi);
    sec[logRequestsOpt] = std::to_string(logRequests);
    sec[logFileOpt] = logFile;

    // sec[guiOpt] = std::to_string(gui);
    // sec[debugOpt] = std::to_string(debug);
    // sec[debugFileOpt] = debugFile;

    mINI::INIFile file{configFile};
    file.generate(ini);
}

static std::optional<float> strToFloat(std::string_view value) {
    std::stringstream ss;
    ss << value;
    float fvalue;
    if (ss >> fvalue) return fvalue;
    return std::nullopt;
}

void Settings::updateFromStr(std::string_view opt, std::string_view value) {
    auto invalidValue = [](auto opt, auto value) {
        LOGW("invalid '" << opt << "' param: " << value);
    };

    if (opt == audioSourceNameOpt) {
        if (std::find(offValues.cbegin(), offValues.cend(), value) !=
            offValues.cend())
            audioSourceName.clear();
        else
            audioSourceName = value;
    } else if (opt == audioVolumeOpt) {
        if (auto fvalue = strToFloat(value);
            fvalue && *fvalue >= 0 && *fvalue <= 100)
            audioVolume = *fvalue;
        else
            invalidValue(opt, value);
    } else if (opt == videoSourceNameOpt) {
        if (std::find(offValues.cbegin(), offValues.cend(), value) !=
            offValues.cend())
            videoSourceName.clear();
        else
            videoSourceName = value;
    } else if (opt == streamFormatOpt) {
        if (auto v = streamFormatFromStr(value))
            streamFormat = v.value();
        else
            invalidValue(opt, value);
    } else if (opt == videoOrientationOpt) {
        if (auto v = videoOrientationFromStr(value))
            videoOrientation = v.value();
        else
            invalidValue(opt, value);
    } else {
        LOGW("invalid url param: " << opt);
    }
}

std::string Settings::streamFormatToStr() const {
    if (streamFormat) {
        switch (*streamFormat) {
            case StreamFormat::Mp4:
                return "mp4";
            case StreamFormat::MpegTs:
                return "mpegts";
            case StreamFormat::Mp3:
                return "mp3";
        }
    }
    return "mp4";
}

std::optional<Settings::StreamFormat> Settings::streamFormatFromStr(
    std::string_view str) {
    if (str == "mp4") return StreamFormat::Mp4;
    if (str == "mpegts") return StreamFormat::MpegTs;
    if (str == "mp3") return StreamFormat::Mp3;
    return std::nullopt;
}

std::string Settings::videoOrientationToStr() const {
    if (videoOrientation) {
        switch (*videoOrientation) {
            case VideoOrientation::Auto:
                return "auto";
            case VideoOrientation::Landscape:
                return "landscape";
            case VideoOrientation::InvertedLandscape:
                return "inverted-landscape";
            case VideoOrientation::Portrait:
                return "portrait";
            case VideoOrientation::InvertedPortrait:
                return "inverted-portrait";
        }
    }
    return "auto";
}

std::optional<Settings::VideoOrientation> Settings::videoOrientationFromStr(
    std::string_view str) {
    if (str == "auto") return VideoOrientation::Auto;
    if (str == "landscape") return VideoOrientation::Landscape;
    if (str == "inverted-landscape") return VideoOrientation::InvertedLandscape;
    if (str == "portrait") return VideoOrientation::Portrait;
    if (str == "inverted-portrait") return VideoOrientation::InvertedPortrait;
    return std::nullopt;
}

std::string Settings::videoEncoderToStr() const {
    if (videoEncoder) {
        switch (*videoEncoder) {
            case VideoEncoder::Auto:
                return "auto";
            case VideoEncoder::Nvenc:
                return "nvenc";
            case VideoEncoder::V4l2:
                return "v4l2";
            case VideoEncoder::X264:
                return "x264";
        }
    }
    return "auto";
}

std::optional<Settings::VideoEncoder> Settings::videoEncoderFromStr(
    std::string_view str) {
    if (str == "auto") return VideoEncoder::Auto;
    if (str == "nvenc") return VideoEncoder::Nvenc;
    if (str == "v4l2") return VideoEncoder::V4l2;
    if (str == "x264") return VideoEncoder::X264;
    return std::nullopt;
}
