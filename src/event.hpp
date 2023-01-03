/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef EVENT_HPP
#define EVENT_HPP

#include <optional>
#include <sstream>
#include <string>

#include "settings.hpp"

namespace Event {
enum class Type {
    StartServer,
    StopServer,
    StartCaster,
    StopCaster,
    CasterStarted,
    CasterEnded
};

struct Pack {
    Type type;
    std::optional<int> connId;
    std::optional<Settings> settings;
};

struct CastingProps {
    std::string clientAddress;
    std::string videoSource;
    std::string audioSource;
};

struct ServerProps {
    std::vector<std::string> webUrls;
    std::vector<std::string> streamUrls;
};

using Handler = std::function<void(Event::Pack&&)>;
}  // namespace Event

std::ostream& operator<<(std::ostream& os, Event::Type event);

std::ostream& operator<<(std::ostream& os, Event::Pack pack);

#endif // EVENT_HPP
