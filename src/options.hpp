/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef OPTIONS_H
#define OPTIONS_H

#include <string>

#include "cxxopts.hpp"
#include "settings.hpp"

class Options {
   public:
    enum class Command {
        None,
        Help,
        ListSources,
        ListAudioSources,
        ListVideoSources
    };

    explicit Options(int argc, char** argv);
    Command command() const;
    Settings settings() const;
    std::string help() const;

   private:
    cxxopts::Options m_options;
    cxxopts::ParseResult m_result;
};

#endif  // OPTIONS_H
