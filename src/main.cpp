/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <fmt/format.h>
#include <signal.h>

#include "avlogger.hpp"
#include "kamkast.hpp"
#include "logger.hpp"
#include "options.hpp"
#include "settings.hpp"

static std::optional<Settings> processOpts(int argc, char** argv) {
    Options options{argc, argv};

    switch (options.command()) {
        case Options::Command::Help:
            fmt::print(
                "{}\nURL format:\n  Web interface URL\n   {}\n  Control URL\n  "
                " "
                "{}\n   (cmds: {})\n  Stream URL\n   "
                "{}\n   "
                "(params: {})\n",
                options.help(), "http://[address]:[port]/[url-path]",
                "http://[address]:[port]/[url-path]/ctrl/[cmd]", "info",
                "http://[address]:[port]/[url-path]/"
                "stream?[param1]=[value1]&[paramN]=[valueN]",
                fmt::join(Settings::urlOpts, ", "));
            break;
        case Options::Command::ListSources: {
            const auto& [v, a] = Kamkast::sourcesTable();
            fmt::print("Video sources:\n{}\nAudio sources:\n{}\n", v, a);
            break;
        }
        case Options::Command::ListVideoSources:
            fmt::print("Video sources:\n{}\n", Kamkast::videoSourcesTable());
            break;
        case Options::Command::ListAudioSources:
            fmt::print("Audio sources:\n{}\n", Kamkast::audioSourcesTable());
            break;
        case Options::Command::None:
            return options.settings();
    }

    return std::nullopt;
}

static Kamkast* gkamkast = nullptr;
static void signalHandler(int sig) {
    LOGD("received signal: " << sig);
    gkamkast->shutdown();
}

int main(int argc, char** argv) {
    try {
        auto settings = processOpts(argc, argv);
        if (!settings) return 0;

#ifdef USE_TRACE_LOGS
        if (settings->debug)
            Logger::init(Logger::LogType::Trace, settings->debugFile);
#else
        if (settings->debug)
            Logger::init(Logger::LogType::Debug, settings->debugFile);
#endif
        initAvLogger();

        Kamkast kamkast{std::move(*settings), argc, argv};

        gkamkast = &kamkast;
        signal(SIGINT, signalHandler);

        kamkast.start();
    } catch (const std::exception& e) {
        fmt::print(stderr, "Error: {}\n", e.what());
    }

    return 0;
}
